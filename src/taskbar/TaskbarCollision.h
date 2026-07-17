#pragma once

#include <algorithm>
#include <optional>
#include <vector>

namespace cqt
{

struct HorizontalInterval
{
    long left = 0;
    long right = 0;
};

inline std::optional<HorizontalInterval> SelectRightmostFreeInterval(
    HorizontalInterval base,
    std::vector<HorizontalInterval> occupied,
    long margin,
    long minimumWidth)
{
    if (base.right <= base.left || minimumWidth <= 0) return std::nullopt;
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

    std::optional<HorizontalInterval> selected;
    long cursor = base.left;
    for (const auto& interval : clipped)
    {
        if (interval.left - cursor >= minimumWidth) selected = HorizontalInterval{cursor, interval.left};
        cursor = std::max(cursor, interval.right);
        if (cursor >= base.right) break;
    }
    if (base.right - cursor >= minimumWidth) selected = HorizontalInterval{cursor, base.right};
    return selected;
}

} // namespace cqt
