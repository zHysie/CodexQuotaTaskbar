#pragma once

#include "usage/HttpTransport.h"

#include <windows.h>
#include <winhttp.h>

#include <mutex>

namespace cqt
{

class WinHttpTransport final : public IHttpTransport
{
public:
    WinHttpTransport() = default;
    ~WinHttpTransport() override;
    WinHttpTransport(const WinHttpTransport&) = delete;
    WinHttpTransport& operator=(const WinHttpTransport&) = delete;

    [[nodiscard]] bool Initialize();
    HttpResponse Get(std::wstring_view host, std::wstring_view path,
                     std::string_view accessToken, std::string_view accountId) override;
    void Cancel() override;

private:
    void SetActiveRequest(HINTERNET request);
    [[nodiscard]] bool ClearActiveRequest(HINTERNET request);

    HINTERNET session_ = nullptr;
    std::mutex requestMutex_;
    HINTERNET activeRequest_ = nullptr;
};

} // namespace cqt
