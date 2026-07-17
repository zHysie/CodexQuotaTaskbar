#pragma once

#include "usage/CodexAuthReader.h"
#include "usage/HttpTransport.h"
#include "usage/UsageModels.h"

#include <optional>

namespace cqt
{

template <typename Snapshot>
struct EndpointFetchResult
{
    Snapshot snapshot;
    std::optional<long long> retryAfterSeconds;
};

class CodexUsageClient
{
public:
    explicit CodexUsageClient(IHttpTransport& transport) : transport_(transport) {}

    [[nodiscard]] EndpointFetchResult<UsageSnapshot> FetchUsage(
        const CodexCredentials& credentials, long long nowUnixSeconds);
    [[nodiscard]] EndpointFetchResult<ResetCreditsSnapshot> FetchResetCredits(
        const CodexCredentials& credentials, long long nowUnixSeconds);
    void Cancel() { transport_.Cancel(); }

private:
    IHttpTransport& transport_;
};

} // namespace cqt
