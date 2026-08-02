#pragma once

#include <windows.h>

namespace cqt
{

struct ResumeValidationPolicy
{
    ULONGLONG graceMilliseconds = 15000;
    UINT validationIntervalMilliseconds = 1000;

    [[nodiscard]] constexpr ULONGLONG DeadlineFrom(ULONGLONG now) const
    {
        return now + graceMilliseconds;
    }

    [[nodiscard]] constexpr bool ShouldDefer(
        ULONGLONG now,
        ULONGLONG deadline) const
    {
        return deadline != 0 && now < deadline;
    }
};

inline constexpr ResumeValidationPolicy kResumeValidationPolicy{};

} // namespace cqt
