#pragma once

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace cqt
{

struct HorizontalInterval
{
    long left = 0;
    long right = 0;
};

struct RectangleEdges
{
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

inline long long RectangleWidth(const RectangleEdges& rect)
{
    return static_cast<long long>(rect.right) - rect.left;
}

inline long long RectangleHeight(const RectangleEdges& rect)
{
    return static_cast<long long>(rect.bottom) - rect.top;
}

inline bool CoordinateNear(long left, long right, long tolerance)
{
    tolerance = std::max(0L, tolerance);
    const auto difference = static_cast<long long>(left) - right;
    return difference >= -static_cast<long long>(tolerance)
        && difference <= static_cast<long long>(tolerance);
}

inline bool RectanglesApproximatelyEqual(
    const RectangleEdges& left,
    const RectangleEdges& right,
    long tolerance)
{
    return CoordinateNear(left.left, right.left, tolerance)
        && CoordinateNear(left.top, right.top, tolerance)
        && CoordinateNear(left.right, right.right, tolerance)
        && CoordinateNear(left.bottom, right.bottom, tolerance);
}

inline bool RectangleInside(
    const RectangleEdges& inner,
    const RectangleEdges& outer,
    long tolerance = 0)
{
    tolerance = std::max(0L, tolerance);
    return inner.left >= outer.left - tolerance
        && inner.top >= outer.top - tolerance
        && inner.right <= outer.right + tolerance
        && inner.bottom <= outer.bottom + tolerance;
}

inline bool RectangleCovers(
    const RectangleEdges& covering,
    const RectangleEdges& covered,
    long tolerance = 0)
{
    return RectangleWidth(covering) > 0 && RectangleHeight(covering) > 0
        && RectangleWidth(covered) > 0 && RectangleHeight(covered) > 0
        && RectangleInside(covered, covering, tolerance);
}

inline bool RectanglesIntersect(const RectangleEdges& left, const RectangleEdges& right)
{
    return left.left < right.right && left.right > right.left
        && left.top < right.bottom && left.bottom > right.top;
}

inline bool IsExcludedExternalWindowClass(std::wstring_view className)
{
    return className == L"CodexQuotaTaskbar.Display"
        || className == L"CodexQuotaTaskbar.Prototype.Display"
        || className == L"tooltips_class32"
        || className == L"#32768";
}

struct ExternalWindowCandidate
{
    RectangleEdges windowRect{};
    RectangleEdges taskbarRect{};
    RectangleEdges baseSafeRect{};
    RectangleEdges monitorRect{};
    bool topLevel = false;
    bool visible = false;
    bool cloaked = false;
    bool excludedProcess = false;
    bool excludedClass = false;
    long minimumExtent = 1;
    long maximumWidth = 1;
    long fullscreenTolerance = 0;
};

inline bool IsExternalObstacleCandidate(const ExternalWindowCandidate& candidate)
{
    if (!candidate.topLevel || !candidate.visible || candidate.cloaked
        || candidate.excludedProcess || candidate.excludedClass)
        return false;

    const long long width = RectangleWidth(candidate.windowRect);
    const long long height = RectangleHeight(candidate.windowRect);
    const long long taskbarHeight = RectangleHeight(candidate.taskbarRect);
    if (width < candidate.minimumExtent || height < candidate.minimumExtent
        || taskbarHeight <= 0 || height > taskbarHeight * 2
        || width > candidate.maximumWidth)
        return false;

    if (RectangleCovers(candidate.windowRect, candidate.monitorRect, candidate.fullscreenTolerance))
        return false;

    if (!RectanglesIntersect(candidate.windowRect, candidate.taskbarRect)
        || !RectanglesIntersect(candidate.windowRect, candidate.baseSafeRect))
        return false;

    const long overlapHeight = std::min(candidate.windowRect.bottom, candidate.taskbarRect.bottom)
        - std::max(candidate.windowRect.top, candidate.taskbarRect.top);
    const long overlapWidth = std::min(candidate.windowRect.right, candidate.baseSafeRect.right)
        - std::max(candidate.windowRect.left, candidate.baseSafeRect.left);
    return overlapHeight >= std::max<long>(8, static_cast<long>(taskbarHeight / 4))
        && overlapWidth >= candidate.minimumExtent;
}

enum class StableCollisionDecision
{
    Clear,
    Pending,
    Confirmed,
};

struct StableCollisionState
{
    unsigned int consecutiveSamples = 0;
    std::optional<HorizontalInterval> lastSafeInterval;
};

inline StableCollisionDecision ObserveCollisionSample(
    bool collides,
    std::optional<HorizontalInterval> safeInterval,
    long tolerance,
    unsigned int requiredSamples,
    StableCollisionState& state)
{
    if (!collides)
    {
        state = {};
        return StableCollisionDecision::Clear;
    }

    requiredSamples = std::max(1U, requiredSamples);
    tolerance = std::max(0L, tolerance);
    const bool sameInterval = (!safeInterval && !state.lastSafeInterval)
        || (safeInterval && state.lastSafeInterval
            && CoordinateNear(safeInterval->left, state.lastSafeInterval->left, tolerance)
            && CoordinateNear(safeInterval->right, state.lastSafeInterval->right, tolerance));
    if (!sameInterval)
        state.consecutiveSamples = 0;

    state.lastSafeInterval = safeInterval;
    if (state.consecutiveSamples < requiredSamples)
        ++state.consecutiveSamples;
    return state.consecutiveSamples >= requiredSamples
        ? StableCollisionDecision::Confirmed
        : StableCollisionDecision::Pending;
}

inline std::optional<HorizontalInterval> ClampIntervalToSafeArea(
    HorizontalInterval current,
    HorizontalInterval safe,
    long minimumWidth)
{
    if (safe.right <= safe.left || minimumWidth <= 0 || safe.right - safe.left < minimumWidth)
        return std::nullopt;

    const long currentWidth = current.right - current.left;
    if (currentWidth <= 0) return std::nullopt;
    if (current.left >= safe.left && current.right <= safe.right)
        return current;

    const long targetWidth = std::min(currentWidth, safe.right - safe.left);
    if (targetWidth < minimumWidth) return std::nullopt;
    const long targetLeft = std::clamp(current.left, safe.left, safe.right - targetWidth);
    return HorizontalInterval{targetLeft, targetLeft + targetWidth};
}

inline bool IsIntervalFree(
    HorizontalInterval candidate,
    HorizontalInterval base,
    const std::vector<HorizontalInterval>& occupied,
    long margin)
{
    if (candidate.right <= candidate.left
        || candidate.left < base.left || candidate.right > base.right)
        return false;

    margin = std::max(0L, margin);
    for (const auto& interval : occupied)
    {
        const long left = std::max(base.left, interval.left - margin);
        const long right = std::min(base.right, interval.right + margin);
        if (right > left && candidate.left < right && candidate.right > left)
            return false;
    }
    return true;
}

inline std::vector<HorizontalInterval> CollectFreeIntervals(
    HorizontalInterval base,
    std::vector<HorizontalInterval> occupied,
    long margin,
    long minimumWidth)
{
    std::vector<HorizontalInterval> freeIntervals;
    if (base.right <= base.left || minimumWidth <= 0) return freeIntervals;
    margin = std::max(0L, margin);

    std::vector<HorizontalInterval> clipped;
    clipped.reserve(occupied.size());
    for (const auto& interval : occupied)
    {
        const long left = std::max(base.left, interval.left - margin);
        const long right = std::min(base.right, interval.right + margin);
        if (right > left) clipped.push_back({left, right});
    }
    std::sort(clipped.begin(), clipped.end(), [](const auto& left, const auto& right) {
        return left.left < right.left || (left.left == right.left && left.right < right.right);
    });

    long cursor = base.left;
    for (const auto& interval : clipped)
    {
        if (interval.left - cursor >= minimumWidth)
            freeIntervals.push_back({cursor, interval.left});
        cursor = std::max(cursor, interval.right);
        if (cursor >= base.right) break;
    }
    if (base.right - cursor >= minimumWidth)
        freeIntervals.push_back({cursor, base.right});
    return freeIntervals;
}

inline std::optional<HorizontalInterval> SelectRightmostFreeInterval(
    HorizontalInterval base,
    std::vector<HorizontalInterval> occupied,
    long margin,
    long minimumWidth)
{
    const auto freeIntervals = CollectFreeIntervals(
        base, std::move(occupied), margin, minimumWidth);
    if (freeIntervals.empty()) return std::nullopt;
    return freeIntervals.back();
}

inline std::optional<HorizontalInterval> SelectNearestFreeInterval(
    HorizontalInterval base,
    std::vector<HorizontalInterval> occupied,
    long margin,
    long minimumWidth,
    HorizontalInterval current)
{
    const long currentWidth = current.right - current.left;
    if (currentWidth <= 0) return std::nullopt;

    const auto freeIntervals = CollectFreeIntervals(
        base, std::move(occupied), margin, minimumWidth);
    std::optional<HorizontalInterval> selected;
    long long selectedDistance = 0;
    for (const auto& interval : freeIntervals)
    {
        const long width = std::min(currentWidth, interval.right - interval.left);
        if (width < minimumWidth) continue;

        const long targetLeft = std::clamp(current.left, interval.left, interval.right - width);
        const long long signedDistance = static_cast<long long>(targetLeft) - current.left;
        const long long distance = signedDistance < 0 ? -signedDistance : signedDistance;
        const HorizontalInterval candidate{targetLeft, targetLeft + width};
        if (!selected || distance < selectedDistance
            || (distance == selectedDistance && candidate.left < selected->left))
        {
            selected = candidate;
            selectedDistance = distance;
        }
    }
    return selected;
}

} // namespace cqt
