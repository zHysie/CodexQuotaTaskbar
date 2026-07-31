#include "taskbar/TaskbarCollision.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

constexpr wchar_t kControllerClass[] = L"CodexQuotaTaskbar.Controller";
constexpr wchar_t kDisplayClass[] = L"CodexQuotaTaskbar.Display";
constexpr wchar_t kFullscreenClass[] = L"CodexQuotaTaskbar.StabilityProbe.Fullscreen";
constexpr wchar_t kBottomChildClass[] = L"CodexQuotaTaskbar.StabilityProbe.BottomChild";
constexpr UINT kTaskbarStructureChanged = WM_APP + 2;
constexpr auto kSampleInterval = std::chrono::milliseconds(25);
constexpr auto kDesktopOverlayTimeout = std::chrono::milliseconds(2000);
constexpr auto kDesktopRecoveryTimeout = std::chrono::milliseconds(250);

struct ProbeOptions
{
    std::optional<unsigned int> desktopCycles;
    std::optional<unsigned int> observeSeconds;
    bool observeAutoHideRestore = false;
};

struct ProbeState
{
    int passed = 0;
    int failed = 0;
    FILE* report = nullptr;
};

struct WindowSnapshot
{
    HWND window = nullptr;
    HWND parent = nullptr;
    RECT rect{};
    bool visible = false;
};

struct PhaseStats
{
    unsigned int samples = 0;
    unsigned int missingSamples = 0;
    unsigned int changedHandleSamples = 0;
    unsigned int changedParentSamples = 0;
    unsigned int changedRectSamples = 0;
    unsigned int visibilityMismatchSamples = 0;
    unsigned int visibilityTransitions = 0;
    bool lastVisible = false;
    bool hasVisibilitySample = false;
};

struct TestWindows
{
    HWND fullscreen = nullptr;
    HWND bottomChild = nullptr;
};

struct CycleStats
{
    unsigned int requested = 0;
    unsigned int attempted = 0;
    unsigned int passed = 0;
    unsigned int triggerFailures = 0;
    unsigned int stabilityFailures = 0;
    unsigned int recoveryFailures = 0;
};

struct ObservationStats
{
    std::uint64_t samples = 0;
    std::uint64_t missingSamples = 0;
    std::uint64_t changedHandleSamples = 0;
    std::uint64_t changedParentSamples = 0;
    std::uint64_t invisibleSamples = 0;
    std::uint64_t visibilityTransitions = 0;
    std::uint64_t rectTransitions = 0;
    std::uint64_t allowedLeftTransitions = 0;
    std::uint64_t allowedTrayReclaimTransitions = 0;
    std::uint64_t rightTransitions = 0;
    std::uint64_t invalidGeometryTransitions = 0;
};

void WriteLine(ProbeState& state, const std::wstring& line)
{
    std::wprintf(L"%ls\n", line.c_str());
    std::fflush(stdout);
    if (state.report)
    {
        std::fwprintf(state.report, L"%ls\n", line.c_str());
        std::fflush(state.report);
    }
}

void Check(ProbeState& state, bool condition, std::wstring_view description)
{
    std::wstring line = condition ? L"[PASS] " : L"[FAIL] ";
    line.append(description);
    WriteLine(state, line);
    if (condition)
        ++state.passed;
    else
        ++state.failed;
}

bool ParsePositiveUnsigned(const wchar_t* value, unsigned int maximum, unsigned int& parsed)
{
    if (!value || *value == L'\0') return false;
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long number = std::wcstoul(value, &end, 10);
    if (errno == ERANGE || end == value || *end != L'\0' || number == 0 || number > maximum)
        return false;
    parsed = static_cast<unsigned int>(number);
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, ProbeOptions& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--desktop-cycles")
        {
            if (options.desktopCycles.has_value() || index + 1 >= argc) return false;
            unsigned int cycles = 0;
            if (!ParsePositiveUnsigned(argv[++index], 100, cycles)) return false;
            options.desktopCycles = cycles;
        }
        else if (argument == L"--observe-seconds")
        {
            if (options.observeSeconds.has_value() || index + 1 >= argc) return false;
            unsigned int seconds = 0;
            if (!ParsePositiveUnsigned(argv[++index], 3600, seconds)) return false;
            options.observeSeconds = seconds;
        }
        else if (argument == L"--observe-autohide-restore")
        {
            if (options.observeAutoHideRestore) return false;
            options.observeAutoHideRestore = true;
        }
        else
        {
            return false;
        }
    }
    return true;
}

void WriteUsage()
{
    std::wprintf(
        L"Usage: TaskbarStabilityProbe.exe [--desktop-cycles 1..100] "
        L"[--observe-seconds 1..3600] [--observe-autohide-restore]\n");
}

bool ClassEquals(HWND window, const wchar_t* expected)
{
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0
        && wcscmp(className, expected) == 0;
}

std::wstring WindowClassName(HWND window)
{
    wchar_t className[256]{};
    const int length = window
        ? GetClassNameW(window, className, static_cast<int>(std::size(className)))
        : 0;
    return length > 0 ? std::wstring(className, length) : L"--";
}

HWND FindDisplayWindow()
{
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) return nullptr;

    struct Search { HWND result = nullptr; } search;
    EnumChildWindows(
        taskbar,
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<Search*>(parameter);
            if (ClassEquals(window, kDisplayClass))
            {
                search->result = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

bool CaptureDisplay(HWND expectedWindow, WindowSnapshot& snapshot)
{
    const HWND window = FindDisplayWindow();
    if (!window || !IsWindow(window) || (expectedWindow && window != expectedWindow)) return false;

    RECT rect{};
    if (!GetWindowRect(window, &rect)) return false;
    snapshot = {window, GetParent(window), rect, IsWindowVisible(window) != FALSE};
    return true;
}

std::wstring FormatHandle(HWND window)
{
    std::wostringstream output;
    output << L"0x" << std::hex << std::uppercase
           << reinterpret_cast<std::uintptr_t>(window);
    return output.str();
}

std::wstring FormatRect(const RECT& rect)
{
    std::wostringstream output;
    output << L"[" << rect.left << L"," << rect.top << L"," << rect.right << L"," << rect.bottom << L"]";
    return output.str();
}

cqt::RectangleEdges ToRectangleEdges(const RECT& rect)
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

void PumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool ActivateTestWindow(HWND window)
{
    if (!window || !IsWindow(window)) return false;

    const HWND previousForeground = GetForegroundWindow();
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
    const DWORD foregroundThread = previousForeground
        ? GetWindowThreadProcessId(previousForeground, nullptr)
        : 0;
    const bool attachedForeground = foregroundThread != 0 && foregroundThread != currentThread
        && AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    const bool attachedTarget = targetThread != 0 && targetThread != currentThread
        && targetThread != foregroundThread
        && AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;

    BringWindowToTop(window);
    SetActiveWindow(window);
    SetFocus(window);
    static_cast<void>(SetForegroundWindow(window));

    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != window)
    {
        INPUT alt[2]{};
        alt[0].type = INPUT_KEYBOARD;
        alt[0].ki.wVk = VK_MENU;
        alt[1] = alt[0];
        alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
        static_cast<void>(SendInput(static_cast<UINT>(std::size(alt)), alt, sizeof(INPUT)));
        BringWindowToTop(window);
        static_cast<void>(SetForegroundWindow(window));
    }

    if (attachedTarget) AttachThreadInput(currentThread, targetThread, FALSE);
    if (attachedForeground) AttachThreadInput(currentThread, foregroundThread, FALSE);
    PumpMessages();
    return GetAncestor(GetForegroundWindow(), GA_ROOT) == window;
}

void SampleDisplay(const WindowSnapshot& baseline, PhaseStats& stats)
{
    ++stats.samples;
    const HWND currentWindow = FindDisplayWindow();
    if (!currentWindow || !IsWindow(currentWindow))
    {
        ++stats.missingSamples;
        return;
    }

    if (currentWindow != baseline.window) ++stats.changedHandleSamples;
    if (GetParent(currentWindow) != baseline.parent) ++stats.changedParentSamples;

    RECT currentRect{};
    if (!GetWindowRect(currentWindow, &currentRect)
        || EqualRect(&currentRect, &baseline.rect) == FALSE)
    {
        ++stats.changedRectSamples;
    }

    const bool visible = IsWindowVisible(currentWindow) != FALSE;
    if (visible != baseline.visible) ++stats.visibilityMismatchSamples;
    if (stats.hasVisibilitySample && visible != stats.lastVisible)
        ++stats.visibilityTransitions;
    stats.lastVisible = visible;
    stats.hasVisibilitySample = true;
}

void MergePhaseStats(PhaseStats& total, const PhaseStats& phase)
{
    total.samples += phase.samples;
    total.missingSamples += phase.missingSamples;
    total.changedHandleSamples += phase.changedHandleSamples;
    total.changedParentSamples += phase.changedParentSamples;
    total.changedRectSamples += phase.changedRectSamples;
    total.visibilityMismatchSamples += phase.visibilityMismatchSamples;
    total.visibilityTransitions += phase.visibilityTransitions;
    if (total.hasVisibilitySample && phase.hasVisibilitySample
        && total.lastVisible != phase.lastVisible)
    {
        ++total.visibilityTransitions;
    }
    if (phase.hasVisibilitySample)
    {
        total.lastVisible = phase.lastVisible;
        total.hasVisibilitySample = true;
    }
}

bool IdentityAndGeometryStable(const PhaseStats& stats)
{
    return stats.samples != 0
        && stats.missingSamples == 0
        && stats.changedHandleSamples == 0
        && stats.changedParentSamples == 0
        && stats.changedRectSamples == 0;
}

PhaseStats Observe(const WindowSnapshot& baseline, std::chrono::milliseconds duration)
{
    PhaseStats stats;
    const auto deadline = std::chrono::steady_clock::now() + duration;
    do
    {
        PumpMessages();
        SampleDisplay(baseline, stats);
        std::this_thread::sleep_for(kSampleInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    PumpMessages();
    return stats;
}

PhaseStats ObserveUntil(
    const WindowSnapshot& baseline,
    std::chrono::milliseconds timeout,
    const std::function<bool()>& condition,
    bool& conditionMet)
{
    PhaseStats stats;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        PumpMessages();
        SampleDisplay(baseline, stats);
        if (condition())
        {
            conditionMet = true;
            break;
        }
        std::this_thread::sleep_for(kSampleInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    PumpMessages();
    return stats;
}

void WritePhaseSummary(ProbeState& state, std::wstring_view name, const PhaseStats& stats)
{
    std::wostringstream output;
    output << name << L": samples=" << stats.samples
           << L" missing=" << stats.missingSamples
           << L" handle_changed=" << stats.changedHandleSamples
           << L" parent_changed=" << stats.changedParentSamples
           << L" rect_changed=" << stats.changedRectSamples
           << L" visibility_mismatch=" << stats.visibilityMismatchSamples
           << L" visibility_transitions=" << stats.visibilityTransitions;
    WriteLine(state, output.str());
}

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterTestClasses(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = TestWindowProc;

    windowClass.lpszClassName = kFullscreenClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    windowClass.lpszClassName = kBottomChildClass;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool GetPrimaryMonitorRect(RECT& monitorRect)
{
    POINT origin{};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return false;
    monitorRect = info.rcMonitor;
    return true;
}

TestWindows CreateTestWindows(
    HINSTANCE instance,
    const RECT& monitorRect,
    const RECT& taskbarRect,
    const RECT& displayRect)
{
    TestWindows windows;
    windows.fullscreen = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kFullscreenClass,
        L"Taskbar stability fullscreen fixture",
        WS_POPUP,
        monitorRect.left,
        monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!windows.fullscreen) return windows;

    static_cast<void>(SetLayeredWindowAttributes(windows.fullscreen, 0, 1, LWA_ALPHA));

    const LONG childLeft = std::clamp(
        displayRect.left - monitorRect.left,
        0L,
        std::max(0L, monitorRect.right - monitorRect.left - 1));
    const LONG childTop = std::clamp(
        taskbarRect.top - monitorRect.top,
        0L,
        std::max(0L, monitorRect.bottom - monitorRect.top - 1));
    const LONG childWidth = std::max<LONG>(1, displayRect.right - displayRect.left);
    const LONG childHeight = std::max<LONG>(1, taskbarRect.bottom - taskbarRect.top);
    windows.bottomChild = CreateWindowExW(
        0,
        kBottomChildClass,
        L"Taskbar stability bottom child fixture",
        WS_CHILD | WS_VISIBLE,
        childLeft,
        childTop,
        childWidth,
        childHeight,
        windows.fullscreen,
        nullptr,
        instance,
        nullptr);
    if (!windows.bottomChild)
    {
        DestroyWindow(windows.fullscreen);
        windows.fullscreen = nullptr;
        return windows;
    }

    SetWindowPos(
        windows.fullscreen,
        HWND_TOPMOST,
        monitorRect.left,
        monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        SWP_SHOWWINDOW);
    ShowWindow(windows.fullscreen, SW_SHOW);
    UpdateWindow(windows.fullscreen);
    static_cast<void>(ActivateTestWindow(windows.fullscreen));
    return windows;
}

void DestroyTestWindows(TestWindows& windows)
{
    if (windows.bottomChild && IsWindow(windows.bottomChild)) DestroyWindow(windows.bottomChild);
    if (windows.fullscreen && IsWindow(windows.fullscreen)) DestroyWindow(windows.fullscreen);
    windows = {};
    PumpMessages();
}

bool SameDisplayState(const WindowSnapshot& baseline, bool requireVisibility)
{
    WindowSnapshot current;
    return CaptureDisplay(baseline.window, current)
        && current.parent == baseline.parent
        && EqualRect(&current.rect, &baseline.rect) != FALSE
        && (!requireVisibility || current.visible == baseline.visible);
}

bool WaitForBaseline(const WindowSnapshot& baseline, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        PumpMessages();
        if (SameDisplayState(baseline, true)) return true;
        std::this_thread::sleep_for(kSampleInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    return SameDisplayState(baseline, true);
}

bool SendVirtualKey(WORD virtualKey)
{
    INPUT input[2]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = virtualKey;
    input[1] = input[0];
    if (virtualKey == VK_SNAPSHOT)
    {
        input[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        input[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    }
    else
    {
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(static_cast<UINT>(std::size(input)), input, sizeof(INPUT)) == std::size(input);
}

bool SendWinShiftS()
{
    INPUT input[6]{};
    constexpr WORD keys[] = {VK_LWIN, VK_SHIFT, L'S'};
    for (std::size_t index = 0; index < std::size(keys); ++index)
    {
        input[index].type = INPUT_KEYBOARD;
        input[index].ki.wVk = keys[index];
    }
    for (std::size_t index = 0; index < std::size(keys); ++index)
    {
        input[index + std::size(keys)].type = INPUT_KEYBOARD;
        input[index + std::size(keys)].ki.wVk = keys[std::size(keys) - index - 1];
        input[index + std::size(keys)].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(static_cast<UINT>(std::size(input)), input, sizeof(INPUT)) == std::size(input);
}

bool SendCtrlN()
{
    INPUT input[4]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = L'N';
    input[2] = input[1];
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3] = input[0];
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(static_cast<UINT>(std::size(input)), input, sizeof(INPUT)) == std::size(input);
}

bool WindowProcessNameEquals(HWND window, const wchar_t* expectedFileName)
{
    if (!window) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = processId
        ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)
        : nullptr;
    if (!process) return false;

    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    if (!queried) return false;

    const wchar_t* fileName = wcsrchr(path, L'\\');
    fileName = fileName ? fileName + 1 : path;
    return _wcsicmp(fileName, expectedFileName) == 0;
}

bool WindowBelongsToEdge(HWND window)
{
    if (!window || !IsWindowVisible(window) || GetAncestor(window, GA_ROOT) != window)
        return false;
    return WindowProcessNameEquals(window, L"msedge.exe");
}

HWND FindVisibleEdgeWindow(const RECT& monitorRect)
{
    struct Search
    {
        const RECT* monitorRect = nullptr;
        HMONITOR monitor = nullptr;
        HWND result = nullptr;
        std::uint64_t area = 0;
    } search{&monitorRect, MonitorFromRect(&monitorRect, MONITOR_DEFAULTTONEAREST), nullptr, 0};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<Search*>(parameter);
            RECT rect{};
            RECT intersection{};
            if (WindowBelongsToEdge(window)
                && !IsIconic(window)
                && MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == search->monitor
                && GetWindowRect(window, &rect)
                && IntersectRect(&intersection, &rect, search->monitorRect)
                && !(rect.left <= search->monitorRect->left + 2
                    && rect.top <= search->monitorRect->top + 2
                    && rect.right >= search->monitorRect->right - 2
                    && rect.bottom >= search->monitorRect->bottom - 2))
            {
                const std::uint64_t width = static_cast<std::uint64_t>(
                    std::max<LONG>(0, intersection.right - intersection.left));
                const std::uint64_t height = static_cast<std::uint64_t>(
                    std::max<LONG>(0, intersection.bottom - intersection.top));
                const std::uint64_t area = width * height;
                if (area > search->area)
                {
                    search->result = window;
                    search->area = area;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

HWND FindVisibleSnippingToolWindow()
{
    struct Search
    {
        HWND result = nullptr;
        std::uint64_t area = 0;
        bool preferredXamlWindow = false;
    } search;
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<Search*>(parameter);
            if (!IsWindowVisible(window) || IsIconic(window)
                || GetAncestor(window, GA_ROOT) != window
                || !WindowProcessNameEquals(window, L"SnippingTool.exe"))
            {
                return TRUE;
            }
            RECT rect{};
            if (!GetWindowRect(window, &rect)) return TRUE;
            const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{sizeof(monitorInfo)};
            if (monitor && GetMonitorInfoW(monitor, &monitorInfo)
                && rect.left <= monitorInfo.rcMonitor.left + 2
                && rect.top <= monitorInfo.rcMonitor.top + 2
                && rect.right >= monitorInfo.rcMonitor.right - 2
                && rect.bottom >= monitorInfo.rcMonitor.bottom - 2)
            {
                return TRUE;
            }
            const std::uint64_t width = static_cast<std::uint64_t>(
                std::max<LONG>(0, rect.right - rect.left));
            const std::uint64_t height = static_cast<std::uint64_t>(
                std::max<LONG>(0, rect.bottom - rect.top));
            const std::uint64_t area = width * height;
            const bool preferredXamlWindow = ClassEquals(window, L"XamlWindow");
            if ((preferredXamlWindow && !search->preferredXamlWindow)
                || (preferredXamlWindow == search->preferredXamlWindow && area > search->area))
            {
                search->result = window;
                search->area = area;
                search->preferredXamlWindow = preferredXamlWindow;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

std::vector<HWND> VisibleSnippingToolWindows()
{
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            if (IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window
                && WindowProcessNameEquals(window, L"SnippingTool.exe"))
            {
                reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(window);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

bool LaunchSnippingTool()
{
    wchar_t commandLine[] = L"SnippingTool.exe";
    STARTUPINFOW startupInfo{sizeof(startupInfo)};
    PROCESS_INFORMATION processInfo{};
    const bool launched = CreateProcessW(
        nullptr,
        commandLine,
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo) != FALSE;
    if (processInfo.hThread) CloseHandle(processInfo.hThread);
    if (processInfo.hProcess) CloseHandle(processInfo.hProcess);
    return launched;
}

HWND WaitForSnippingToolWindow(
    const WindowSnapshot& baseline,
    PhaseStats& phase,
    std::chrono::milliseconds timeout)
{
    HWND result = nullptr;
    bool found = false;
    PhaseStats waitStats = ObserveUntil(
        baseline,
        timeout,
        [&]() {
            result = FindVisibleSnippingToolWindow();
            return result != nullptr;
        },
        found);
    MergePhaseStats(phase, waitStats);
    return found ? result : nullptr;
}

void CloseStartedSnippingToolWindows(const std::vector<HWND>& windowsBeforeLaunch)
{
    const std::vector<HWND> currentWindows = VisibleSnippingToolWindows();
    std::vector<HWND> startedWindows;
    for (const HWND window : currentWindows)
    {
        if (std::find(windowsBeforeLaunch.begin(), windowsBeforeLaunch.end(), window)
            == windowsBeforeLaunch.end())
        {
            startedWindows.push_back(window);
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const bool anyRemaining = std::any_of(
            startedWindows.begin(),
            startedWindows.end(),
            [](HWND window) { return IsWindow(window) && IsWindowVisible(window); });
        if (!anyRemaining) break;
        PumpMessages();
        std::this_thread::sleep_for(kSampleInterval);
    }
}

bool ForegroundCoversMonitor(HWND expected, const RECT& monitorRect)
{
    const HWND root = GetAncestor(GetForegroundWindow(), GA_ROOT);
    if (!root || (expected && root != expected)) return false;
    RECT rect{};
    return GetWindowRect(root, &rect)
        && rect.left <= monitorRect.left + 2
        && rect.top <= monitorRect.top + 2
        && rect.right >= monitorRect.right - 2
        && rect.bottom >= monitorRect.bottom - 2;
}

bool WindowCoversMonitor(HWND window, const RECT& monitorRect)
{
    RECT rect{};
    return window && IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window
        && GetWindowRect(window, &rect)
        && rect.left <= monitorRect.left + 2
        && rect.top <= monitorRect.top + 2
        && rect.right >= monitorRect.right - 2
        && rect.bottom >= monitorRect.bottom - 2;
}

HWND FindSnippingOverlayWindow(HWND mainWindow, const RECT& monitorRect)
{
    struct Search
    {
        HWND mainWindow = nullptr;
        const RECT* monitorRect = nullptr;
        HWND result = nullptr;
    } search{mainWindow, &monitorRect, nullptr};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<Search*>(parameter);
            if (window != search->mainWindow
                && WindowCoversMonitor(window, *search->monitorRect)
                && WindowProcessNameEquals(window, L"SnippingTool.exe"))
            {
                search->result = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

bool WaitForWindowStable(
    HWND window,
    const WindowSnapshot& baseline,
    PhaseStats& phase,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds stableDuration,
    bool requireForeground)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::optional<std::chrono::steady_clock::time_point> stableSince;
    do
    {
        PumpMessages();
        SampleDisplay(baseline, phase);
        const bool ready = window && IsWindow(window) && IsWindowVisible(window) && !IsIconic(window)
            && (!requireForeground || GetAncestor(GetForegroundWindow(), GA_ROOT) == window);
        const auto now = std::chrono::steady_clock::now();
        if (ready)
        {
            if (!stableSince) stableSince = now;
            if (now - *stableSince >= stableDuration) return true;
        }
        else
        {
            stableSince.reset();
        }
        std::this_thread::sleep_for(kSampleInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    PumpMessages();
    return false;
}

bool WaitForReusableSnippingWindow(
    HWND& window,
    const WindowSnapshot& baseline,
    PhaseStats& phase,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds stableDuration)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    HWND stableCandidate = nullptr;
    std::optional<std::chrono::steady_clock::time_point> stableSince;
    do
    {
        PumpMessages();
        SampleDisplay(baseline, phase);
        const HWND candidate = FindVisibleSnippingToolWindow();
        const auto now = std::chrono::steady_clock::now();
        if (candidate && candidate == stableCandidate)
        {
            if (stableSince && now - *stableSince >= stableDuration)
            {
                window = candidate;
                return true;
            }
        }
        else
        {
            stableCandidate = candidate;
            stableSince = candidate
                ? std::optional<std::chrono::steady_clock::time_point>(now)
                : std::nullopt;
        }
        std::this_thread::sleep_for(kSampleInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    PumpMessages();
    return false;
}

std::vector<HWND> VisibleTopLevelWindows()
{
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            if (IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window)
                reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

HWND FindNewFullscreenWindow(const std::vector<HWND>& before, const RECT& monitorRect)
{
    struct Search
    {
        const std::vector<HWND>* before = nullptr;
        const RECT* monitorRect = nullptr;
        HWND result = nullptr;
    } search{&before, &monitorRect, nullptr};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<Search*>(parameter);
            if (std::find(search->before->begin(), search->before->end(), window)
                    == search->before->end()
                && WindowCoversMonitor(window, *search->monitorRect))
            {
                search->result = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}

bool VisibilityStable(const PhaseStats& stats)
{
    return stats.visibilityMismatchSamples == 0 && stats.visibilityTransitions == 0;
}

void WriteCycleSummary(ProbeState& state, std::wstring_view name, const CycleStats& stats)
{
    std::wostringstream output;
    output << name << L": requested=" << stats.requested
           << L" attempted=" << stats.attempted
           << L" passed=" << stats.passed
           << L" trigger_failures=" << stats.triggerFailures
           << L" stability_failures=" << stats.stabilityFailures
           << L" recovery_failures=" << stats.recoveryFailures;
    WriteLine(state, output.str());
}

bool RunDesktopCycles(
    ProbeState& state,
    const WindowSnapshot& baseline,
    const RECT& monitorRect,
    unsigned int cycles)
{
    const HWND foregroundBefore = GetForegroundWindow();
    CycleStats printScreen{cycles};
    CycleStats screenClip{cycles};
    CycleStats edgeFullscreen{cycles};

    WriteLine(state, L"Desktop cycles: PrintScreen begin");
    for (unsigned int cycle = 0; cycle < cycles; ++cycle)
    {
        ++printScreen.attempted;
        const std::vector<HWND> windowsBefore = VisibleTopLevelWindows();
        const bool triggered = SendVirtualKey(VK_SNAPSHOT);
        bool triggerComplete = triggered;
        bool overlayReady = false;
        HWND overlayWindow = nullptr;
        PhaseStats phase = ObserveUntil(
            baseline,
            std::chrono::milliseconds(750),
            [&]() {
                overlayWindow = FindNewFullscreenWindow(windowsBefore, monitorRect);
                return overlayWindow != nullptr;
            },
            overlayReady);
        if (overlayReady)
        {
            MergePhaseStats(phase, Observe(baseline, std::chrono::milliseconds(300)));
            const bool overlayForeground = GetAncestor(GetForegroundWindow(), GA_ROOT) == overlayWindow;
            const bool activated = overlayForeground || ActivateTestWindow(overlayWindow);
            triggerComplete = activated && SendVirtualKey(VK_ESCAPE) && triggerComplete;
        }
        if (!triggerComplete) ++printScreen.triggerFailures;
        MergePhaseStats(phase, Observe(baseline, kDesktopRecoveryTimeout));
        const bool stable = IdentityAndGeometryStable(phase) && VisibilityStable(phase);
        const bool recovered = SameDisplayState(baseline, true);
        if (!stable) ++printScreen.stabilityFailures;
        if (!recovered) ++printScreen.recoveryFailures;
        if (triggerComplete && stable && recovered) ++printScreen.passed;
        if (!triggerComplete || !stable || !recovered)
        {
            std::wostringstream detail;
            detail << L"PrintScreen cycle=" << (cycle + 1)
                   << L" trigger_complete=" << (triggerComplete ? L"yes" : L"no")
                   << L" stable=" << (stable ? L"yes" : L"no")
                   << L" recovered_250ms=" << (recovered ? L"yes" : L"no");
            WriteLine(state, detail.str());
            WritePhaseSummary(state, L"PrintScreen failed phase", phase);
        }
    }

    WriteLine(state, L"Desktop cycles: Win+Shift+S begin");
    HWND reusableSnippingWindow = nullptr;
    bool snippingStartedByProbe = false;
    bool capturedSnippingWindowsBeforeLaunch = false;
    std::vector<HWND> snippingWindowsBeforeLaunch;
    for (unsigned int cycle = 0; cycle < cycles; ++cycle)
    {
        ++screenClip.attempted;
        const HWND foreground = GetForegroundWindow();
        const std::vector<HWND> windowsBefore = VisibleTopLevelWindows();
        const bool directInput = SendWinShiftS();
        bool directOverlayReady = false;
        HWND directOverlay = nullptr;
        PhaseStats phase;
        if (directInput)
        {
            phase = ObserveUntil(
                baseline,
                kDesktopOverlayTimeout,
                [&]() {
                    directOverlay = FindNewFullscreenWindow(windowsBefore, monitorRect);
                    return directOverlay != nullptr;
                },
                directOverlayReady);
            if (directOverlayReady)
                MergePhaseStats(phase, Observe(baseline, std::chrono::milliseconds(500)));
        }

        bool directDismissed = false;
        if (directOverlay)
        {
            const bool overlayForeground = GetAncestor(GetForegroundWindow(), GA_ROOT) == directOverlay;
            const bool activated = overlayForeground || ActivateTestWindow(directOverlay);
            directDismissed = activated && SendVirtualKey(VK_ESCAPE);
        }

        const bool directHotkeyPassed = directInput && directOverlayReady && directDismissed;
        bool openWithSeen = false;
        bool openWithClosed = true;
        if (!directOverlayReady)
        {
            const HWND foregroundAfterDirect = GetAncestor(GetForegroundWindow(), GA_ROOT);
            openWithSeen = ClassEquals(foregroundAfterDirect, L"Open With");
            if (openWithSeen)
            {
                static_cast<void>(SendVirtualKey(VK_ESCAPE));
                MergePhaseStats(phase, Observe(baseline, std::chrono::milliseconds(300)));
                openWithClosed = !IsWindow(foregroundAfterDirect)
                    || !IsWindowVisible(foregroundAfterDirect);
                if (!openWithClosed
                    && GetAncestor(GetForegroundWindow(), GA_ROOT) == foregroundAfterDirect)
                {
                    static_cast<void>(SendVirtualKey(VK_ESCAPE));
                    MergePhaseStats(phase, Observe(baseline, std::chrono::milliseconds(300)));
                    openWithClosed = !IsWindow(foregroundAfterDirect)
                        || !IsWindowVisible(foregroundAfterDirect);
                }
            }
        }

        bool fallbackPassed = false;
        bool fallbackInput = false;
        bool fallbackOverlayReady = false;
        bool fallbackMainStable = false;
        bool fallbackMainReusable = false;
        HWND fallbackOverlay = nullptr;
        if (!directOverlayReady)
        {
            if (!reusableSnippingWindow || !IsWindow(reusableSnippingWindow)
                || !IsWindowVisible(reusableSnippingWindow))
            {
                reusableSnippingWindow = FindVisibleSnippingToolWindow();
                if (!reusableSnippingWindow)
                {
                    if (!capturedSnippingWindowsBeforeLaunch)
                    {
                        snippingWindowsBeforeLaunch = VisibleSnippingToolWindows();
                        capturedSnippingWindowsBeforeLaunch = true;
                    }
                    const bool launched = LaunchSnippingTool();
                    snippingStartedByProbe = snippingStartedByProbe || launched;
                    if (launched)
                    {
                        reusableSnippingWindow = WaitForSnippingToolWindow(
                            baseline,
                            phase,
                            std::chrono::seconds(5));
                    }
                }
            }

            if (reusableSnippingWindow && IsWindow(reusableSnippingWindow)
                && ActivateTestWindow(reusableSnippingWindow))
            {
                fallbackMainStable = WaitForWindowStable(
                    reusableSnippingWindow,
                    baseline,
                    phase,
                    kDesktopOverlayTimeout,
                    std::chrono::milliseconds(400),
                    true);
                if (fallbackMainStable)
                {
                    fallbackInput = SendCtrlN();
                    if (fallbackInput)
                    {
                        PhaseStats fallbackPhase = ObserveUntil(
                            baseline,
                            kDesktopOverlayTimeout,
                            [&]() {
                                fallbackOverlay = FindSnippingOverlayWindow(
                                    reusableSnippingWindow,
                                    monitorRect);
                                return fallbackOverlay != nullptr;
                            },
                            fallbackOverlayReady);
                        MergePhaseStats(phase, fallbackPhase);
                        if (fallbackOverlayReady)
                            MergePhaseStats(phase, Observe(baseline, std::chrono::milliseconds(500)));
                    }
                }
            }

            bool fallbackDismissed = false;
            if (fallbackOverlay)
            {
                const bool overlayForeground = GetAncestor(GetForegroundWindow(), GA_ROOT) == fallbackOverlay;
                const bool activated = overlayForeground || ActivateTestWindow(fallbackOverlay);
                fallbackDismissed = activated && SendVirtualKey(VK_ESCAPE);
            }
            if (fallbackDismissed)
            {
                bool overlayGone = false;
                PhaseStats exitPhase = ObserveUntil(
                    baseline,
                    kDesktopOverlayTimeout,
                    [&]() { return !IsWindowVisible(fallbackOverlay); },
                    overlayGone);
                MergePhaseStats(phase, exitPhase);
                if (overlayGone)
                {
                    fallbackMainReusable = WaitForReusableSnippingWindow(
                        reusableSnippingWindow,
                        baseline,
                        phase,
                        kDesktopOverlayTimeout,
                        std::chrono::milliseconds(300));
                }
            }
            fallbackPassed = reusableSnippingWindow && fallbackMainStable && fallbackInput
                && fallbackOverlayReady && fallbackDismissed && fallbackMainReusable;
        }

        const bool triggerComplete = directHotkeyPassed || fallbackPassed;
        if (!triggerComplete) ++screenClip.triggerFailures;
        MergePhaseStats(phase, Observe(baseline, kDesktopRecoveryTimeout));
        const bool stable = IdentityAndGeometryStable(phase) && VisibilityStable(phase);
        const bool recovered = SameDisplayState(baseline, true) && openWithClosed;
        if (!stable) ++screenClip.stabilityFailures;
        if (!recovered) ++screenClip.recoveryFailures;
        if (triggerComplete && stable && recovered) ++screenClip.passed;

        std::wostringstream result;
        result << L"Win+Shift+S cycle=" << (cycle + 1)
               << L" direct_hotkey_passed=" << (directHotkeyPassed ? L"yes" : L"no")
               << L" fallback_passed=" << (fallbackPassed ? L"yes" : L"no")
               << L" open_with_seen=" << (openWithSeen ? L"yes" : L"no")
               << L" open_with_closed=" << (openWithClosed ? L"yes" : L"no")
               << L" fallback_started_by_probe=" << (snippingStartedByProbe ? L"yes" : L"no")
               << L" stable=" << (stable ? L"yes" : L"no")
               << L" recovered_250ms=" << (recovered ? L"yes" : L"no");
        WriteLine(state, result.str());
        if (!triggerComplete || !stable || !recovered)
        {
            std::wostringstream detail;
            detail << L"Win+Shift+S failure cycle=" << (cycle + 1)
                   << L" direct_input=" << (directInput ? L"yes" : L"no")
                   << L" direct_overlay=" << (directOverlayReady ? L"yes" : L"no")
                   << L" fallback_input=" << (fallbackInput ? L"yes" : L"no")
                   << L" fallback_overlay=" << (fallbackOverlayReady ? L"yes" : L"no")
                   << L" fallback_main_stable=" << (fallbackMainStable ? L"yes" : L"no")
                   << L" fallback_main_reusable=" << (fallbackMainReusable ? L"yes" : L"no")
                   << L" stable=" << (stable ? L"yes" : L"no")
                   << L" recovered_250ms=" << (recovered ? L"yes" : L"no")
                   << L" before=" << FormatHandle(foreground)
                   << L" after=" << FormatHandle(GetForegroundWindow())
                   << L" class=" << WindowClassName(GetForegroundWindow());
            WriteLine(state, detail.str());
            WritePhaseSummary(state, L"Win+Shift+S failed phase", phase);
        }

    }

    if (snippingStartedByProbe)
        CloseStartedSnippingToolWindows(snippingWindowsBeforeLaunch);

    WriteLine(state, L"Desktop cycles: Edge F11 begin");
    const HWND edge = FindVisibleEdgeWindow(monitorRect);
    if (!edge)
    {
        edgeFullscreen.triggerFailures = cycles;
        WriteLine(state, L"Edge F11: no visible msedge.exe top-level window");
    }
    else
    {
        for (unsigned int cycle = 0; cycle < cycles; ++cycle)
        {
            ++edgeFullscreen.attempted;
            RECT edgeRectBefore{};
            const bool edgeRectReady = GetWindowRect(edge, &edgeRectBefore) != FALSE;
            const bool activated = ActivateTestWindow(edge);
            const bool enteredInput = edgeRectReady && activated && SendVirtualKey(VK_F11);
            bool entered = false;
            PhaseStats phase;
            if (enteredInput)
            {
                phase = ObserveUntil(
                    baseline,
                    kDesktopOverlayTimeout,
                    [&]() { return ForegroundCoversMonitor(edge, monitorRect); },
                    entered);
                if (entered)
                    MergePhaseStats(phase, Observe(baseline, std::chrono::milliseconds(750)));
            }

            RECT edgeRectAfterEntry{};
            const bool browserChanged = enteredInput
                && GetWindowRect(edge, &edgeRectAfterEntry)
                && EqualRect(&edgeRectAfterEntry, &edgeRectBefore) == FALSE;
            bool exited = false;
            if (browserChanged)
            {
                const bool exitInput = SendVirtualKey(VK_F11);
                if (exitInput)
                {
                    bool exitObserved = false;
                    PhaseStats exitPhase = ObserveUntil(
                        baseline,
                        kDesktopOverlayTimeout,
                        [&]() {
                            RECT current{};
                            return GetWindowRect(edge, &current)
                                && EqualRect(&current, &edgeRectBefore) != FALSE;
                        },
                        exitObserved);
                    exited = exitObserved;
                    MergePhaseStats(phase, exitPhase);
                }
            }
            if (!activated || !enteredInput || !entered || !exited)
            {
                ++edgeFullscreen.triggerFailures;
                std::wostringstream detail;
                detail << L"Edge F11 cycle=" << (cycle + 1)
                       << L" activated=" << (activated ? L"yes" : L"no")
                       << L" rect_ready=" << (edgeRectReady ? L"yes" : L"no")
                       << L" input=" << (enteredInput ? L"yes" : L"no")
                       << L" entered=" << (entered ? L"yes" : L"no")
                       << L" exited=" << (exited ? L"yes" : L"no")
                       << L" foreground=" << FormatHandle(GetForegroundWindow())
                       << L" class=" << WindowClassName(GetForegroundWindow());
                WriteLine(state, detail.str());
            }
            MergePhaseStats(phase, Observe(baseline, kDesktopRecoveryTimeout));
            const bool stable = IdentityAndGeometryStable(phase) && VisibilityStable(phase);
            const bool recovered = SameDisplayState(baseline, true);
            if (!stable) ++edgeFullscreen.stabilityFailures;
            if (!recovered) ++edgeFullscreen.recoveryFailures;
            if (activated && enteredInput && entered && exited && stable && recovered)
                ++edgeFullscreen.passed;

            // A failed exit must not leave the user's existing browser in F11 mode.
            if (edgeRectReady && WindowCoversMonitor(edge, monitorRect))
            {
                static_cast<void>(ActivateTestWindow(edge));
                static_cast<void>(SendVirtualKey(VK_F11));
                const auto restoreDeadline = std::chrono::steady_clock::now() + kDesktopOverlayTimeout;
                while (std::chrono::steady_clock::now() < restoreDeadline)
                {
                    RECT current{};
                    if (GetWindowRect(edge, &current)
                        && EqualRect(&current, &edgeRectBefore) != FALSE)
                        break;
                    PumpMessages();
                    std::this_thread::sleep_for(kSampleInterval);
                }
            }
        }
    }

    if (foregroundBefore && IsWindow(foregroundBefore))
        static_cast<void>(ActivateTestWindow(foregroundBefore));

    WriteCycleSummary(state, L"PrintScreen cycles", printScreen);
    WriteCycleSummary(state, L"Win+Shift+S cycles", screenClip);
    WriteCycleSummary(state, L"Edge F11 cycles", edgeFullscreen);
    const bool passed = printScreen.passed == cycles
        && screenClip.passed == cycles
        && edgeFullscreen.passed == cycles;
    Check(state, passed, L"真实 PrintScreen、截图遮罩和 Edge F11 循环全部稳定");
    return passed;
}

bool RunLongObservation(
    ProbeState& state,
    const WindowSnapshot& baseline,
    unsigned int seconds)
{
    ObservationStats stats;
    RECT lastRect = baseline.rect;
    HWND notificationArea = FindWindowExW(baseline.parent, nullptr, L"TrayNotifyWnd", nullptr);
    RECT trayRectAtLastWindowPosition{};
    static_cast<void>(notificationArea
        && GetWindowRect(notificationArea, &trayRectAtLastWindowPosition));
    bool lastVisible = baseline.visible;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);

    WriteLine(
        state,
        L"Long observation begin: seconds=" + std::to_wstring(seconds)
            + L" sample_interval_ms=25");
    do
    {
        PumpMessages();
        ++stats.samples;
        const HWND window = FindDisplayWindow();
        if (!window || !IsWindow(window))
        {
            ++stats.missingSamples;
        }
        else
        {
            if (window != baseline.window) ++stats.changedHandleSamples;
            if (GetParent(window) != baseline.parent) ++stats.changedParentSamples;

            const bool visible = IsWindowVisible(window) != FALSE;
            if (!visible) ++stats.invisibleSamples;
            if (visible != lastVisible) ++stats.visibilityTransitions;
            lastVisible = visible;

            RECT rect{};
            if (GetWindowRect(window, &rect) && EqualRect(&rect, &lastRect) == FALSE)
            {
                RECT currentTrayRect{};
                static_cast<void>(notificationArea
                    && GetWindowRect(notificationArea, &currentTrayRect));
                ++stats.rectTransitions;
                const LONG leftDelta = rect.left - lastRect.left;
                const LONG rightDelta = rect.right - lastRect.right;
                const bool sameVertical = rect.top == lastRect.top && rect.bottom == lastRect.bottom;
                const bool pureTranslation = sameVertical && leftDelta == rightDelta;
                if (pureTranslation && leftDelta < 0)
                    ++stats.allowedLeftTransitions;
                else if (cqt::IsAllowedTrayReclaimTransition(
                             ToRectangleEdges(lastRect),
                             ToRectangleEdges(rect),
                             ToRectangleEdges(trayRectAtLastWindowPosition),
                             ToRectangleEdges(currentTrayRect)))
                    ++stats.allowedTrayReclaimTransitions;
                else if (rect.left > lastRect.left || rect.right > lastRect.right)
                    ++stats.rightTransitions;
                else
                    ++stats.invalidGeometryTransitions;
                std::wostringstream transition;
                transition << L"Long observation rect transition: from=" << FormatRect(lastRect)
                           << L" to=" << FormatRect(rect)
                           << L" tray_from=" << FormatRect(trayRectAtLastWindowPosition)
                           << L" tray_now=" << FormatRect(currentTrayRect);
                WriteLine(state, transition.str());
                lastRect = rect;
                trayRectAtLastWindowPosition = currentTrayRect;
            }
        }
        std::this_thread::sleep_for(kSampleInterval);
    } while (std::chrono::steady_clock::now() < deadline);
    PumpMessages();

    std::wostringstream output;
    output << L"Long observation summary: seconds=" << seconds
           << L" samples=" << stats.samples
           << L" missing=" << stats.missingSamples
           << L" handle_changed=" << stats.changedHandleSamples
           << L" parent_changed=" << stats.changedParentSamples
           << L" invisible=" << stats.invisibleSamples
           << L" visibility_transitions=" << stats.visibilityTransitions
           << L" rect_transitions=" << stats.rectTransitions
           << L" allowed_left=" << stats.allowedLeftTransitions
           << L" allowed_tray_reclaim=" << stats.allowedTrayReclaimTransitions
           << L" right=" << stats.rightTransitions
           << L" invalid_geometry=" << stats.invalidGeometryTransitions;
    WriteLine(state, output.str());

    const bool passed = baseline.visible
        && stats.missingSamples == 0
        && stats.changedHandleSamples == 0
        && stats.changedParentSamples == 0
        && stats.invisibleSamples == 0
        && stats.visibilityTransitions == 0
        && stats.rightTransitions == 0
        && stats.invalidGeometryTransitions == 0;
    Check(
        state,
        passed,
        L"长时间采样中额度 HWND/父窗口持续存在，未异常右跳、未闪隐，仅允许同 HWND 纯左移或通知区收缩后的受限复位");
    return passed;
}

UINT QueryTaskbarAppBarState(HWND taskbar)
{
    APPBARDATA data{sizeof(data)};
    data.hWnd = taskbar;
    return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &data));
}

bool ObserveAutoHideRestore(
    ProbeState& state,
    const WindowSnapshot& hiddenBaseline,
    const RECT& monitorRect)
{
    const HWND taskbar = hiddenBaseline.parent;
    const bool precondition = (QueryTaskbarAppBarState(taskbar) & ABS_AUTOHIDE) != 0
        && hiddenBaseline.rect.top >= monitorRect.bottom;
    Check(state, precondition, L"恢复观察开始时任务栏和额度窗口处于真实自动隐藏位置");
    if (!precondition) return false;

    bool settingChanged = false;
    auto settingChangedAt = std::chrono::steady_clock::now();
    const auto deadline = settingChangedAt + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        PumpMessages();
        if ((QueryTaskbarAppBarState(taskbar) & ABS_AUTOHIDE) == 0)
        {
            settingChanged = true;
            settingChangedAt = std::chrono::steady_clock::now();
            break;
        }
        std::this_thread::sleep_for(kSampleInterval);
    }

    bool restored = false;
    bool sameHandleAndParent = true;
    long long restoreMilliseconds = -1;
    while (settingChanged && std::chrono::steady_clock::now() < deadline)
    {
        PumpMessages();
        WindowSnapshot current;
        RECT taskbarCurrent{};
        if (!CaptureDisplay(hiddenBaseline.window, current)
            || current.parent != hiddenBaseline.parent)
        {
            sameHandleAndParent = false;
            break;
        }
        if (current.visible
            && current.rect.bottom <= monitorRect.bottom
            && GetWindowRect(taskbar, &taskbarCurrent)
            && taskbarCurrent.bottom <= monitorRect.bottom)
        {
            restored = true;
            restoreMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - settingChangedAt).count();
            break;
        }
        std::this_thread::sleep_for(kSampleInterval);
    }

    {
        std::wostringstream output;
        output << L"Auto-hide restore observation: setting_changed="
               << (settingChanged ? L"yes" : L"no")
               << L" same_hwnd_parent=" << (sameHandleAndParent ? L"yes" : L"no")
               << L" restored=" << (restored ? L"yes" : L"no")
               << L" restore_ms=" << restoreMilliseconds;
        WriteLine(state, output.str());
    }
    const bool passed = settingChanged
        && sameHandleAndParent
        && restored
        && restoreMilliseconds >= 0
        && restoreMilliseconds <= kDesktopRecoveryTimeout.count();
    Check(
        state,
        passed,
        L"关闭自动隐藏后同一额度 HWND 和父窗口在 250ms 内回到屏幕内");
    return passed;
}

void OpenReport(ProbeState& state)
{
    wchar_t executablePath[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        executablePath,
        static_cast<DWORD>(std::size(executablePath)));
    if (length == 0 || length >= std::size(executablePath)) return;

    std::wstring reportPath(executablePath, length);
    const std::wstring::size_type separator = reportPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return;
    reportPath.resize(separator + 1);
    reportPath += L"TaskbarStabilityProbe-report.txt";
    _wfopen_s(&state.report, reportPath.c_str(), L"w, ccs=UTF-8");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    static_cast<void>(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    ProbeOptions options;
    if (!ParseOptions(argc, argv, options))
    {
        WriteUsage();
        return 2;
    }

    ProbeState state;
    OpenReport(state);
    WriteLine(state, L"TaskbarStabilityProbe sample_interval_ms=25");

    const HWND controller = FindWindowW(kControllerClass, nullptr);
    WindowSnapshot baseline;
    Check(state, controller != nullptr, L"找到正式版控制窗口");
    Check(state, CaptureDisplay(nullptr, baseline), L"找到正式版任务栏额度窗口");
    if (!controller || !baseline.window)
    {
        WriteLine(state, L"Summary: probe prerequisites missing");
        if (state.report) std::fclose(state.report);
        return 2;
    }

    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT taskbarRect{};
    RECT monitorRect{};
    const bool validTaskbar = taskbar && baseline.parent == taskbar && GetWindowRect(taskbar, &taskbarRect);
    Check(state, validTaskbar, L"额度窗口直接附着到主任务栏");
    const bool validMonitor = GetPrimaryMonitorRect(monitorRect);
    Check(state, validMonitor, L"读取主显示器矩形");

    {
        std::wostringstream output;
        output << L"Baseline: hwnd=" << FormatHandle(baseline.window)
               << L" parent=" << FormatHandle(baseline.parent)
               << L" rect=" << FormatRect(baseline.rect)
               << L" visible=" << (baseline.visible ? L"yes" : L"no");
        WriteLine(state, output.str());
    }

    if (options.desktopCycles || options.observeSeconds || options.observeAutoHideRestore)
    {
        bool passed = true;
        if (options.desktopCycles)
        {
            passed = validMonitor
                && RunDesktopCycles(state, baseline, monitorRect, *options.desktopCycles)
                && passed;
        }
        if (options.observeSeconds)
        {
            passed = RunLongObservation(state, baseline, *options.observeSeconds) && passed;
        }
        if (options.observeAutoHideRestore)
        {
            passed = validMonitor
                && ObserveAutoHideRestore(state, baseline, monitorRect)
                && passed;
        }
        std::wostringstream summary;
        summary << L"Summary: passed=" << state.passed << L" failed=" << state.failed;
        WriteLine(state, summary.str());
        if (state.report) std::fclose(state.report);
        return passed && state.failed == 0 ? 0 : 1;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const bool classesRegistered = RegisterTestClasses(instance);
    Check(state, classesRegistered, L"注册全屏复现窗口类");

    const HWND foregroundBefore = GetForegroundWindow();
    TestWindows testWindows;
    if (validTaskbar && validMonitor && classesRegistered)
        testWindows = CreateTestWindows(instance, monitorRect, taskbarRect, baseline.rect);

    RECT fullscreenRect{};
    Check(
        state,
        testWindows.fullscreen && (GetWindowLongPtrW(testWindows.fullscreen, GWL_STYLE) & WS_POPUP) != 0
            && GetParent(testWindows.fullscreen) == nullptr
            && GetWindowRect(testWindows.fullscreen, &fullscreenRect)
            && EqualRect(&fullscreenRect, &monitorRect) != FALSE,
        L"创建覆盖主显示器的顶层 WS_POPUP");
    RECT bottomChildRect{};
    RECT bottomIntersection{};
    Check(
        state,
        testWindows.bottomChild && GetParent(testWindows.bottomChild) == testWindows.fullscreen
            && (GetWindowLongPtrW(testWindows.bottomChild, GWL_STYLE) & WS_CHILD) != 0
            && GetWindowRect(testWindows.bottomChild, &bottomChildRect)
            && IntersectRect(&bottomIntersection, &bottomChildRect, &taskbarRect) != FALSE,
        L"创建与任务栏底部相交的子窗口");

    const bool fullscreenForeground = testWindows.fullscreen
        && GetAncestor(GetForegroundWindow(), GA_ROOT) == testWindows.fullscreen;
    Check(state, fullscreenForeground, L"复现顶层窗口成为前台全屏窗口");

    const PhaseStats fullscreenStats = Observe(baseline, std::chrono::seconds(6));
    WritePhaseSummary(state, L"Fullscreen phase", fullscreenStats);
    Check(
        state,
        fullscreenStats.missingSamples == 0 && fullscreenStats.changedHandleSamples == 0,
        L"全屏与底部子窗口持续 6 秒期间额度 HWND 未消失或更换");
    Check(state, fullscreenStats.changedParentSamples == 0, L"全屏复现期间额度父子关系不变");
    Check(state, fullscreenStats.changedRectSamples == 0, L"全屏复现期间额度坐标不变");
    Check(
        state,
        fullscreenStats.visibilityMismatchSamples == 0 && fullscreenStats.visibilityTransitions == 0,
        L"任务栏保持显示的全屏复现期间额度窗口不闪烁");

    DestroyTestWindows(testWindows);
    if (foregroundBefore && IsWindow(foregroundBefore))
        static_cast<void>(SetForegroundWindow(foregroundBefore));
    Check(state, WaitForBaseline(baseline, std::chrono::milliseconds(250)), L"全屏退出后 250ms 内恢复基线可见状态");

    WindowSnapshot softBaseline;
    const bool softBaselineReady = CaptureDisplay(nullptr, softBaseline);
    Check(state, softBaselineReady, L"软结构变化测试前额度窗口有效");
    const bool posted = softBaselineReady && PostMessageW(
        controller,
        kTaskbarStructureChanged,
        EVENT_OBJECT_LOCATIONCHANGE,
        reinterpret_cast<LPARAM>(taskbar));
    Check(state, posted, L"向控制窗口投递软结构变化消息 WM_APP+2");

    PhaseStats softStats;
    if (posted) softStats = Observe(softBaseline, std::chrono::milliseconds(2500));
    WritePhaseSummary(state, L"Soft-change phase", softStats);
    Check(
        state,
        posted && softStats.missingSamples == 0 && softStats.changedHandleSamples == 0,
        L"软结构变化复核期间额度 HWND 未消失或更换");
    Check(state, posted && softStats.changedParentSamples == 0, L"软结构变化复核期间父子关系不变");
    Check(state, posted && softStats.changedRectSamples == 0, L"软结构变化复核期间额度坐标不变");
    Check(
        state,
        posted && softStats.visibilityMismatchSamples == 0 && softStats.visibilityTransitions == 0,
        L"软结构变化复核期间可见性不闪烁");

    std::wostringstream summary;
    summary << L"Summary: passed=" << state.passed << L" failed=" << state.failed;
    WriteLine(state, summary.str());
    if (state.report) std::fclose(state.report);
    return state.failed == 0 ? 0 : 1;
}
