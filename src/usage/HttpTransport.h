#pragma once

#include <map>
#include <string>
#include <string_view>

namespace cqt
{

enum class TransportError
{
    None,
    Cancelled,
    Timeout,
    Network,
    ResponseHeadersTooLarge,
    ResponseBodyTooLarge,
    SecurityPolicy
};

struct HttpResponse
{
    int statusCode = 0;
    std::string body;
    std::map<std::wstring, std::wstring, std::less<>> headers;
    TransportError transportError = TransportError::None;
    std::string errorCode;
};

class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse Get(std::wstring_view host, std::wstring_view path,
                             std::string_view accessToken, std::string_view accountId) = 0;
    virtual void Cancel() = 0;
};

} // namespace cqt
