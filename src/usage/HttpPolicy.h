#pragma once

#include "usage/HttpTransport.h"

#include <optional>
#include <string_view>

namespace cqt
{

constexpr std::size_t kMaximumResponseHeaderBytes = 64ULL * 1024ULL;
constexpr std::size_t kMaximumResponseBodyBytes = 1024ULL * 1024ULL;
constexpr long long kMaximumRetryAfterSeconds = 24LL * 60 * 60;

enum class HttpResponseClass
{
    Success,
    AuthenticationFailure,
    RateLimited,
    ServerFailure,
    RedirectRejected,
    OtherFailure,
    TransportFailure,
    ResponseTooLarge
};

[[nodiscard]] HttpResponseClass ClassifyResponse(const HttpResponse& response);
[[nodiscard]] std::optional<long long> ParseRetryAfterSeconds(
    std::wstring_view value, long long nowUnixSeconds);
[[nodiscard]] long long EffectiveRetryDelaySeconds(
    std::optional<long long> serverDelay, long long localBackoffSeconds);
[[nodiscard]] int UsageBackoffSeconds(int consecutiveFailures);

} // namespace cqt
