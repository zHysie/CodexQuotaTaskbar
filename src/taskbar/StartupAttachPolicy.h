#pragma once

#include <windows.h>

namespace cqt
{

struct StartupAttachPolicy
{
    int maximumAttempts = 6;
    ULONGLONG firstRetryDelayMilliseconds = 1500;
    ULONGLONG laterRetryDelayMilliseconds = 3000;

    [[nodiscard]] constexpr bool ShouldRetry(bool retryable, int completedAttempts) const
    {
        return retryable && completedAttempts < maximumAttempts;
    }

    [[nodiscard]] constexpr ULONGLONG DelayBeforeNextAttempt(int completedAttempts) const
    {
        return completedAttempts <= 1
            ? firstRetryDelayMilliseconds
            : laterRetryDelayMilliseconds;
    }
};

inline constexpr StartupAttachPolicy kStartupAttachPolicy{};

template <typename Attempt, typename Wait>
[[nodiscard]] bool RunStartupAttachSequence(
    const StartupAttachPolicy& policy,
    Attempt&& attempt,
    Wait&& wait)
{
    for (int completedAttempts = 0; completedAttempts < policy.maximumAttempts;)
    {
        bool retryable = false;
        ++completedAttempts;
        if (attempt(retryable)) return true;
        if (!policy.ShouldRetry(retryable, completedAttempts)) return false;
        if (!wait(policy.DelayBeforeNextAttempt(completedAttempts))) return false;
    }
    return false;
}

} // namespace cqt
