#include "taskbar/TaskbarCollision.h"

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace
{

int failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

void CheckInterval(const char* name, cqt::HorizontalInterval base,
                   std::initializer_list<cqt::HorizontalInterval> occupied,
                   long margin, long minimumWidth,
                   std::optional<cqt::HorizontalInterval> expected)
{
    const auto actual = cqt::SelectRightmostFreeInterval(
        base, std::vector<cqt::HorizontalInterval>(occupied), margin, minimumWidth);
    const bool matches = (!actual && !expected)
        || (actual && expected && actual->left == expected->left && actual->right == expected->right);
    std::printf("[%s] %s\n", matches ? "PASS" : "FAIL", name);
    if (!matches) ++failures;
}

cqt::ExternalWindowCandidate NormalExternalCandidate()
{
    return {
        {1200, 1036, 1300, 1078},
        {0, 1032, 1920, 1080},
        {900, 1032, 1560, 1080},
        {0, 0, 1920, 1080},
        true,
        true,
        false,
        false,
        false,
        12,
        640,
        2};
}

} // namespace

int main()
{
    CheckInterval("empty safe area remains unchanged", {1553, 2161}, {}, 8, 68, {{1553, 2161}});
    CheckInterval("TrafficMonitor-sized right obstacle is avoided", {1553, 2161}, {{2069, 2171}}, 8, 68,
                  {{1553, 2061}});
    CheckInterval("rightmost usable segment is preferred", {100, 500}, {{250, 330}}, 10, 60,
                  {{340, 500}});
    CheckInterval("small right gap falls back to left segment", {100, 500}, {{410, 450}}, 10, 60,
                  {{100, 400}});
    CheckInterval("overlapping obstacles are merged by scan", {100, 500}, {{190, 300}, {250, 420}}, 10, 60,
                  {{430, 500}});
    CheckInterval("insufficient space is rejected", {100, 200}, {{120, 180}}, 10, 40, std::nullopt);
    CheckInterval("outside obstacles do not affect safe area", {100, 500}, {{0, 50}, {550, 600}}, 8, 68,
                  {{100, 500}});

    Check(
        cqt::RectanglesApproximatelyEqual(
            {100, 200, 300, 400}, {102, 198, 302, 401}, 2),
        "two-DIP rectangle jitter is ignored");
    Check(
        !cqt::RectanglesApproximatelyEqual(
            {100, 200, 300, 400}, {103, 200, 300, 400}, 2),
        "rectangle movement beyond tolerance is detected");

    const auto expanded = cqt::ClampIntervalToSafeArea({400, 480}, {100, 600}, 68);
    Check(
        expanded && expanded->left == 400 && expanded->right == 480,
        "safe-area expansion does not move the current window right");
    const auto shifted = cqt::ClampIntervalToSafeArea({430, 510}, {100, 480}, 68);
    Check(
        shifted && shifted->left == 400 && shifted->right == 480,
        "collision clamps the same window to the nearest safe position");
    Check(
        !cqt::ClampIntervalToSafeArea({430, 510}, {100, 150}, 68),
        "safe area below minimum width is rejected");

    Check(
        cqt::IsIntervalFree({180, 260}, {100, 600}, {{300, 400}}, 0),
        "current placement remains safe when another right-side gap appears");
    const auto rightmostAfterShrink = cqt::SelectRightmostFreeInterval(
        {100, 600}, {{300, 400}}, 0, 68);
    const auto nearestAfterShrink = cqt::SelectNearestFreeInterval(
        {100, 600}, {{300, 400}}, 0, 68, {180, 260});
    Check(
        rightmostAfterShrink && rightmostAfterShrink->left == 400
            && nearestAfterShrink && nearestAfterShrink->left == 180
            && nearestAfterShrink->right == 260,
        "TrafficMonitor shrink does not pull a safe current window into a new right gap");
    const auto nearestCollision = cqt::SelectNearestFreeInterval(
        {100, 460}, {{300, 400}}, 0, 68, {350, 430});
    Check(
        nearestCollision && nearestCollision->left == 220 && nearestCollision->right == 300,
        "sustained collision selects the nearest usable left interval");

    cqt::StableCollisionState collisionState;
    Check(
        cqt::ObserveCollisionSample(true, {{100, 500}}, 2, 3, collisionState)
            == cqt::StableCollisionDecision::Pending,
        "first collision sample remains pending");
    Check(
        cqt::ObserveCollisionSample(true, {{101, 499}}, 2, 3, collisionState)
            == cqt::StableCollisionDecision::Pending,
        "second approximately equal collision remains pending");
    Check(
        cqt::ObserveCollisionSample(true, {{100, 500}}, 2, 3, collisionState)
            == cqt::StableCollisionDecision::Confirmed,
        "third consecutive collision is confirmed");
    Check(
        cqt::ObserveCollisionSample(false, {{100, 500}}, 2, 3, collisionState)
            == cqt::StableCollisionDecision::Clear
            && collisionState.consecutiveSamples == 0,
        "clear sample resets collision confirmation");

    cqt::StableCollisionState changedCandidateState;
    static_cast<void>(cqt::ObserveCollisionSample(
        true, {{100, 500}}, 2, 3, changedCandidateState));
    static_cast<void>(cqt::ObserveCollisionSample(
        true, {{104, 500}}, 2, 3, changedCandidateState));
    Check(
        changedCandidateState.consecutiveSamples == 1
            && cqt::ObserveCollisionSample(true, {{104, 500}}, 2, 3, changedCandidateState)
                == cqt::StableCollisionDecision::Pending,
        "different collision interval restarts confirmation");

    cqt::StableCollisionState noSpaceState;
    static_cast<void>(cqt::ObserveCollisionSample(true, std::nullopt, 2, 3, noSpaceState));
    static_cast<void>(cqt::ObserveCollisionSample(true, std::nullopt, 2, 3, noSpaceState));
    Check(
        cqt::ObserveCollisionSample(true, std::nullopt, 2, 3, noSpaceState)
            == cqt::StableCollisionDecision::Confirmed,
        "three consecutive no-space samples are confirmed");

    auto candidate = NormalExternalCandidate();
    Check(cqt::IsExternalObstacleCandidate(candidate), "visible top-level taskbar widget is retained");
    candidate.topLevel = false;
    Check(!cqt::IsExternalObstacleCandidate(candidate), "child window is excluded");
    candidate = NormalExternalCandidate();
    candidate.cloaked = true;
    Check(!cqt::IsExternalObstacleCandidate(candidate), "DWM-cloaked window is excluded");
    candidate = NormalExternalCandidate();
    candidate.windowRect = candidate.monitorRect;
    candidate.maximumWidth = 3000;
    Check(!cqt::IsExternalObstacleCandidate(candidate), "fullscreen overlay is excluded");
    Check(cqt::IsExcludedExternalWindowClass(L"tooltips_class32"), "tooltip class is excluded");
    Check(cqt::IsExcludedExternalWindowClass(L"#32768"), "menu class is excluded");

    std::printf("taskbar collision summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
