#include "usage/WinHttpTransport.h"

#include "common/StringUtils.h"
#include "usage/HttpPolicy.h"

#include <algorithm>
#include <array>
#include <string>

namespace
{

class InternetHandle
{
public:
    explicit InternetHandle(HINTERNET value = nullptr) : value_(value) {}
    ~InternetHandle() { if (value_) WinHttpCloseHandle(value_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    [[nodiscard]] HINTERNET Get() const { return value_; }
    void AbandonClosedHandle() { value_ = nullptr; }
private:
    HINTERNET value_ = nullptr;
};

void ClearSensitive(std::wstring& value)
{
    if (!value.empty())
    {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

cqt::TransportError MapLastError(DWORD error)
{
    switch (error)
    {
    case ERROR_WINHTTP_TIMEOUT: return cqt::TransportError::Timeout;
    case ERROR_WINHTTP_OPERATION_CANCELLED: return cqt::TransportError::Cancelled;
    case ERROR_WINHTTP_SECURE_FAILURE: return cqt::TransportError::SecurityPolicy;
    default: return cqt::TransportError::Network;
    }
}

} // namespace

namespace cqt
{

bool WinHttpTransport::Initialize()
{
    if (session_) return true;
    session_ = WinHttpOpen(L"CodexQuotaTaskbar/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) return false;
    if (!WinHttpSetTimeouts(session_, 5000, 10000, 10000, 15000))
    {
        WinHttpCloseHandle(session_);
        session_ = nullptr;
        return false;
    }
    return true;
}

WinHttpTransport::~WinHttpTransport()
{
    Cancel();
    if (session_) WinHttpCloseHandle(session_);
}

void WinHttpTransport::SetActiveRequest(HINTERNET request)
{
    std::scoped_lock lock(requestMutex_);
    activeRequest_ = request;
}

bool WinHttpTransport::ClearActiveRequest(HINTERNET request)
{
    std::scoped_lock lock(requestMutex_);
    if (activeRequest_ != request) return false;
    activeRequest_ = nullptr;
    return true;
}

void WinHttpTransport::Cancel()
{
    std::scoped_lock lock(requestMutex_);
    if (activeRequest_)
    {
        WinHttpCloseHandle(activeRequest_);
        activeRequest_ = nullptr;
    }
}

HttpResponse WinHttpTransport::Get(std::wstring_view host, std::wstring_view path,
                                   std::string_view accessToken, std::string_view accountId)
{
    HttpResponse response;
    if (!session_ || host != L"chatgpt.com"
        || (path != L"/backend-api/wham/usage"
            && path != L"/backend-api/wham/rate-limit-reset-credits"))
    {
        response.transportError = TransportError::SecurityPolicy;
        response.errorCode = "HTTP_DESTINATION_REJECTED";
        return response;
    }

    InternetHandle connection(WinHttpConnect(session_, L"chatgpt.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.Get())
    {
        response.transportError = MapLastError(GetLastError());
        response.errorCode = "HTTP_CONNECT_FAILED";
        return response;
    }
    std::wstring pathCopy(path);
    InternetHandle request(WinHttpOpenRequest(connection.Get(), L"GET", pathCopy.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.Get())
    {
        response.transportError = MapLastError(GetLastError());
        response.errorCode = "HTTP_REQUEST_CREATE_FAILED";
        return response;
    }
    SetActiveRequest(request.Get());
    const auto finishRequest = [&] {
        if (!ClearActiveRequest(request.Get())) request.AbandonClosedHandle();
    };
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)))
    {
        finishRequest();
        response.transportError = TransportError::SecurityPolicy;
        response.errorCode = "HTTP_REDIRECT_POLICY_FAILED";
        return response;
    }

    std::wstring tokenWide = Utf8ToWide(accessToken);
    std::wstring accountWide = Utf8ToWide(accountId);
    std::wstring headers = L"Authorization: Bearer " + tokenWide + L"\r\nAccept: application/json\r\n";
    if (!accountWide.empty()) headers += L"ChatGPT-Account-Id: " + accountWide + L"\r\n";
    const bool headersAdded = WinHttpAddRequestHeaders(request.Get(), headers.c_str(),
        static_cast<DWORD>(headers.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE;
    ClearSensitive(tokenWide);
    ClearSensitive(accountWide);
    ClearSensitive(headers);
    if (!headersAdded)
    {
        finishRequest();
        response.transportError = MapLastError(GetLastError());
        response.errorCode = "HTTP_HEADER_BUILD_FAILED";
        return response;
    }

    if (!WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request.Get(), nullptr))
    {
        const DWORD error = GetLastError();
        finishRequest();
        response.transportError = MapLastError(error);
        response.errorCode = response.transportError == TransportError::Timeout ? "HTTP_TIMEOUT" : "HTTP_REQUEST_FAILED";
        return response;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
    {
        finishRequest();
        response.transportError = TransportError::Network;
        response.errorCode = "HTTP_STATUS_MISSING";
        return response;
    }
    response.statusCode = static_cast<int>(status);

    DWORD rawHeaderBytes = 0;
    WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &rawHeaderBytes, WINHTTP_NO_HEADER_INDEX);
    if (rawHeaderBytes > kMaximumResponseHeaderBytes)
    {
        finishRequest();
        response.transportError = TransportError::ResponseHeadersTooLarge;
        response.errorCode = "CODEX_RESPONSE_HEADERS_TOO_LARGE";
        return response;
    }

    DWORD retryBytes = 0;
    WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_RETRY_AFTER,
                        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &retryBytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && retryBytes <= 4096)
    {
        std::wstring retry(retryBytes / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_RETRY_AFTER,
                                WINHTTP_HEADER_NAME_BY_INDEX, retry.data(), &retryBytes, WINHTTP_NO_HEADER_INDEX))
        {
            if (!retry.empty() && retry.back() == L'\0') retry.pop_back();
            response.headers.emplace(L"Retry-After", std::move(retry));
        }
    }

    std::array<char, 16 * 1024> buffer{};
    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(request.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read))
        {
            const DWORD error = GetLastError();
            finishRequest();
            response.transportError = MapLastError(error);
            response.errorCode = "HTTP_BODY_READ_FAILED";
            return response;
        }
        if (read == 0) break;
        if (response.body.size() + read > kMaximumResponseBodyBytes)
        {
            SecureZeroMemory(response.body.data(), response.body.size());
            response.body.clear();
            finishRequest();
            response.transportError = TransportError::ResponseBodyTooLarge;
            response.errorCode = "CODEX_RESPONSE_TOO_LARGE";
            return response;
        }
        response.body.append(buffer.data(), read);
    }
    finishRequest();
    return response;
}

} // namespace cqt
