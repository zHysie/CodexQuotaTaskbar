#include "taskbar/TaskbarHost.h"
#include "taskbar/TaskbarCollision.h"

#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <winternl.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace
{

using Microsoft::WRL::ComPtr;

cqt::TaskbarHost* gMonitoredTaskbarHost = nullptr;

LONG RectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

LONG RectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

bool Intersects(const RECT& left, const RECT& right)
{
    RECT intersection{};
    return IntersectRect(&intersection, &left, &right) != FALSE;
}

cqt::RectangleEdges ToRectangleEdges(const RECT& rect)
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

bool NativeRectsApproximatelyEqual(const RECT& left, const RECT& right, LONG tolerance)
{
    return cqt::RectanglesApproximatelyEqual(
        ToRectangleEdges(left), ToRectangleEdges(right), tolerance);
}

bool IsWindowCloaked(HWND window)
{
    using DwmGetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    static const auto getWindowAttribute = []() -> DwmGetWindowAttributeFunction
    {
        HMODULE module = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module
            ? reinterpret_cast<DwmGetWindowAttributeFunction>(
                GetProcAddress(module, "DwmGetWindowAttribute"))
            : nullptr;
    }();

    constexpr DWORD dwmWindowAttributeCloaked = 14;
    DWORD cloaked = 0;
    return getWindowAttribute
        && SUCCEEDED(getWindowAttribute(window, dwmWindowAttributeCloaked, &cloaked, sizeof(cloaked)))
        && cloaked != 0;
}

std::wstring FormatRect(const RECT& rect)
{
    std::wostringstream output;
    output << L"[" << rect.left << L"," << rect.top << L"," << rect.right << L"," << rect.bottom
           << L"] " << RectWidth(rect) << L"x" << RectHeight(rect);
    return output.str();
}

std::wstring FormatHandle(HWND window)
{
    std::wostringstream output;
    output << L"0x" << std::hex << std::uppercase
           << static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(window));
    return output.str();
}

std::wstring WindowClassName(HWND window)
{
    wchar_t className[256]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    return className;
}

std::vector<HWND> DirectChildren(HWND parent)
{
    std::vector<HWND> children;
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM parameter) -> BOOL
        {
            auto* result = reinterpret_cast<std::vector<HWND>*>(parameter);
            result->push_back(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&children));

    children.erase(
        std::remove_if(
            children.begin(),
            children.end(),
            [parent](HWND child) { return GetParent(child) != parent; }),
        children.end());
    return children;
}

struct ExternalWindowContext
{
    RECT taskbarRect{};
    RECT baseSafeRect{};
    RECT monitorRect{};
    DWORD taskbarProcessId = 0;
    UINT dpi = 96;
    std::vector<cqt::ExternalWindowObstacle>* obstacles = nullptr;
};

BOOL CALLBACK CollectExternalWindow(HWND window, LPARAM parameter)
{
    auto* context = reinterpret_cast<ExternalWindowContext*>(parameter);
    if (!context || !context->obstacles) return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);

    RECT rect{};
    if (!GetWindowRect(window, &rect)) return TRUE;
    const LONG minimumExtent = std::max<LONG>(8, MulDiv(12, context->dpi, 96));
    const LONG maximumWidth = std::max<LONG>(
        RectWidth(context->baseSafeRect), MulDiv(640, context->dpi, 96));

    const std::wstring className = WindowClassName(window);
    const cqt::ExternalWindowCandidate candidate{
        ToRectangleEdges(rect),
        ToRectangleEdges(context->taskbarRect),
        ToRectangleEdges(context->baseSafeRect),
        ToRectangleEdges(context->monitorRect),
        GetAncestor(window, GA_ROOT) == window,
        IsWindowVisible(window) != FALSE,
        IsWindowCloaked(window),
        processId == 0 || processId == context->taskbarProcessId
            || processId == GetCurrentProcessId(),
        cqt::IsExcludedExternalWindowClass(className),
        minimumExtent,
        maximumWidth,
        std::max<LONG>(2, MulDiv(2, context->dpi, 96))};
    if (!cqt::IsExternalObstacleCandidate(candidate))
        return TRUE;

    context->obstacles->push_back({rect, processId, className});
    return TRUE;
}

bool ApplyExternalObstacles(cqt::TaskbarProbeResult& result)
{
    result.externalObstacles.clear();
    DWORD taskbarProcessId = 0;
    GetWindowThreadProcessId(result.taskbar, &taskbarProcessId);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(result.taskbar, MONITOR_DEFAULTTONULL);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
    {
        SetRectEmpty(&result.safeRect);
        return false;
    }
    ExternalWindowContext context{
        result.taskbarRect,
        result.baseSafeRect,
        monitorInfo.rcMonitor,
        taskbarProcessId,
        result.dpi,
        &result.externalObstacles};
    EnumWindows(CollectExternalWindow, reinterpret_cast<LPARAM>(&context));

    std::vector<cqt::HorizontalInterval> intervals;
    intervals.reserve(result.externalObstacles.size());
    for (const auto& obstacle : result.externalObstacles)
        intervals.push_back({obstacle.rect.left, obstacle.rect.right});

    const LONG margin = MulDiv(8, result.dpi, 96);
    const LONG minimumWidth = MulDiv(68, result.dpi, 96);
    const auto selected = cqt::SelectRightmostFreeInterval(
        {result.baseSafeRect.left, result.baseSafeRect.right},
        std::move(intervals), margin, minimumWidth);
    if (!selected)
    {
        SetRectEmpty(&result.safeRect);
        return false;
    }
    result.safeRect = {
        selected->left,
        result.baseSafeRect.top,
        selected->right,
        result.baseSafeRect.bottom};
    return true;
}

bool ShellSignaturesApproximatelyEqual(
    const cqt::TaskbarShellSignature& left,
    const cqt::TaskbarShellSignature& right,
    LONG tolerance)
{
    if (left.taskbar != right.taskbar
        || left.rebar != right.rebar
        || left.taskSwitch != right.taskSwitch
        || left.notificationArea != right.notificationArea
        || left.dpi != right.dpi)
        return false;

    // These child HWNDs are structural, but their rectangles are soft safety
    // boundaries: task buttons, tray content and TrafficMonitor can resize them
    // without Explorer rebuilding the taskbar.
    return NativeRectsApproximatelyEqual(left.taskbarRect, right.taskbarRect, tolerance);
}

void AppendWindowTree(HWND parent, int depth, std::wostringstream& output)
{
    for (HWND child : DirectChildren(parent))
    {
        RECT rect{};
        GetWindowRect(child, &rect);
        DWORD processId = 0;
        GetWindowThreadProcessId(child, &processId);
        const std::wstring className = WindowClassName(child);
        output << std::wstring(static_cast<std::size_t>(depth) * 2, L' ')
               << L"- " << className
               << L" hwnd=" << FormatHandle(child)
               << L" visible=" << (IsWindowVisible(child) ? L"yes" : L"no")
               << L" pid=" << processId
               << L" dpi=" << GetDpiForWindow(child)
               << L" rect=" << FormatRect(rect)
               << L" awareness=" << cqt::TaskbarHost::DpiAwarenessName(GetWindowDpiAwarenessContext(child))
               ;
        if (className == L"CodexQuotaTaskbar.Prototype.Display")
        {
            wchar_t title[256]{};
            GetWindowTextW(child, title, static_cast<int>(std::size(title)));
            output << L" title=\"" << title << L"\"";
        }
        output << L"\n";
        AppendWindowTree(child, depth + 1, output);
    }
}

struct MonitorRecord
{
    RECT monitor{};
    RECT work{};
    bool primary = false;
    std::wstring device;
};

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
    auto* monitors = reinterpret_cast<std::vector<MonitorRecord>*>(parameter);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info))
    {
        monitors->push_back({info.rcMonitor, info.rcWork, (info.dwFlags & MONITORINFOF_PRIMARY) != 0, info.szDevice});
    }
    return TRUE;
}

std::wstring WindowsVersion()
{
    using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion)
    {
        return L"unknown";
    }

    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0)
    {
        return L"unknown";
    }

    std::wostringstream output;
    output << info.dwMajorVersion << L"." << info.dwMinorVersion << L"." << info.dwBuildNumber;

    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0,
            KEY_QUERY_VALUE,
            &key) == ERROR_SUCCESS)
    {
        DWORD ubr = 0;
        DWORD size = sizeof(ubr);
        if (RegQueryValueExW(key, L"UBR", nullptr, nullptr, reinterpret_cast<BYTE*>(&ubr), &size) == ERROR_SUCCESS)
        {
            output << L"." << ubr;
        }
        RegCloseKey(key);
    }
    return output.str();
}

} // namespace

namespace cqt
{

TaskbarHost::~TaskbarHost()
{
    StopEventMonitoring();
}

void CALLBACK TaskbarHost::WinEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG objectId,
    LONG childId,
    DWORD,
    DWORD)
{
    if (gMonitoredTaskbarHost)
        gMonitoredTaskbarHost->HandleWinEvent(event, window, objectId, childId);
}

void TaskbarHost::HandleWinEvent(DWORD event, HWND window, LONG objectId, LONG) const
{
    if (!eventNotificationWindow_ || !eventNotificationMessage_ || !window) return;
    if (objectId != OBJID_WINDOW && objectId != OBJID_CLIENT && objectId != OBJID_NATIVEOM) return;

    const bool knownWindow = window == monitoredTaskbar_ || window == monitoredNotificationArea_;
    const bool currentDescendant = IsWindow(monitoredTaskbar_)
        && (IsChild(monitoredTaskbar_, window) || GetAncestor(window, GA_ROOT) == monitoredTaskbar_);
    const std::wstring className = IsWindow(window) ? WindowClassName(window) : std::wstring{};
    const bool keyTaskbarClass = className == L"MSTaskSwWClass"
        || className == L"ReBarWindow32"
        || className == L"TrayNotifyWnd"
        || className == L"Windows.UI.Composition.DesktopWindowContentBridge";
    if (!knownWindow && !(currentDescendant && keyTaskbarClass)) return;

    PostMessageW(
        eventNotificationWindow_,
        eventNotificationMessage_,
        static_cast<WPARAM>(event),
        reinterpret_cast<LPARAM>(window));
}

bool TaskbarHost::StartEventMonitoring(
    HWND notificationWindow,
    UINT notificationMessage,
    const TaskbarProbeResult& probe)
{
    StopEventMonitoring();
    if (!IsWindow(notificationWindow) || notificationMessage < WM_APP
        || !IsWindow(probe.taskbar) || !IsWindow(probe.notificationArea))
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(probe.taskbar, &processId);
    if (processId == 0) return false;

    eventNotificationWindow_ = notificationWindow;
    eventNotificationMessage_ = notificationMessage;
    monitoredTaskbar_ = probe.taskbar;
    monitoredNotificationArea_ = probe.notificationArea;
    gMonitoredTaskbarHost = this;

    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    for (const DWORD event : {
             EVENT_OBJECT_DESTROY,
             EVENT_OBJECT_SHOW,
             EVENT_OBJECT_HIDE,
             EVENT_OBJECT_LOCATIONCHANGE})
    {
        HWINEVENTHOOK hook = SetWinEventHook(
            event, event, nullptr, WinEventProc, processId, 0, flags);
        if (!hook)
        {
            StopEventMonitoring();
            return false;
        }
        eventHooks_.push_back(hook);
    }
    return true;
}

void TaskbarHost::StopEventMonitoring()
{
    if (gMonitoredTaskbarHost == this) gMonitoredTaskbarHost = nullptr;
    for (const HWINEVENTHOOK hook : eventHooks_)
        if (hook) UnhookWinEvent(hook);
    eventHooks_.clear();
    eventNotificationWindow_ = nullptr;
    monitoredTaskbar_ = nullptr;
    monitoredNotificationArea_ = nullptr;
    eventNotificationMessage_ = 0;
}

HWND TaskbarHost::FindDescendantByClass(HWND parent, const wchar_t* className)
{
    struct Search
    {
        const wchar_t* className = nullptr;
        HWND result = nullptr;
    } search{className, nullptr};

    EnumChildWindows(
        parent,
        [](HWND child, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<Search*>(parameter);
            wchar_t currentClass[256]{};
            GetClassNameW(child, currentClass, static_cast<int>(std::size(currentClass)));
            if (wcscmp(currentClass, search->className) == 0)
            {
                search->result = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

bool TaskbarHost::CaptureShellSignature(HWND taskbar, TaskbarShellSignature& signature)
{
    signature = {};
    if (!IsWindow(taskbar) || !GetWindowRect(taskbar, &signature.taskbarRect))
        return false;

    signature.taskbar = taskbar;
    signature.dpi = GetDpiForWindow(taskbar);
    if (signature.dpi == 0) signature.dpi = 96;
    signature.rebar = FindDescendantByClass(taskbar, L"ReBarWindow32");
    signature.taskSwitch = FindDescendantByClass(taskbar, L"MSTaskSwWClass");
    signature.notificationArea = FindDescendantByClass(taskbar, L"TrayNotifyWnd");

    const auto captureOptionalRect = [](HWND& window, RECT& rect)
    {
        if (!window || !IsWindow(window) || !GetWindowRect(window, &rect))
        {
            window = nullptr;
            SetRectEmpty(&rect);
        }
    };
    captureOptionalRect(signature.rebar, signature.rebarRect);
    captureOptionalRect(signature.taskSwitch, signature.taskSwitchRect);
    captureOptionalRect(signature.notificationArea, signature.notificationRect);
    return signature.notificationArea != nullptr;
}

bool TaskbarHost::IsInteractiveControlType(int controlType)
{
    switch (controlType)
    {
    case UIA_ButtonControlTypeId:
    case UIA_SplitButtonControlTypeId:
    case UIA_MenuItemControlTypeId:
    case UIA_TabItemControlTypeId:
    case UIA_HyperlinkControlTypeId:
    case UIA_CheckBoxControlTypeId:
    case UIA_RadioButtonControlTypeId:
    case UIA_EditControlTypeId:
        return true;
    default:
        return false;
    }
}

bool TaskbarHost::RectInside(const RECT& inner, const RECT& outer, LONG tolerance)
{
    return inner.left >= outer.left - tolerance && inner.top >= outer.top - tolerance
        && inner.right <= outer.right + tolerance && inner.bottom <= outer.bottom + tolerance;
}

TaskbarProbeResult TaskbarHost::ProbeCompatibility() const
{
    TaskbarProbeResult result;
    result.taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!result.taskbar || !IsWindow(result.taskbar))
    {
        result.failure = TaskbarProbeFailure::TaskbarUnavailable;
        result.reason = L"未找到当前主任务栏 Shell_TrayWnd。";
        return result;
    }

    HMONITOR monitor = MonitorFromWindow(result.taskbar, MONITOR_DEFAULTTONULL);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo) || (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) == 0)
    {
        result.failure = TaskbarProbeFailure::PrimaryMonitorUnavailable;
        result.reason = L"Shell_TrayWnd 不在当前主显示器上。";
        return result;
    }

    if (!GetWindowRect(result.taskbar, &result.taskbarRect))
    {
        result.failure = TaskbarProbeFailure::TaskbarGeometryUnavailable;
        result.reason = L"无法读取主任务栏位置。";
        return result;
    }

    const LONG taskbarWidth = RectWidth(result.taskbarRect);
    const LONG taskbarHeight = RectHeight(result.taskbarRect);
    if (taskbarWidth <= taskbarHeight || result.taskbarRect.bottom < monitorInfo.rcMonitor.bottom - 2)
    {
        result.failure = TaskbarProbeFailure::UnsupportedTaskbarLayout;
        result.reason = L"原型只支持 Windows 11 底部主任务栏。";
        return result;
    }

    result.dpi = GetDpiForWindow(result.taskbar);
    if (result.dpi == 0)
    {
        result.dpi = 96;
    }

    if (!CaptureShellSignature(result.taskbar, result.shellSignature))
    {
        result.failure = TaskbarProbeFailure::ShellStructureUnavailable;
        result.reason = L"无法读取任务栏轻量结构签名。";
        return result;
    }
    result.notificationArea = result.shellSignature.notificationArea;
    result.notificationRect = result.shellSignature.notificationRect;
    if (!result.notificationArea
        || !RectInside(result.notificationRect, result.taskbarRect, 2))
    {
        result.failure = TaskbarProbeFailure::NotificationAreaUnavailable;
        result.reason = L"无法可靠识别任务栏通知区域。";
        return result;
    }

    ComPtr<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation8,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation));
    if (FAILED(hr))
    {
        result.failure = TaskbarProbeFailure::AutomationUnavailable;
        result.reason = L"无法初始化 UI Automation，不能验证任务按钮边界。";
        return result;
    }

    ComPtr<IUIAutomation2> automationWithTimeouts;
    if (SUCCEEDED(automation.As(&automationWithTimeouts)) && automationWithTimeouts)
    {
        automationWithTimeouts->put_ConnectionTimeout(2000);
        automationWithTimeouts->put_TransactionTimeout(2000);
    }

    ComPtr<IUIAutomationElement> root;
    hr = automation->ElementFromHandle(result.taskbar, &root);
    if (FAILED(hr) || !root)
    {
        result.failure = TaskbarProbeFailure::AutomationTreeUnavailable;
        result.reason = L"无法读取任务栏 UI Automation 树。";
        return result;
    }

    ComPtr<IUIAutomationCondition> condition;
    hr = automation->CreateTrueCondition(&condition);
    if (FAILED(hr))
    {
        result.failure = TaskbarProbeFailure::AutomationQueryUnavailable;
        result.reason = L"无法创建任务栏 UI Automation 查询。";
        return result;
    }

    ComPtr<IUIAutomationElementArray> elements;
    hr = root->FindAll(TreeScope_Descendants, condition.Get(), &elements);
    if (FAILED(hr) || !elements)
    {
        result.failure = TaskbarProbeFailure::AutomationQueryUnavailable;
        result.reason = L"无法枚举任务栏 UI Automation 元素。";
        return result;
    }

    int length = 0;
    elements->get_Length(&length);
    result.rightmostOccupied = result.taskbarRect.left;
    const LONG maximumElementWidth = std::max<LONG>(taskbarHeight * 8, MulDiv(320, result.dpi, 96));

    for (int index = 0; index < length; ++index)
    {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(index, &element)) || !element)
        {
            continue;
        }

        BOOL offscreen = TRUE;
        CONTROLTYPEID controlType = 0;
        RECT rect{};
        if (FAILED(element->get_CurrentIsOffscreen(&offscreen)) || offscreen
            || FAILED(element->get_CurrentControlType(&controlType))
            || !IsInteractiveControlType(controlType)
            || FAILED(element->get_CurrentBoundingRectangle(&rect)))
        {
            continue;
        }

        const LONG width = RectWidth(rect);
        const LONG height = RectHeight(rect);
        if (width <= 0 || height <= 0 || width > maximumElementWidth || height > taskbarHeight * 2
            || !Intersects(rect, result.taskbarRect)
            || rect.left < result.taskbarRect.left
            || rect.right > result.notificationRect.left)
        {
            continue;
        }

        result.occupiedElements.push_back({rect, controlType});
        result.rightmostOccupied = std::max(result.rightmostOccupied, rect.right);
    }

    if (result.occupiedElements.empty() || result.rightmostOccupied <= result.taskbarRect.left)
    {
        result.failure = TaskbarProbeFailure::TaskButtonBoundaryUnavailable;
        result.reason = L"未能从 UI Automation 可靠识别任务按钮边界。";
        return result;
    }

    const LONG margin = MulDiv(8, result.dpi, 96);
    const LONG minimumWidth = MulDiv(68, result.dpi, 96);
    result.baseSafeRect = {
        result.rightmostOccupied + margin,
        result.taskbarRect.top,
        result.notificationRect.left - margin,
        result.taskbarRect.bottom};

    if (RectWidth(result.baseSafeRect) < minimumWidth)
    {
        result.failure = TaskbarProbeFailure::InsufficientSafeSpace;
        result.reason = L"任务栏任务按钮与通知区域之间没有足够的连续安全空白。";
        return result;
    }
    if (!ApplyExternalObstacles(result))
    {
        result.failure = TaskbarProbeFailure::InsufficientExternalSafeSpace;
        result.reason = L"第三方任务栏悬浮窗口之后没有足够的连续安全空白。";
        return result;
    }

    result.supported = true;
    return result;
}

bool TaskbarHost::Attach(HWND childWindow, const TaskbarProbeResult& probe, std::wstring& error,
                         LONG desiredWidthDip) const
{
    if (!probe.supported || !IsWindow(childWindow) || !IsWindow(probe.taskbar))
    {
        error = probe.reason.empty() ? L"任务栏附着前置验证失败。" : probe.reason;
        return false;
    }

    DPI_AWARENESS_CONTEXT parentContext = GetWindowDpiAwarenessContext(probe.taskbar);

    LONG_PTR style = GetWindowLongPtrW(childWindow, GWL_STYLE);
    style &= ~static_cast<LONG_PTR>(WS_POPUP);
    style |= WS_CHILD;
    SetWindowLongPtrW(childWindow, GWL_STYLE, style);

    SetLastError(ERROR_SUCCESS);
    HWND previousParent = SetParent(childWindow, probe.taskbar);
    if (!previousParent && GetLastError() != ERROR_SUCCESS)
    {
        error = L"SetParent 无法把额度窗口附着到主任务栏。";
        return false;
    }

    const LONG desiredWidth = MulDiv(desiredWidthDip, probe.dpi, 96);
    const LONG width = std::min(desiredWidth, RectWidth(probe.safeRect));
    const LONG verticalMargin = MulDiv(3, probe.dpi, 96);
    const LONG height = std::max<LONG>(1, RectHeight(probe.taskbarRect) - verticalMargin * 2);
    RECT windowRect{
        probe.safeRect.right - width,
        probe.taskbarRect.top + (RectHeight(probe.taskbarRect) - height) / 2,
        probe.safeRect.right,
        probe.taskbarRect.top + (RectHeight(probe.taskbarRect) - height) / 2 + height};

    POINT points[2]{{windowRect.left, windowRect.top}, {windowRect.right, windowRect.bottom}};
    if (MapWindowPoints(HWND_DESKTOP, probe.taskbar, points, 2) == 0 && GetLastError() != ERROR_SUCCESS)
    {
        error = L"无法把屏幕坐标转换为任务栏客户区坐标。";
        return false;
    }

    if (!SetWindowPos(
            childWindow,
            HWND_TOP,
            points[0].x,
            points[0].y,
            points[1].x - points[0].x,
            points[1].y - points[0].y,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED))
    {
        error = L"无法在任务栏安全空白区定位额度窗口。";
        return false;
    }

    DPI_AWARENESS_CONTEXT childContext = GetWindowDpiAwarenessContext(childWindow);
    if (GetAwarenessFromDpiAwarenessContext(parentContext) != GetAwarenessFromDpiAwarenessContext(childContext)
        || GetDpiForWindow(childWindow) != probe.dpi)
    {
        ShowWindow(childWindow, SW_HIDE);
        error = L"附着后父子窗口 DPI 感知上下文或 DPI 不一致。";
        return false;
    }

    return ValidatePlacement(childWindow, probe, error);
}

bool TaskbarHost::ValidatePlacement(
    HWND childWindow,
    const TaskbarProbeResult& probe,
    std::wstring& error) const
{
    if (!IsWindow(childWindow) || !IsWindow(probe.taskbar) || GetParent(childWindow) != probe.taskbar)
    {
        error = L"额度窗口已失去主任务栏父窗口。";
        return false;
    }

    RECT childRect{};
    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    if (!GetWindowRect(childWindow, &childRect)
        || !RectInside(childRect, probe.taskbarRect, tolerance)
        || !RectInside(childRect, probe.baseSafeRect, tolerance))
    {
        error = L"额度窗口不再位于已验证的任务栏安全空白区。";
        return false;
    }

    if (childRect.right > probe.notificationRect.left + tolerance)
    {
        error = L"额度窗口与任务栏通知区域发生重叠。";
        return false;
    }

    std::vector<HorizontalInterval> occupied;
    occupied.reserve(probe.externalObstacles.size());
    for (const auto& obstacle : probe.externalObstacles)
        occupied.push_back({obstacle.rect.left, obstacle.rect.right});
    const LONG margin = MulDiv(8, probe.dpi, 96);
    if (!IsIntervalFree(
            {childRect.left, childRect.right},
            {probe.baseSafeRect.left - tolerance, probe.baseSafeRect.right + tolerance},
            occupied,
            std::max(0L, margin - tolerance)))
    {
        error = L"额度窗口与第三方任务栏窗口的安全间距发生碰撞。";
        return false;
    }

    return true;
}

bool TaskbarHost::RefreshExternalLayout(TaskbarProbeResult& probe) const
{
    if (!probe.supported || !IsWindow(probe.taskbar))
    {
        probe.reason = L"主任务栏宿主已经失效。";
        return false;
    }

    TaskbarShellSignature currentSignature;
    if (!CaptureShellSignature(probe.taskbar, currentSignature))
    {
        probe.reason = L"无法读取任务栏轻量结构签名。";
        return false;
    }
    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    if (!ShellSignaturesApproximatelyEqual(probe.shellSignature, currentSignature, tolerance))
    {
        probe.reason = L"任务栏结构或 DPI 已发生变化，需要重新探测。";
        return false;
    }

    if (!RectInside(currentSignature.notificationRect, currentSignature.taskbarRect, tolerance))
    {
        probe.reason = L"任务栏通知区域已离开主任务栏边界。";
        return false;
    }
    const auto bandRight = [](const TaskbarShellSignature& signature) -> LONG
    {
        const RECT& band = signature.rebar
            ? signature.rebarRect
            : signature.taskSwitchRect;
        return band.right;
    };
    // Windows 11 centers the task-button band by default, so its width can
    // change twice as much as its occupied right edge. Track the actual right
    // edge delta; this also remains correct for a left-aligned taskbar.
    const long long adjustedRightmost = std::clamp<long long>(
        static_cast<long long>(probe.rightmostOccupied)
            + bandRight(currentSignature) - bandRight(probe.shellSignature),
        currentSignature.taskbarRect.left,
        currentSignature.notificationRect.left);

    probe.notificationArea = currentSignature.notificationArea;
    probe.notificationRect = currentSignature.notificationRect;
    probe.shellSignature.notificationRect = currentSignature.notificationRect;
    const LONG margin = MulDiv(8, probe.dpi, 96);
    const LONG minimumWidth = MulDiv(68, probe.dpi, 96);
    probe.baseSafeRect.left = static_cast<LONG>(adjustedRightmost) + margin;
    probe.baseSafeRect.right = probe.notificationRect.left - margin;
    if (RectWidth(probe.baseSafeRect) < minimumWidth)
    {
        SetRectEmpty(&probe.safeRect);
        probe.reason = L"任务栏任务按钮与通知区域之间没有足够的连续安全空白。";
        return false;
    }

    if (!ApplyExternalObstacles(probe))
    {
        probe.reason = L"第三方任务栏悬浮窗口之后没有足够的连续安全空白。";
        return false;
    }
    probe.reason.clear();
    return true;
}

bool TaskbarHost::HasLightweightStructureChanged(const TaskbarProbeResult& probe) const
{
    if (!probe.supported || !IsWindow(probe.taskbar)) return true;
    TaskbarShellSignature currentSignature;
    if (!CaptureShellSignature(probe.taskbar, currentSignature)) return true;
    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    return !ShellSignaturesApproximatelyEqual(probe.shellSignature, currentSignature, tolerance);
}

bool TaskbarHost::IsForegroundFullscreen(const TaskbarProbeResult& probe) const
{
    if (!IsWindow(probe.taskbar)) return false;
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    foreground = GetAncestor(foreground, GA_ROOT);
    if (!foreground || foreground == probe.taskbar || !IsWindowVisible(foreground)
        || IsWindowCloaked(foreground))
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (processId == 0 || processId == GetCurrentProcessId()) return false;

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(probe.taskbar, MONITOR_DEFAULTTONULL);
    RECT foregroundRect{};
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)
        || !GetWindowRect(foreground, &foregroundRect))
        return false;

    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    return RectangleCovers(
        ToRectangleEdges(foregroundRect),
        ToRectangleEdges(monitorInfo.rcMonitor),
        tolerance);
}

bool TaskbarHost::RepositionWithinSafeRect(
    HWND childWindow,
    const TaskbarProbeResult& probe,
    std::wstring& error,
    LONG desiredWidthDip) const
{
    if (!probe.supported || !IsWindow(childWindow) || !IsWindow(probe.taskbar)
        || GetParent(childWindow) != probe.taskbar)
    {
        error = L"额度窗口已失去主任务栏父窗口。";
        return false;
    }

    RECT childRect{};
    if (!GetWindowRect(childWindow, &childRect))
    {
        error = L"无法读取额度窗口位置。";
        return false;
    }

    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    std::wstring validationError;
    if (ValidatePlacement(childWindow, probe, validationError))
    {
        error.clear();
        return true;
    }

    const LONG minimumWidth = MulDiv(68, probe.dpi, 96);
    const LONG desiredWidth = std::max<LONG>(1, MulDiv(desiredWidthDip, probe.dpi, 96));
    const LONG currentWidth = RectWidth(childRect) > 0 ? RectWidth(childRect) : desiredWidth;
    std::vector<HorizontalInterval> occupied;
    occupied.reserve(probe.externalObstacles.size());
    for (const auto& obstacle : probe.externalObstacles)
        occupied.push_back({obstacle.rect.left, obstacle.rect.right});
    const auto horizontal = SelectNearestFreeInterval(
        {probe.baseSafeRect.left, probe.baseSafeRect.right},
        std::move(occupied),
        MulDiv(8, probe.dpi, 96),
        minimumWidth,
        {childRect.left, childRect.left + currentWidth});
    if (!horizontal)
    {
        error = L"任务栏空间不足，无法安全放置额度窗口。";
        return false;
    }

    LONG height = RectHeight(childRect);
    if (height <= 0)
    {
        const LONG verticalMargin = MulDiv(3, probe.dpi, 96);
        height = std::max<LONG>(1, RectHeight(probe.taskbarRect) - verticalMargin * 2);
    }
    height = std::min(height, RectHeight(probe.baseSafeRect));
    if (height <= 0)
    {
        error = L"任务栏安全区域高度无效。";
        return false;
    }

    LONG width = horizontal->right - horizontal->left;
    if (RectWidth(childRect) <= 0)
        width = std::min(desiredWidth, RectWidth(probe.baseSafeRect));
    if (width < minimumWidth)
    {
        error = L"任务栏空间不足，无法安全放置额度窗口。";
        return false;
    }

    const LONG maximumTop = probe.baseSafeRect.bottom - height;
    const LONG top = std::clamp(childRect.top, probe.baseSafeRect.top, maximumTop);
    RECT targetRect{horizontal->left, top, horizontal->left + width, top + height};
    if (NativeRectsApproximatelyEqual(childRect, targetRect, tolerance))
    {
        error.clear();
        return true;
    }

    POINT points[2]{{targetRect.left, targetRect.top}, {targetRect.right, targetRect.bottom}};
    SetLastError(ERROR_SUCCESS);
    if (MapWindowPoints(HWND_DESKTOP, probe.taskbar, points, 2) == 0
        && GetLastError() != ERROR_SUCCESS)
    {
        error = L"无法把屏幕坐标转换为任务栏客户区坐标。";
        return false;
    }
    if (!SetWindowPos(
            childWindow,
            nullptr,
            points[0].x,
            points[0].y,
            points[1].x - points[0].x,
            points[1].y - points[0].y,
            SWP_NOACTIVATE | SWP_NOZORDER))
    {
        error = L"无法在任务栏安全空白区原位调整额度窗口。";
        return false;
    }

    if (!ValidatePlacement(childWindow, probe, validationError))
    {
        error = validationError.empty()
            ? L"原位调整后额度窗口仍未进入安全区域。"
            : validationError;
        return false;
    }

    error.clear();
    return true;
}

bool TaskbarHost::RepositionToRightmostSafeRect(
    HWND childWindow,
    const TaskbarProbeResult& probe,
    std::wstring& error,
    LONG desiredWidthDip) const
{
    if (!probe.supported || !IsWindow(childWindow) || !IsWindow(probe.taskbar)
        || GetParent(childWindow) != probe.taskbar)
    {
        error = L"额度窗口已失去主任务栏父窗口。";
        return false;
    }

    RECT childRect{};
    if (!GetWindowRect(childWindow, &childRect))
    {
        error = L"无法读取额度窗口位置。";
        return false;
    }

    const LONG minimumWidth = MulDiv(68, probe.dpi, 96);
    const LONG desiredWidth = std::max<LONG>(1, MulDiv(desiredWidthDip, probe.dpi, 96));
    LONG width = RectWidth(childRect) > 0 ? RectWidth(childRect) : desiredWidth;
    width = std::min(width, RectWidth(probe.safeRect));
    if (width < minimumWidth)
    {
        error = L"任务栏空间不足，无法恢复最右安全位置。";
        return false;
    }

    LONG height = RectHeight(childRect);
    if (height <= 0)
    {
        const LONG verticalMargin = MulDiv(3, probe.dpi, 96);
        height = std::max<LONG>(1, RectHeight(probe.taskbarRect) - verticalMargin * 2);
    }
    height = std::min(height, RectHeight(probe.baseSafeRect));
    if (height <= 0)
    {
        error = L"任务栏安全区域高度无效。";
        return false;
    }

    const LONG maximumTop = probe.baseSafeRect.bottom - height;
    const LONG top = std::clamp(childRect.top, probe.baseSafeRect.top, maximumTop);
    RECT targetRect{
        probe.safeRect.right - width,
        top,
        probe.safeRect.right,
        top + height};
    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    if (NativeRectsApproximatelyEqual(childRect, targetRect, tolerance))
    {
        error.clear();
        return true;
    }

    POINT points[2]{{targetRect.left, targetRect.top}, {targetRect.right, targetRect.bottom}};
    SetLastError(ERROR_SUCCESS);
    if (MapWindowPoints(HWND_DESKTOP, probe.taskbar, points, 2) == 0
        && GetLastError() != ERROR_SUCCESS)
    {
        error = L"无法把屏幕坐标转换为任务栏客户区坐标。";
        return false;
    }
    if (!SetWindowPos(
            childWindow,
            nullptr,
            points[0].x,
            points[0].y,
            points[1].x - points[0].x,
            points[1].y - points[0].y,
            SWP_NOACTIVATE | SWP_NOZORDER))
    {
        error = L"无法恢复任务栏最右安全位置。";
        return false;
    }

    std::wstring validationError;
    if (!ValidatePlacement(childWindow, probe, validationError))
    {
        POINT originalPoints[2]{
            {childRect.left, childRect.top},
            {childRect.right, childRect.bottom}};
        SetLastError(ERROR_SUCCESS);
        if (MapWindowPoints(HWND_DESKTOP, probe.taskbar, originalPoints, 2) != 0
            || GetLastError() == ERROR_SUCCESS)
        {
            static_cast<void>(SetWindowPos(
                childWindow,
                nullptr,
                originalPoints[0].x,
                originalPoints[0].y,
                originalPoints[1].x - originalPoints[0].x,
                originalPoints[1].y - originalPoints[0].y,
                SWP_NOACTIVATE | SWP_NOZORDER));
        }
        error = validationError.empty()
            ? L"恢复后额度窗口未进入最右安全位置。"
            : validationError;
        return false;
    }

    error.clear();
    return true;
}

bool TaskbarHost::ExternalLayoutChanged(const TaskbarProbeResult& probe) const
{
    if (!probe.supported || !IsWindow(probe.taskbar)) return true;
    TaskbarProbeResult current = probe;
    if (!RefreshExternalLayout(current)) return true;
    const LONG tolerance = std::max<LONG>(2, MulDiv(2, probe.dpi, 96));
    return !NativeRectsApproximatelyEqual(current.safeRect, probe.safeRect, tolerance);
}

std::wstring TaskbarHost::DpiAwarenessName(DPI_AWARENESS_CONTEXT context)
{
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        return L"PerMonitorV2";
    }
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
    {
        return L"PerMonitor";
    }
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE))
    {
        return L"System";
    }
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED))
    {
        return L"UnawareGdiScaled";
    }
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE))
    {
        return L"Unaware";
    }
    return L"Unknown";
}

std::wstring TaskbarHost::BuildEnvironmentReport()
{
    std::wostringstream output;
    output << L"CodexQuotaTaskbar phase-0 environment probe\n";
    output << L"Windows build: " << WindowsVersion() << L"\n";
    output << L"Process DPI awareness: " << DpiAwarenessName(GetThreadDpiAwarenessContext()) << L"\n";

    std::vector<MonitorRecord> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    output << L"Monitors: " << monitors.size() << L"\n";
    for (std::size_t index = 0; index < monitors.size(); ++index)
    {
        const auto& monitor = monitors[index];
        output << L"  [" << index << L"] " << monitor.device
               << L" primary=" << (monitor.primary ? L"yes" : L"no")
               << L" monitor=" << FormatRect(monitor.monitor)
               << L" work=" << FormatRect(monitor.work) << L"\n";
    }

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar)
    {
        output << L"Taskbar: not found\n";
        return output.str();
    }

    RECT taskbarRect{};
    GetWindowRect(taskbar, &taskbarRect);
    DWORD taskbarProcessId = 0;
    GetWindowThreadProcessId(taskbar, &taskbarProcessId);
    output << L"Taskbar: class=" << WindowClassName(taskbar)
           << L" hwnd=" << FormatHandle(taskbar)
           << L" pid=" << taskbarProcessId
           << L" dpi=" << GetDpiForWindow(taskbar)
           << L" rect=" << FormatRect(taskbarRect)
           << L" awareness=" << DpiAwarenessName(GetWindowDpiAwarenessContext(taskbar))
           << L"\n";
    output << L"Taskbar window tree:\n";
    AppendWindowTree(taskbar, 1, output);

    TaskbarHost host;
    TaskbarProbeResult probe = host.ProbeCompatibility();
    output << L"Compatibility: " << (probe.supported ? L"supported" : L"unsupported") << L"\n";
    if (!probe.reason.empty())
    {
        output << L"Reason: " << probe.reason << L"\n";
    }
    output << L"Notification rect: " << FormatRect(probe.notificationRect) << L"\n";
    output << L"Rightmost interactive edge: " << probe.rightmostOccupied << L"\n";
    output << L"Base safe rect: " << FormatRect(probe.baseSafeRect) << L"\n";
    output << L"Safe rect: " << FormatRect(probe.safeRect) << L"\n";
    output << L"External taskbar obstacles: " << probe.externalObstacles.size() << L"\n";
    for (const auto& obstacle : probe.externalObstacles)
    {
        output << L"  - class=" << obstacle.className << L" pid=" << obstacle.processId
               << L" rect=" << FormatRect(obstacle.rect) << L"\n";
    }
    output << L"Interactive UIA elements before notification area: " << probe.occupiedElements.size() << L"\n";
    for (const auto& element : probe.occupiedElements)
    {
        output << L"  - type=" << element.controlType << L" rect=" << FormatRect(element.rect) << L"\n";
    }
    return output.str();
}

} // namespace cqt
