#include "usage/ZhipuUsageClient.h"

#include "usage/HttpPolicy.h"
#include "usage/ZhipuUsageParser.h"

#include <windows.h>

#include <optional>

namespace
{

void ClearBody(std::string& body)
{
    if (!body.empty())
    {
        SecureZeroMemory(body.data(), body.size());
        body.clear();
    }
}

void SetHttpError(cqt::ZhipuUsageSnapshot& snapshot, const cqt::HttpResponse& response,
                  cqt::HttpResponseClass responseClass)
{
    snapshot.httpStatusCode = response.statusCode;
    switch (responseClass)
    {
    case cqt::HttpResponseClass::AuthenticationFailure:
        snapshot.errorCode = "ZHIPU_AUTH_INVALID";
        snapshot.errorMessage = L"智谱 API Key 已失效或无权访问额度接口。";
        break;
    case cqt::HttpResponseClass::RateLimited:
        snapshot.errorCode = "ZHIPU_HTTP_429";
        snapshot.errorMessage = L"智谱额度服务请求过于频繁。";
        break;
    case cqt::HttpResponseClass::ServerFailure:
        snapshot.errorCode = "ZHIPU_HTTP_SERVER_ERROR";
        snapshot.errorMessage = L"智谱额度服务暂不可用。";
        break;
    case cqt::HttpResponseClass::RedirectRejected:
        snapshot.errorCode = "ZHIPU_REDIRECT_REJECTED";
        snapshot.errorMessage = L"智谱额度接口返回了不允许的重定向。";
        break;
    case cqt::HttpResponseClass::ResponseTooLarge:
        snapshot.errorCode = "ZHIPU_RESPONSE_TOO_LARGE";
        snapshot.errorMessage = L"智谱额度响应超过安全大小限制。";
        break;
    case cqt::HttpResponseClass::TransportFailure:
        snapshot.errorCode = response.errorCode.empty() ? "ZHIPU_NETWORK_FAILED" : response.errorCode;
        snapshot.errorMessage = response.transportError == cqt::TransportError::Timeout
            ? L"连接智谱额度服务超时。" : L"当前网络无法连接智谱额度服务。";
        break;
    default:
        snapshot.errorCode = "ZHIPU_HTTP_STATUS_UNEXPECTED";
        snapshot.errorMessage = L"智谱额度接口返回了不兼容的状态。";
        break;
    }
}

} // namespace

namespace cqt
{

EndpointFetchResult<ZhipuUsageSnapshot> ZhipuUsageClient::FetchQuotaLimit(
    const ZhipuCredentials& credentials, long long nowUnixSeconds)
{
    EndpointFetchResult<ZhipuUsageSnapshot> result;
    HttpResponse response = transport_.Get(credentials.quotaHost, L"/api/monitor/usage/quota/limit",
        credentials.apiKey.View(), {});
    const HttpResponseClass responseClass = ClassifyResponse(response);
    if (const auto iterator = response.headers.find(L"Retry-After"); iterator != response.headers.end())
    {
        result.retryAfterSeconds = ParseRetryAfterSeconds(iterator->second, nowUnixSeconds);
    }
    if (responseClass == HttpResponseClass::Success)
    {
        result.snapshot = ZhipuUsageParser::Parse(response.body, nowUnixSeconds);
        result.snapshot.httpStatusCode = response.statusCode;
    }
    else
    {
        SetHttpError(result.snapshot, response, responseClass);
    }
    ClearBody(response.body);
    return result;
}

} // namespace cqt
