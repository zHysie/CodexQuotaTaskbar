#pragma once

#include <windows.h>

#include <string>

namespace cqt
{

class TooltipController
{
public:
    [[nodiscard]] bool Create(HWND owner, HINSTANCE instance);
    void Destroy();
    void Update(std::wstring text);
    void Hide();
    [[nodiscard]] HWND Window() const noexcept { return window_; }

private:
    void ApplyPendingText();

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    std::wstring displayedText_;
    std::wstring pendingText_;
};

} // namespace cqt
