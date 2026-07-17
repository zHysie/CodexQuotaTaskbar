#include "taskbar/TaskbarWindow.h"

#include <commctrl.h>
#include <windowsx.h>

#include <array>

namespace
{

constexpr wchar_t kClassName[] = L"CodexQuotaTaskbar.Display";
constexpr UINT_PTR kRenderRecoveryTimer = 1;
constexpr std::array<UINT, 5> kRenderRecoveryDelays{100, 250, 500, 1000, 2000};

} // namespace

namespace cqt
{

bool RenderRecoveryState::NextDelay(UINT& delayMilliseconds) noexcept
{
    if (attempt_ >= kRenderRecoveryDelays.size()) return false;
    delayMilliseconds = kRenderRecoveryDelays[attempt_++];
    return true;
}

bool TaskbarWindow::Initialize(HINSTANCE instance, ContextHandler contextHandler)
{
    instance_ = instance;
    contextHandler_ = std::move(contextHandler);
    if (FAILED(renderer_.Initialize())) return false;
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool TaskbarWindow::Create()
{
    if (window_ && IsWindow(window_)) return true;
    Destroy();
    // A non-layered HWND retains a rectangular hit-test region. DirectComposition
    // supplies the visual surface, whose non-glyph pixels are true alpha zero.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, L"CodexQuotaTaskbar", WS_POPUP, 0, 0, 84, 40,
        nullptr, nullptr, instance_, this);
    if (!window_)
    {
        Destroy();
        return false;
    }
    if (!tooltip_.Create(window_, instance_))
    {
        Destroy();
        return false;
    }
    return true;
}

void TaskbarWindow::Destroy()
{
    if (window_ && IsWindow(window_)) KillTimer(window_, kRenderRecoveryTimer);
    tooltip_.Destroy();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    renderer_.DiscardDeviceResources();
    renderRecovery_.Reset();
}

void TaskbarWindow::Update(TaskbarRenderModel model, std::wstring tooltip)
{
    const bool modelChanged = model != model_;
    if (modelChanged) model_ = std::move(model);
    tooltip_.Update(std::move(tooltip));
    if (window_ && modelChanged)
    {
        renderRecovery_.Reset();
        InvalidateRect(window_, nullptr, FALSE);
    }
}

LRESULT CALLBACK TaskbarWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    TaskbarWindow* self = reinterpret_cast<TaskbarWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<TaskbarWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TaskbarWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (FAILED(renderer_.Draw(window, GetDpiForWindow(window), model_)))
        {
            // Keep the GDI path as a best-effort frame while the DirectComposition
            // device is being recreated. A bounded timer prevents a tight WM_PAINT
            // loop when the graphics stack remains unavailable.
            renderer_.DrawGdiFallback(window, paint.hdc, GetDpiForWindow(window), model_);
            UINT delay = 0;
            if (renderRecovery_.NextDelay(delay))
                SetTimer(window, kRenderRecoveryTimer, delay, nullptr);
        }
        else
        {
            KillTimer(window, kRenderRecoveryTimer);
            renderRecovery_.Reset();
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        KillTimer(window, kRenderRecoveryTimer);
        renderRecovery_.Reset();
        renderer_.DiscardDeviceResources();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        KillTimer(window, kRenderRecoveryTimer);
        renderRecovery_.Reset();
        renderer_.InvalidateTheme();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wParam == kRenderRecoveryTimer)
        {
            KillTimer(window, kRenderRecoveryTimer);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: return 0;
    case WM_CONTEXTMENU:
        tooltip_.Hide();
        if (contextHandler_) contextHandler_(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return TRUE;
    case WM_NCDESTROY:
        KillTimer(window, kRenderRecoveryTimer);
        if (window_ == window) window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace cqt
