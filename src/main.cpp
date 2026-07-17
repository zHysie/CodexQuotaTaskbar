#include "taskbar/TaskbarHost.h"
#include "ui/QuotaLayout.h"
#include "ui/TaskbarRenderer.h"

#include <commctrl.h>
#include <objbase.h>
#include <windows.h>
#include <windowsx.h>

#include <cstdio>
#include <string>

namespace
{

constexpr wchar_t kControllerClass[] = L"CodexQuotaTaskbar.Prototype.Controller";
constexpr wchar_t kDisplayClass[] = L"CodexQuotaTaskbar.Prototype.Display";
constexpr wchar_t kMutexName[] = L"Local\\CodexQuotaTaskbar.Singleton";
constexpr UINT_PTR kValidationTimer = 1;
constexpr UINT kValidationIntervalMs = 2000;
constexpr ULONGLONG kReattachInitialDelayMs = 1500;
constexpr ULONGLONG kReattachRetryIntervalMs = 3000;
constexpr int kMaximumReattachAttempts = 3;
constexpr UINT kCommandRevalidate = 1001;
constexpr UINT kCommandAbout = 1002;
constexpr UINT kCommandExit = 1003;

class PrototypeApp
{
public:
    int Run(HINSTANCE instance)
    {
        instance_ = instance;

        mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
        if (!mutex_)
        {
            ShowError(L"无法建立单实例互斥。", false);
            return 1;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(mutex_);
            mutex_ = nullptr;
            return 0;
        }

        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
            && GetLastError() != ERROR_ACCESS_DENIED)
        {
            ShowError(L"无法启用 Per-Monitor DPI Aware V2。", false);
            Cleanup();
            return 1;
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(comResult))
        {
            ShowError(L"无法初始化 COM。", false);
            Cleanup();
            return 1;
        }
        comInitialized_ = true;

        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        if (!InitCommonControlsEx(&controls) || FAILED(renderer_.Initialize()) || !RegisterWindowClasses())
        {
            ShowError(L"无法初始化原型窗口或绘制组件。", false);
            Cleanup();
            return 1;
        }

        taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
        controller_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kControllerClass,
            L"CodexQuotaTaskbar Prototype Controller",
            WS_POPUP,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!controller_ || !EnsureDisplayWindow())
        {
            ShowError(L"无法创建原型窗口。", false);
            Cleanup();
            return 1;
        }

        std::wstring error;
        if (!AttachToTaskbar(error))
        {
            ShowError(error, false);
            Cleanup();
            return 2;
        }

        SetTimer(controller_, kValidationTimer, kValidationIntervalMs, nullptr);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        Cleanup();
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK ControllerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        PrototypeApp* app = reinterpret_cast<PrototypeApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<PrototypeApp*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        if (message == app->taskbarCreatedMessage_ && app->taskbarCreatedMessage_ != 0)
        {
            app->RequestReattach();
            return 0;
        }

        switch (message)
        {
        case WM_TIMER:
            if (wParam == kValidationTimer)
            {
                app->OnValidationTimer();
                return 0;
            }
            break;
        case WM_DISPLAYCHANGE:
            app->RequestReattach();
            return 0;
        case WM_SETTINGCHANGE:
            if (app->display_ && IsWindow(app->display_))
            {
                InvalidateRect(app->display_, nullptr, FALSE);
            }
            return 0;
        case WM_POWERBROADCAST:
            return TRUE;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK DisplayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        PrototypeApp* app = reinterpret_cast<PrototypeApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<PrototypeApp*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            const HRESULT drawResult = app->renderer_.Draw(window, GetDpiForWindow(window));
            if (FAILED(drawResult))
            {
                app->DrawGdiFallback(window, paint.hdc);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            app->renderer_.DiscardDeviceResources();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
            app->RequestReattach();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return 0;
        case WM_CONTEXTMENU:
            app->ShowContextMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        case WM_NCDESTROY:
            if (app->display_ == window)
            {
                app->display_ = nullptr;
                app->tooltip_ = nullptr;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool RegisterWindowClasses() const
    {
        WNDCLASSEXW controllerClass{sizeof(controllerClass)};
        controllerClass.hInstance = instance_;
        controllerClass.lpfnWndProc = ControllerWindowProc;
        controllerClass.lpszClassName = kControllerClass;
        if (!RegisterClassExW(&controllerClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        WNDCLASSEXW displayClass{sizeof(displayClass)};
        displayClass.hInstance = instance_;
        displayClass.lpfnWndProc = DisplayWindowProc;
        displayClass.lpszClassName = kDisplayClass;
        displayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&displayClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }
        return true;
    }

    static void DrawGdiFallback(HWND window, HDC deviceContext)
    {
        RECT client{};
        GetClientRect(window, &client);
        SetBkMode(deviceContext, TRANSPARENT);
        SetTextColor(deviceContext, RGB(240, 240, 240));
        const int fontHeight = -MulDiv(10, static_cast<int>(GetDpiForWindow(window)), 72);
        HFONT font = CreateFontW(
            fontHeight,
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Microsoft YaHei UI");
        HGDIOBJ previousFont = SelectObject(deviceContext, font);
        RECT firstLine = client;
        firstLine.bottom = (client.top + client.bottom) / 2;
        RECT secondLine = client;
        secondLine.top = firstLine.bottom;
        const cqt::QuotaColumnLayout columns =
            cqt::ComputeQuotaColumnLayout(static_cast<float>(client.right - client.left));
        const LONG valuePercentGap = std::max<LONG>(1, MulDiv(1, static_cast<int>(GetDpiForWindow(window)), 96));
        const auto drawQuotaLine = [&](const RECT& line, LPCWSTR label, LPCWSTR value) {
            RECT labelColumn{
                client.left + static_cast<LONG>(columns.contentLeft),
                line.top,
                client.left + static_cast<LONG>(columns.labelRight),
                line.bottom};
            RECT valueColumn{
                client.left + static_cast<LONG>(columns.labelRight),
                line.top,
                client.left + static_cast<LONG>(columns.valueRight) - valuePercentGap,
                line.bottom};
            RECT percentColumn{
                client.left + static_cast<LONG>(columns.valueRight) + valuePercentGap,
                line.top,
                client.left + static_cast<LONG>(columns.contentRight),
                line.bottom};
            constexpr UINT commonFlags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
            DrawTextW(deviceContext, label, -1, &labelColumn, commonFlags | DT_CENTER);
            DrawTextW(deviceContext, value, -1, &valueColumn, commonFlags | DT_RIGHT);
            DrawTextW(deviceContext, L"%", 1, &percentColumn, commonFlags | DT_LEFT);
        };
        drawQuotaLine(firstLine, L"5h", L"85");
        drawQuotaLine(secondLine, L"1W", L"72");
        SelectObject(deviceContext, previousFont);
        DeleteObject(font);
    }

    bool EnsureDisplayWindow()
    {
        if (display_ && IsWindow(display_))
        {
            return true;
        }

        display_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
            kDisplayClass,
            L"CodexQuotaTaskbar Static Prototype",
            WS_POPUP,
            0,
            0,
            84,
            40,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!display_)
        {
            return false;
        }

        tooltipText_ =
            L"Codex 额度（静态原型）\r\n\r\n"
            L"5 小时额度：剩余 85%\r\n"
            L"周额度：剩余 72%\r\n\r\n"
            L"此原型不读取登录凭证，也不访问网络。";
        tooltip_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            display_,
            nullptr,
            instance_,
            nullptr);
        if (!tooltip_)
        {
            DestroyWindow(display_);
            display_ = nullptr;
            return false;
        }

        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = display_;
        tool.uId = reinterpret_cast<UINT_PTR>(display_);
        tool.lpszText = tooltipText_.data();
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 600);
        SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return true;
    }

    bool AttachToTaskbar(std::wstring& error)
    {
        if (!EnsureDisplayWindow())
        {
            error = L"无法重新创建额度显示窗口。";
            return false;
        }

        ++probeCount_;
        const cqt::TaskbarProbeResult probe = host_.ProbeCompatibility();
        if (!probe.supported)
        {
            error = probe.reason;
            return false;
        }
        if (!host_.Attach(display_, probe, error))
        {
            return false;
        }

        currentTaskbar_ = probe.taskbar;
        currentProbe_ = probe;
        ++attachCount_;
        wchar_t diagnosticTitle[160]{};
        swprintf_s(
            diagnosticTitle,
            L"CodexQuotaTaskbar Static Prototype | probes=%u | attaches=%u",
            probeCount_,
            attachCount_);
        SetWindowTextW(display_, diagnosticTitle);
        InvalidateRect(display_, nullptr, FALSE);
        return true;
    }

    void RequestReattach()
    {
        if (shuttingDown_ || reattachPending_)
        {
            return;
        }
        reattachPending_ = true;
        nextReattachAttempt_ = GetTickCount64() + kReattachInitialDelayMs;
        reattachAttempts_ = 0;
        if (display_ && IsWindow(display_))
        {
            ShowWindow(display_, SW_HIDE);
        }
    }

    void DestroyDisplayWindow()
    {
        if (tooltip_ && IsWindow(tooltip_))
        {
            DestroyWindow(tooltip_);
        }
        tooltip_ = nullptr;

        if (display_ && IsWindow(display_))
        {
            DestroyWindow(display_);
        }
        display_ = nullptr;
        currentTaskbar_ = nullptr;
        currentProbe_ = {};
    }

    void OnValidationTimer()
    {
        if (shuttingDown_)
        {
            return;
        }

        if (!display_ || !IsWindow(display_) || !currentTaskbar_ || !IsWindow(currentTaskbar_)
            || GetParent(display_) != currentTaskbar_)
        {
            if (!reattachPending_)
            {
                RequestReattach();
            }
        }

        if (reattachPending_)
        {
            const ULONGLONG now = GetTickCount64();
            if (now < nextReattachAttempt_)
            {
                return;
            }

            // UI Automation must never enumerate Shell_TrayWnd while our own
            // cross-process child is still attached to it. Explorer's UIA
            // provider can synchronously query that child, forming a wait cycle
            // with this STA thread. Recreate the display as a top-level window
            // before each bounded probe attempt.
            DestroyDisplayWindow();
            ++reattachAttempts_;
            std::wstring error;
            if (AttachToTaskbar(error))
            {
                reattachPending_ = false;
                reattachAttempts_ = 0;
                return;
            }

            if (reattachAttempts_ >= kMaximumReattachAttempts)
            {
                ShowError(error.empty() ? L"Explorer 重启后无法重新附着任务栏。" : error, true);
                return;
            }
            nextReattachAttempt_ = now + kReattachRetryIntervalMs;
            return;
        }

        RECT taskbarRect{};
        RECT notificationRect{};
        std::wstring placementError;
        if (!GetWindowRect(currentProbe_.taskbar, &taskbarRect)
            || !EqualRect(&taskbarRect, &currentProbe_.taskbarRect)
            || !IsWindow(currentProbe_.notificationArea)
            || !GetWindowRect(currentProbe_.notificationArea, &notificationRect)
            || !EqualRect(&notificationRect, &currentProbe_.notificationRect)
            || GetDpiForWindow(currentProbe_.taskbar) != currentProbe_.dpi
            || !host_.ValidatePlacement(display_, currentProbe_, placementError))
        {
            RequestReattach();
        }
    }

    void ShowContextMenu(int screenX, int screenY)
    {
        if (shuttingDown_ || !display_)
        {
            return;
        }
        if (tooltip_)
        {
            SendMessageW(tooltip_, TTM_POP, 0, 0);
        }

        if (screenX == -1 && screenY == -1)
        {
            RECT rect{};
            GetWindowRect(display_, &rect);
            screenX = rect.left;
            screenY = rect.top;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        AppendMenuW(menu, MF_STRING, kCommandRevalidate, L"重新检测任务栏");
        AppendMenuW(menu, MF_STRING, kCommandAbout, L"关于原型");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandExit, L"退出");

        HWND previousForeground = GetForegroundWindow();
        SetForegroundWindow(display_);
        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            screenX,
            screenY,
            display_,
            nullptr);
        DestroyMenu(menu);
        PostMessageW(display_, WM_NULL, 0, 0);

        if (previousForeground && IsWindow(previousForeground))
        {
            SetForegroundWindow(previousForeground);
        }

        switch (command)
        {
        case kCommandRevalidate:
            RequestReattach();
            break;
        case kCommandAbout:
            MessageBoxW(
                display_,
                L"CodexQuotaTaskbar 阶段 0 静态原型\r\n\r\n"
                L"只验证任务栏嵌入、DPI、安全空白区、Tooltip、右键菜单和重附着。\r\n"
                L"不会读取 auth.json，也不会发起网络请求。",
                L"关于原型",
                MB_OK | MB_ICONINFORMATION);
            break;
        case kCommandExit:
            BeginShutdown();
            break;
        default:
            break;
        }
    }

    void ShowError(const std::wstring& message, bool exitAfter)
    {
        if (exitAfter && errorShown_)
        {
            return;
        }
        if (exitAfter)
        {
            errorShown_ = true;
        }
        MessageBoxW(
            nullptr,
            message.c_str(),
            L"CodexQuotaTaskbar 原型",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        if (exitAfter)
        {
            BeginShutdown();
        }
    }

    void BeginShutdown()
    {
        if (shuttingDown_)
        {
            return;
        }
        shuttingDown_ = true;
        if (controller_ && IsWindow(controller_))
        {
            KillTimer(controller_, kValidationTimer);
        }
        DestroyDisplayWindow();
        if (controller_ && IsWindow(controller_))
        {
            DestroyWindow(controller_);
            controller_ = nullptr;
        }
    }

    void Cleanup()
    {
        BeginShutdown();
        renderer_.DiscardDeviceResources();
        if (comInitialized_)
        {
            CoUninitialize();
            comInitialized_ = false;
        }
        if (mutex_)
        {
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }
    }

    HINSTANCE instance_ = nullptr;
    HWND controller_ = nullptr;
    HWND display_ = nullptr;
    HWND tooltip_ = nullptr;
    HWND currentTaskbar_ = nullptr;
    HANDLE mutex_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    bool comInitialized_ = false;
    bool reattachPending_ = false;
    bool shuttingDown_ = false;
    bool errorShown_ = false;
    ULONGLONG nextReattachAttempt_ = 0;
    int reattachAttempts_ = 0;
    unsigned int probeCount_ = 0;
    unsigned int attachCount_ = 0;
    std::wstring tooltipText_;
    cqt::TaskbarHost host_;
    cqt::TaskbarProbeResult currentProbe_;
    cqt::TaskbarRenderer renderer_;
};

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int)
{
    PrototypeApp app;
    return app.Run(instance);
}
