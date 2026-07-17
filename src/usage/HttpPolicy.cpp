#include "usage/HttpPolicy.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <string>

namespace
{

bool SystemTimeToUnix(const SYSTEMTIME& time, long long& value)
{
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&time, &fileTime))
    {
        return false;
    }
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    constexpr unsigned long long kUnixEpochTicks = 116444736000000000ULL;
    if (ticks.QuadPart < kUnixEpochTicks)
    {
        return false;
    }
    value = static_cast<long long>((ticks.QuadPart - kUnixEpochTicks) / 10000000ULL);
    return true;
}

} // namespace

namespace cqt
{

HttpResponseClass ClassifyResponse(const HttpResponse& response)
{
    if (response.transportError == TransportError::ResponseBodyTooLarge
        || response.transportError == TransportError::ResponseHeadersTooLarge
        || response.body.size() > kMaximumResponseBodyBytes)
    {
        return HttpResponseClass::ResponseTooLarge;
    }
    if (response.transportError != TransportError::None)
    {
        return HttpResponseClass::TransportFailure;
    }
    if (response.statusCode >= 200 && response.statusCode <= 299)
    {
        return HttpResponseClass::Success;
    }
    if (response.statusCode == 401 || response.statusCode == 403)
    {
        return HttpResponseClass::AuthenticationFailure;
    }
    if (response.statusCode == 429)
    {
        return HttpResponseClass::RateLimited;
    }
    if (response.statusCode >= 500 && response.statusCode <= 599)
    {
        return HttpResponseClass::ServerFailure;
    }
    if (response.statusCode >= 300 && response.statusCode <= 399)
    {
        return HttpResponseClass::RedirectRejected;
    }
    return HttpResponseClass::OtherFailure;
}

std::optional<long long> ParseRetryAfterSeconds(std::wstring_view input, long long nowUnixSeconds)
{
    while (!input.empty() && std::iswspace(input.front())) input.remove_prefix(1);
    while (!input.empty() && std::iswspace(input.back())) input.remove_suffix(1);
    if (input.empty())
    {
        return std::nullopt;
    }

    long long seconds = 0;
    bool digitsOnly = true;
    for (const wchar_t character : input)
    {
        if (character < L'0' || character > L'9')
        {
            digitsOnly = false;
            break;
        }
        const int digit = character - L'0';
        if (seconds > (std::numeric_limits<long long>::max() - digit) / 10)
        {
            return std::nullopt;
        }
        seconds = seconds * 10 + digit;
    }
    if (digitsOnly)
    {
        return std::min(seconds, kMaximumRetryAfterSeconds);
    }

    std::wstring value(input);
    SYSTEMTIME systemTime{};
    if (!WinHttpTimeToSystemTime(value.c_str(), &systemTime))
    {
        return std::nullopt;
    }
    long long target = 0;
    if (!SystemTimeToUnix(systemTime, target) || target <= nowUnixSeconds)
    {
        return std::nullopt;
    }
    return std::min(target - nowUnixSeconds, kMaximumRetryAfterSeconds);
}

long long EffectiveRetryDelaySeconds(std::optional<long long> serverDelay, long long localBackoffSeconds)
{
    const long long local = std::clamp(localBackoffSeconds, 0LL, kMaximumRetryAfterSeconds);
    if (!serverDelay)
    {
        return local;
    }
    return std::max(local, std::clamp(*serverDelay, 0LL, kMaximumRetryAfterSeconds));
}

int UsageBackoffSeconds(int consecutiveFailures)
{
    if (consecutiveFailures <= 1) return 60;
    if (consecutiveFailures == 2) return 120;
    if (consecutiveFailures == 3) return 300;
    return 600;
}

} // namespace cqt
