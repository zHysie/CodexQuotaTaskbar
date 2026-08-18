#pragma once

#include "usage/HttpTransport.h"
#include "usage/UsageModels.h"
#include "usage/ZhipuAuthReader.h"

namespace cqt
{

class ZhipuUsageClient
{
public:
    explicit ZhipuUsageClient(IHttpTransport& transport) : transport_(transport) {}

    [[nodiscard]] EndpointFetchResult<ZhipuUsageSnapshot> FetchQuotaLimit(
        const ZhipuCredentials& credentials, long long nowUnixSeconds);
    void Cancel() { transport_.Cancel(); }

private:
    IHttpTransport& transport_;
};

} // namespace cqt
