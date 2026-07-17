#include "taskbar/TaskbarCollision.h"

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace
{

int failures = 0;

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

    std::printf("taskbar collision summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
