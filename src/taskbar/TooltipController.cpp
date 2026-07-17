#include "taskbar/TooltipController.h"

#include <commctrl.h>

namespace cqt
{

bool TooltipController::Create(HWND owner, HINSTANCE instance)
{
    Destroy();
    owner_ = owner;
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, instance, nullptr);
    if (!window_) return false;
    displayedText_ = L"Codex 额度\r\n\r\n正在准备首次刷新。";
    pendingText_ = displayedText_;
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = owner;
    tool.uId = reinterpret_cast<UINT_PTR>(owner);
    tool.lpszText = displayedText_.data();
    SendMessageW(window_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(window_, TTM_SETMAXTIPWIDTH, 0, 600);
    SendMessageW(window_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

void TooltipController::Destroy()
{
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    owner_ = nullptr;
    displayedText_.clear();
    pendingText_.clear();
}

void TooltipController::Update(std::wstring text)
{
    if (text != pendingText_) pendingText_ = std::move(text);
    if (!window_ || !owner_) return;
    if (IsWindowVisible(window_)) return;
    ApplyPendingText();
}

void TooltipController::ApplyPendingText()
{
    if (!window_ || !owner_ || pendingText_ == displayedText_) return;
    displayedText_ = pendingText_;
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = owner_;
    tool.uId = reinterpret_cast<UINT_PTR>(owner_);
    tool.lpszText = displayedText_.data();
    SendMessageW(window_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void TooltipController::Hide()
{
    if (window_)
    {
        SendMessageW(window_, TTM_POP, 0, 0);
        ApplyPendingText();
    }
}

} // namespace cqt
