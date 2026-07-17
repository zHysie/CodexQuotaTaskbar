#include "usage/CodexUsageClient.h"

#include "usage/HttpPolicy.h"
#include "usage/ResetCreditsParser.h"
#include "usage/UsageParser.h"

#include <windows.h>

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

std::optional<long long> RetryAfter(const cqt::HttpResponse& response, long long now)
{
    const auto iterator = response.headers.find(L"Retry-After");
    return iterator == response.headers.end()
        ? std::nullopt : cqt::ParseRetryAfterSeconds(iterator->second, now);
}

template <typename Snapshot>
void SetHttpError(Snapshot& snapshot, const cqt::HttpResponse& response, cqt::HttpResponseClass responseClass)
{
    snapshot.httpStatusCode = response.statusCode;
    switch (responseClass)
    {
    case cqt::HttpResponseClass::AuthenticationFailure:
        snapshot.errorCode = "HTTP_AUTH_INVALID";
        snapshot.errorMessage = L"Codex 登录已失效。";
        break;
    case cqt::HttpResponseClass::RateLimited:
        snapshot.errorCode = "HTTP_429";
        snapshot.errorMessage = L"Codex 额度服务请求过于频繁。";
        break;
    case cqt::HttpResponseClass::ServerFailure:
        snapshot.errorCode = "HTTP_SERVER_ERROR";
        snapshot.errorMessage = L"Codex 额度服务暂不可用。";
        break;
    case cqt::HttpResponseClass::RedirectRejected:
        snapshot.errorCode = "HTTP_REDIRECT_REJECTED";
        snapshot.errorMessage = L"Codex 额度接口返回了不允许的重定向。";
        break;
    case cqt::HttpResponseClass::ResponseTooLarge:
        snapshot.errorCode = "CODEX_RESPONSE_TOO_LARGE";
        snapshot.errorMessage = L"Codex 额度响应超过安全大小限制。";
        break;
    case cqt::HttpResponseClass::TransportFailure:
        snapshot.errorCode = response.errorCode.empty() ? "HTTP_NETWORK_FAILED" : response.errorCode;
        snapshot.errorMessage = response.transportError == cqt::TransportError::Timeout
            ? L"连接 Codex 额度服务超时。" : L"当前网络无法连接 Codex 额度服务。";
        break;
    default:
        snapshot.errorCode = "HTTP_STATUS_UNEXPECTED";
        snapshot.errorMessage = L"Codex 额度接口返回了不兼容的状态。";
        break;
    }
}

} // namespace

namespace cqt
{

EndpointFetchResult<UsageSnapshot> CodexUsageClient::FetchUsage(
    const CodexCredentials& credentials, long long nowUnixSeconds)
{
    EndpointFetchResult<UsageSnapshot> result;
    HttpResponse response = transport_.Get(L"chatgpt.com", L"/backend-api/wham/usage",
        credentials.accessToken.View(), credentials.accountId.View());
    const HttpResponseClass responseClass = ClassifyResponse(response);
    result.retryAfterSeconds = RetryAfter(response, nowUnixSeconds);
    if (responseClass == HttpResponseClass::Success)
    {
        result.snapshot = UsageParser::Parse(response.body, nowUnixSeconds);
        result.snapshot.httpStatusCode = response.statusCode;
    }
    else
    {
        SetHttpError(result.snapshot, response, responseClass);
    }
    ClearBody(response.body);
    return result;
}

EndpointFetchResult<ResetCreditsSnapshot> CodexUsageClient::FetchResetCredits(
    const CodexCredentials& credentials, long long nowUnixSeconds)
{
    EndpointFetchResult<ResetCreditsSnapshot> result;
    HttpResponse response = transport_.Get(L"chatgpt.com",
        L"/backend-api/wham/rate-limit-reset-credits",
        credentials.accessToken.View(), credentials.accountId.View());
    const HttpResponseClass responseClass = ClassifyResponse(response);
    result.retryAfterSeconds = RetryAfter(response, nowUnixSeconds);
    if (responseClass == HttpResponseClass::Success)
    {
        result.snapshot = ResetCreditsParser::Parse(response.body, nowUnixSeconds);
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
