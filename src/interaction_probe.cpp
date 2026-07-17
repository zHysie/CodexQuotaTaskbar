#include <windows.h>
#include <commctrl.h>

#include <chrono>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

namespace
{

constexpr wchar_t kPrototypeControllerClass[] = L"CodexQuotaTaskbar.Prototype.Controller";
constexpr wchar_t kPrototypeDisplayClass[] = L"CodexQuotaTaskbar.Prototype.Display";
constexpr wchar_t kProductionControllerClass[] = L"CodexQuotaTaskbar.Controller";
constexpr wchar_t kProductionDisplayClass[] = L"CodexQuotaTaskbar.Display";

struct ProbeState
{
    int passed = 0;
    int failed = 0;
    FILE* report = nullptr;
};

void Check(ProbeState& state, bool condition, std::wstring_view description)
{
    std::wprintf(L"[%ls] %.*ls\n", condition ? L"PASS" : L"FAIL", static_cast<int>(description.size()), description.data());
    if (state.report)
    {
        std::fwprintf(
            state.report,
            L"[%ls] %.*ls\n",
            condition ? L"PASS" : L"FAIL",
            static_cast<int>(description.size()),
            description.data());
        std::fflush(state.report);
    }
    if (condition)
    {
        ++state.passed;
    }
    else
    {
        ++state.failed;
    }
}

bool ClassEquals(HWND window, const wchar_t* expected)
{
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0
        && wcscmp(className, expected) == 0;
}

bool IsDisplayClass(HWND window)
{
    return ClassEquals(window, kProductionDisplayClass) || ClassEquals(window, kPrototypeDisplayClass);
}

HWND FindDisplayWindow()
{
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar)
    {
        return nullptr;
    }

    HWND child = nullptr;
    while ((child = FindWindowExW(taskbar, child, nullptr, nullptr)) != nullptr)
    {
        if (IsDisplayClass(child))
        {
            return child;
        }
    }
    return nullptr;
}

struct WindowSearch
{
    DWORD processId = 0;
    DWORD threadId = 0;
    const wchar_t* className = nullptr;
    HWND found = nullptr;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter)
{
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(window, &processId);
    if ((search.processId == 0 || search.processId == processId)
        && (search.threadId == 0 || search.threadId == threadId)
        && ClassEquals(window, search.className))
    {
        search.found = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindTopLevelProcessWindow(DWORD processId, DWORD threadId, const wchar_t* className)
{
    WindowSearch search{processId, threadId, className, nullptr};
    EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found && threadId != 0)
    {
        EnumThreadWindows(threadId, FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    }
    return search.found;
}

bool SendWithTimeout(HWND window, UINT message, WPARAM wParam, LPARAM lParam, DWORD_PTR& result)
{
    result = 0;
    return SendMessageTimeoutW(
               window,
               message,
               wParam,
               lParam,
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               1000,
               &result)
        != 0;
}

template <typename Predicate>
bool WaitUntil(std::chrono::milliseconds timeout, Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

} // namespace

int wmain()
{
    SetConsoleOutputCP(CP_UTF8);
    ProbeState state;
    wchar_t executablePath[MAX_PATH]{};
    const DWORD executablePathLength = GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    if (executablePathLength > 0 && executablePathLength < std::size(executablePath))
    {
        std::wstring reportPath(executablePath, executablePathLength);
        const std::wstring::size_type separator = reportPath.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
        {
            reportPath.resize(separator + 1);
            reportPath += L"TaskbarInteractionProbe-report.txt";
            _wfopen_s(&state.report, reportPath.c_str(), L"w, ccs=UTF-8");
        }
    }

    HWND controller = FindWindowW(kProductionControllerClass, nullptr);
    if (!controller) controller = FindWindowW(kPrototypeControllerClass, nullptr);
    HWND display = FindDisplayWindow();
    Check(state, controller != nullptr, L"找到原型控制窗口");
    Check(state, display != nullptr, L"找到任务栏额度子窗口");
    if (!controller || !display)
    {
        std::wprintf(L"Summary: passed=%d failed=%d\n", state.passed, state.failed);
        if (state.report)
        {
            std::fwprintf(state.report, L"Summary: passed=%d failed=%d\n", state.passed, state.failed);
            std::fclose(state.report);
        }
        return 2;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(display, &processId);
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    Check(state, taskbar != nullptr && GetParent(display) == taskbar, L"额度窗口直接附着到主任务栏");

    const LONG_PTR style = GetWindowLongPtrW(display, GWL_STYLE);
    const LONG_PTR extendedStyle = GetWindowLongPtrW(display, GWL_EXSTYLE);
    Check(state, (style & WS_CHILD) != 0 && (style & WS_POPUP) == 0, L"附着后使用 WS_CHILD 而不是 WS_POPUP");
    Check(
        state,
        (extendedStyle & WS_EX_TOOLWINDOW) != 0 && (extendedStyle & WS_EX_NOACTIVATE) != 0
            && (extendedStyle & WS_EX_APPWINDOW) == 0,
        L"窗口不进入 Alt+Tab 且默认不激活");
    Check(
        state,
        (extendedStyle & WS_EX_NOREDIRECTIONBITMAP) != 0
            && (extendedStyle & WS_EX_LAYERED) == 0,
        L"窗口使用 DirectComposition，未使用近透明 Alpha 或色键");

    RECT displayRect{};
    GetWindowRect(display, &displayRect);
    DWORD_PTR hitTest = 0;
    const LPARAM centerPoint = MAKELPARAM(
        (displayRect.left + displayRect.right) / 2,
        (displayRect.top + displayRect.bottom) / 2);
    Check(
        state,
        SendWithTimeout(display, WM_NCHITTEST, 0, centerPoint, hitTest)
            && hitTest == HTCLIENT,
        L"全透明背景仍由单一窗口接收整个矩形的命中");

    DWORD_PTR mouseActivateResult = 0;
    const bool mouseActivateResponded = SendWithTimeout(
        display,
        WM_MOUSEACTIVATE,
        reinterpret_cast<WPARAM>(taskbar),
        MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN),
        mouseActivateResult);
    Check(state, mouseActivateResponded && mouseActivateResult == MA_NOACTIVATE, L"WM_MOUSEACTIVATE 返回 MA_NOACTIVATE");

    const HWND foregroundBeforeClick = GetForegroundWindow();
    DWORD_PTR ignoredResult = 0;
    const bool leftDownResponded = SendWithTimeout(display, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10), ignoredResult);
    const bool leftUpResponded = SendWithTimeout(display, WM_LBUTTONUP, 0, MAKELPARAM(10, 10), ignoredResult);
    const HWND foregroundAfterClick = GetForegroundWindow();
    Check(
        state,
        leftDownResponded && leftUpResponded && foregroundBeforeClick == foregroundAfterClick,
        L"左键消息无操作且不改变前台窗口");

    const HWND tooltip = FindTopLevelProcessWindow(processId, 0, TOOLTIPS_CLASSW);
    Check(state, tooltip != nullptr, L"找到原生 Tooltip 窗口");
    if (tooltip)
    {
        DWORD_PTR toolCount = 0;
        const bool countResponded = SendWithTimeout(tooltip, TTM_GETTOOLCOUNT, 0, 0, toolCount);
        Check(state, countResponded && toolCount >= 1, L"Tooltip 已注册额度窗口工具区");
        Check(
            state,
            (GetWindowLongPtrW(tooltip, GWL_STYLE) & TTS_ALWAYSTIP) != 0
                && (GetWindowLongPtrW(tooltip, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
            L"Tooltip 使用始终提示并保持置顶");

        POINT originalCursor{};
        const bool cursorRead = GetCursorPos(&originalCursor) != FALSE;
        const POINT displayCenter{
            (displayRect.left + displayRect.right) / 2,
            (displayRect.top + displayRect.bottom) / 2};
        const auto staysVisibleForObservationWindow = [&]() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (!IsWindowVisible(tooltip))
                {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        };
        bool tooltipAppeared = false;
        bool remainedVisible = false;
        for (int attempt = 0; attempt < 3 && !remainedVisible; ++attempt)
        {
            // The first live refresh can legitimately repaint the owner while
            // the initial tooltip is opening. Retry after that transition;
            // subsequent one-second presentation updates must stay stable.
            SetCursorPos(displayRect.left - 8, (displayRect.top + displayRect.bottom) / 2);
            WaitUntil(std::chrono::milliseconds(1000), [&]() { return !IsWindowVisible(tooltip); });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const bool cursorMoved = SetCursorPos(displayCenter.x + attempt % 2, displayCenter.y) != FALSE;
            DWORD_PTR mouseMoveResult = 0;
            const bool mouseMoveDelivered = cursorMoved && SendWithTimeout(
                display,
                WM_MOUSEMOVE,
                0,
                MAKELPARAM(
                    (displayRect.right - displayRect.left) / 2 + attempt % 2,
                    (displayRect.bottom - displayRect.top) / 2),
                mouseMoveResult);
            if (mouseMoveDelivered) SendMessageW(tooltip, TTM_POPUP, 0, 0);
            const bool appearedThisAttempt = mouseMoveDelivered && WaitUntil(std::chrono::milliseconds(3000), [&]() {
                return IsWindowVisible(tooltip) != FALSE;
            });
            tooltipAppeared = tooltipAppeared || appearedThisAttempt;
            remainedVisible = appearedThisAttempt && staysVisibleForObservationWindow();
        }
        Check(state, tooltipAppeared, L"系统命中后的悬停状态可正常显示 Tooltip");
        Check(state, remainedVisible, L"Tooltip 连续显示 2.5 秒且未随每秒更新闪烁");
        if (cursorRead) SetCursorPos(originalCursor.x, originalCursor.y);
        WaitUntil(std::chrono::milliseconds(1000), [&]() { return !IsWindowVisible(tooltip); });
    }

    const HWND foregroundBeforeMenu = GetForegroundWindow();
    POINT originalCursor{};
    const bool cursorRead = GetCursorPos(&originalCursor) != FALSE;
    const POINT transparentCorner{displayRect.left + 2, displayRect.top + 2};
    const bool cursorMoved = SetCursorPos(transparentCorner.x, transparentCorner.y) != FALSE;
    Check(
        state,
        cursorMoved && WindowFromPoint(transparentCorner) == display,
        L"透明空白处由系统实际命中额度窗口");
    PostMessageW(
        display,
        WM_CONTEXTMENU,
        reinterpret_cast<WPARAM>(display),
        MAKELPARAM(transparentCorner.x, transparentCorner.y));
    HWND menuWindow = nullptr;
    const bool menuAppeared = WaitUntil(std::chrono::milliseconds(2000), [&]() {
        menuWindow = FindTopLevelProcessWindow(processId, 0, L"#32768");
        return menuWindow != nullptr && IsWindowVisible(menuWindow);
    });
    Check(state, menuAppeared, L"透明空白坐标弹出本程序原生菜单");
    PostMessageW(display, WM_CANCELMODE, 0, 0);
    if (menuWindow)
    {
        WaitUntil(std::chrono::milliseconds(2000), [&]() {
            return !IsWindow(menuWindow) || !IsWindowVisible(menuWindow);
        });
    }
    if (cursorRead) SetCursorPos(originalCursor.x, originalCursor.y);
    const bool foregroundNotStolen = WaitUntil(std::chrono::milliseconds(1000), [&]() {
        const HWND foreground = GetForegroundWindow();
        if (foregroundBeforeMenu == nullptr || foreground == foregroundBeforeMenu || foreground == nullptr)
            return true;
        DWORD foregroundProcessId = 0;
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
        return foregroundProcessId != processId;
    });
    Check(state, foregroundNotStolen, L"右键菜单关闭后本程序未抢占前台");

    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    const HWND oldDisplay = display;
    Check(state, taskbarCreated != 0 && PostMessageW(controller, taskbarCreated, 0, 0), L"已触发受控的 TaskbarCreated 重附着流程");
    const bool reattached = WaitUntil(std::chrono::seconds(10), [&]() {
        display = FindDisplayWindow();
        return display && display != oldDisplay && GetParent(display) == taskbar && IsWindowVisible(display);
    });
    Check(state, reattached, L"旧子窗口销毁后创建新窗口并重新附着");

    DWORD_PTR controllerResponse = 0;
    DWORD_PTR explorerResponse = 0;
    Check(state, SendWithTimeout(controller, WM_NULL, 0, 0, controllerResponse), L"重附着后原型界面线程保持响应");
    Check(state, taskbar && SendWithTimeout(taskbar, WM_NULL, 0, 0, explorerResponse), L"重附着后 Explorer 任务栏保持响应");

    std::wprintf(L"Summary: passed=%d failed=%d\n", state.passed, state.failed);
    if (state.report)
    {
        std::fwprintf(state.report, L"Summary: passed=%d failed=%d\n", state.passed, state.failed);
        std::fclose(state.report);
    }
    return state.failed == 0 ? 0 : 1;
}
