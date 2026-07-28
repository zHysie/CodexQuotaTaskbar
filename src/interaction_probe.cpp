#include <windows.h>
#include <commctrl.h>

#include "ui/ContextMenu.h"

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
    bool visibleOnly = false;
    HWND found = nullptr;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter)
{
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(window, &processId);
    if ((search.processId == 0 || search.processId == processId)
        && (search.threadId == 0 || search.threadId == threadId)
        && ClassEquals(window, search.className)
        && (!search.visibleOnly || IsWindowVisible(window)))
    {
        search.found = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindTopLevelProcessWindow(
    DWORD processId,
    DWORD threadId,
    const wchar_t* className,
    bool visibleOnly = false)
{
    WindowSearch search{processId, threadId, className, visibleOnly, nullptr};
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

bool SendMouseClick(POINT point)
{
    if (!SetCursorPos(point.x, point.y)) return false;
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(static_cast<UINT>(std::size(input)), input, sizeof(INPUT))
        == static_cast<UINT>(std::size(input));
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

struct SingleLabelMenuState
{
    bool found = false;
    bool enabled = false;
    bool checked = false;
    std::wstring text;
};

bool CancelMenu(HWND display, DWORD processId);

bool OpenSubmenu(
    HWND display,
    DWORD processId,
    std::wstring_view submenuText,
    HWND& menuWindow,
    HMENU& submenu,
    RECT& rootItemRect)
{
    if (!CancelMenu(display, processId)) return false;
    RECT displayRect{};
    if (!GetWindowRect(display, &displayRect)) return false;
    const POINT point{displayRect.left + 2, displayRect.top + 2};
    if (!PostMessageW(
            display,
            WM_CONTEXTMENU,
            reinterpret_cast<WPARAM>(display),
            MAKELPARAM(point.x, point.y)))
    {
        return false;
    }

    menuWindow = nullptr;
    if (!WaitUntil(std::chrono::milliseconds(2000), [&]() {
            menuWindow = FindTopLevelProcessWindow(processId, 0, L"#32768", true);
            return menuWindow != nullptr;
        }))
    {
        return false;
    }

    // MN_GETHMENU is the menu-window query used by Windows menu/accessibility
    // implementations even though the Windows SDK headers do not expose it.
    constexpr UINT kMenuGetHandle = 0x01E1;
    DWORD_PTR menuResult = 0;
    if (!SendWithTimeout(menuWindow, kMenuGetHandle, 0, 0, menuResult) || menuResult == 0)
        return false;
    const HMENU rootMenu = reinterpret_cast<HMENU>(menuResult);
    const int itemCount = GetMenuItemCount(rootMenu);
    wchar_t itemText[128]{};
    submenu = nullptr;
    for (int index = 0; index < itemCount; ++index)
    {
        itemText[0] = L'\0';
        GetMenuStringW(rootMenu, static_cast<UINT>(index), itemText,
                       static_cast<int>(std::size(itemText)), MF_BYPOSITION);
        if (submenuText == itemText)
        {
            submenu = GetSubMenu(rootMenu, index);
            if (!submenu
                || !GetMenuItemRect(
                    nullptr, rootMenu, static_cast<UINT>(index), &rootItemRect))
            {
                submenu = nullptr;
            }
            break;
        }
    }
    return submenu != nullptr;
}

SingleLabelMenuState ReadSingleLabelMenuState(HMENU contentMenu)
{
    SingleLabelMenuState state;
    wchar_t text[128]{};
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_STATE | MIIM_STRING;
    item.dwTypeData = text;
    item.cch = static_cast<UINT>(std::size(text));
    state.found = GetMenuItemInfoW(
        contentMenu, cqt::CommandShowSingleQuotaLabel, FALSE, &item) != FALSE;
    if (state.found)
    {
        state.enabled = (item.fState & (MFS_DISABLED | MFS_GRAYED)) == 0;
        state.checked = (item.fState & MFS_CHECKED) != 0;
        state.text = text;
    }
    return state;
}

bool CancelMenu(HWND display, DWORD processId)
{
    HWND menuWindow = FindTopLevelProcessWindow(processId, 0, L"#32768", true);
    if (!menuWindow) return true;
    PostMessageW(menuWindow, WM_CANCELMODE, 0, 0);
    PostMessageW(display, WM_CANCELMODE, 0, 0);
    if (!WaitUntil(std::chrono::milliseconds(250), [&]() {
            return FindTopLevelProcessWindow(processId, 0, L"#32768", true) == nullptr;
        }))
    {
        RECT displayRect{};
        POINT originalCursor{};
        const bool cursorRead = GetCursorPos(&originalCursor) != FALSE;
        if (GetWindowRect(display, &displayRect))
        {
            SendMouseClick(POINT{
                (displayRect.left + displayRect.right) / 2,
                (displayRect.top + displayRect.bottom) / 2,
            });
        }
        if (cursorRead) SetCursorPos(originalCursor.x, originalCursor.y);
    }
    return WaitUntil(std::chrono::milliseconds(2000), [&]() {
        return FindTopLevelProcessWindow(processId, 0, L"#32768", true) == nullptr;
    });
}

int RunSingleLabelMenuStateProbe(bool expectedEnabled, bool expectedChecked)
{
    const HWND display = FindDisplayWindow();
    if (!display)
    {
        std::wprintf(L"[FAIL] 未找到任务栏额度子窗口\n");
        return 2;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(display, &processId);
    HWND menuWindow = nullptr;
    HMENU contentMenu = nullptr;
    RECT contentRootItemRect{};
    if (!OpenSubmenu(
            display,
            processId,
            L"显示内容",
            menuWindow,
            contentMenu,
            contentRootItemRect))
    {
        CancelMenu(display, processId);
        std::wprintf(L"[FAIL] 无法打开或读取显示内容菜单\n");
        return 2;
    }
    const SingleLabelMenuState state = ReadSingleLabelMenuState(contentMenu);
    const bool menuClosed = CancelMenu(display, processId);
    const bool passed = state.found
        && state.text == L"单项时显示标识（5h / 1W）"
        && state.enabled == expectedEnabled
        && state.checked == expectedChecked
        && menuClosed;
    std::wprintf(
        L"[%ls] text=%ls enabled=%ls checked=%ls\n",
        passed ? L"PASS" : L"FAIL",
        state.text.c_str(),
        state.enabled ? L"yes" : L"no",
        state.checked ? L"yes" : L"no");
    return passed ? 0 : 1;
}

int RunSubmenuCommandProbe(
    std::wstring_view submenuText,
    int submenuItemIndex,
    std::wstring_view commandName)
{
    const HWND display = FindDisplayWindow();
    if (!display)
    {
        std::wprintf(L"[FAIL] 未找到任务栏额度子窗口\n");
        return 2;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(display, &processId);
    RECT before{};
    GetWindowRect(display, &before);
    HWND menuWindow = nullptr;
    HMENU submenu = nullptr;
    RECT rootItemRect{};
    if (!OpenSubmenu(
            display,
            processId,
            submenuText,
            menuWindow,
            submenu,
            rootItemRect))
    {
        CancelMenu(display, processId);
        std::wprintf(L"[FAIL] 无法打开目标子菜单\n");
        return 2;
    }

    POINT originalCursor{};
    const bool cursorRead = GetCursorPos(&originalCursor) != FALSE;
    const POINT submenuPoint{
        (rootItemRect.left + rootItemRect.right) / 2,
        (rootItemRect.top + rootItemRect.bottom) / 2,
    };
    bool inputSent = SendMouseClick(submenuPoint);
    RECT commandItemRect{};
    inputSent = inputSent && WaitUntil(std::chrono::milliseconds(1000), [&]() {
        return GetMenuItemRect(
                   nullptr,
                   submenu,
                   static_cast<UINT>(submenuItemIndex),
                   &commandItemRect)
            && commandItemRect.right > commandItemRect.left
            && commandItemRect.bottom > commandItemRect.top;
    });
    if (inputSent)
    {
        const POINT commandPoint{
            (commandItemRect.left + commandItemRect.right) / 2,
            (commandItemRect.top + commandItemRect.bottom) / 2,
        };
        inputSent = SendMouseClick(commandPoint);
    }
    const bool menuClosed = inputSent && WaitUntil(std::chrono::milliseconds(2000), [&]() {
        return FindTopLevelProcessWindow(processId, 0, L"#32768", true) == nullptr;
    });
    if (cursorRead) SetCursorPos(originalCursor.x, originalCursor.y);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    RECT after{};
    const HWND afterDisplay = FindDisplayWindow();
    const bool sameWindow = afterDisplay == display && GetWindowRect(afterDisplay, &after)
        && EqualRect(&before, &after);
    const bool passed = menuClosed && sameWindow;
    std::wprintf(
        L"[%ls] command=%.*ls item=%d menu-closed=%ls same-hwnd-and-rect=%ls\n",
        passed ? L"PASS" : L"FAIL",
        static_cast<int>(commandName.size()),
        commandName.data(),
        submenuItemIndex,
        menuClosed ? L"yes" : L"no",
        sameWindow ? L"yes" : L"no");
    if (!menuClosed) CancelMenu(display, processId);
    return passed ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc == 4 && wcscmp(argv[1], L"--single-label-menu") == 0)
    {
        const bool expectedEnabled = wcscmp(argv[2], L"enabled") == 0;
        const bool expectedChecked = wcscmp(argv[3], L"checked") == 0;
        if ((!expectedEnabled && wcscmp(argv[2], L"disabled") != 0)
            || (!expectedChecked && wcscmp(argv[3], L"unchecked") != 0))
        {
            std::wprintf(L"Usage: --single-label-menu enabled|disabled checked|unchecked\n");
            return 2;
        }
        return RunSingleLabelMenuStateProbe(expectedEnabled, expectedChecked);
    }
    if (argc == 3 && wcscmp(argv[1], L"--content-command") == 0)
    {
        if (wcscmp(argv[2], L"five-hour") == 0)
            return RunSubmenuCommandProbe(L"显示内容", 0, argv[2]);
        if (wcscmp(argv[2], L"weekly") == 0)
            return RunSubmenuCommandProbe(L"显示内容", 1, argv[2]);
        if (wcscmp(argv[2], L"single-label") == 0)
            return RunSubmenuCommandProbe(L"显示内容", 2, argv[2]);
        std::wprintf(L"Usage: --content-command five-hour|weekly|single-label\n");
        return 2;
    }
    if (argc == 3 && wcscmp(argv[1], L"--layout-command") == 0)
    {
        if (wcscmp(argv[2], L"vertical") == 0)
            return RunSubmenuCommandProbe(L"显示模式", 0, argv[2]);
        if (wcscmp(argv[2], L"horizontal") == 0)
            return RunSubmenuCommandProbe(L"显示模式", 1, argv[2]);
        std::wprintf(L"Usage: --layout-command vertical|horizontal\n");
        return 2;
    }
    if (argc != 1)
    {
        std::wprintf(L"Unknown arguments.\n");
        return 2;
    }
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
