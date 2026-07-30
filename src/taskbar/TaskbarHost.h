#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace cqt
{

struct OccupiedElement
{
    RECT rect{};
    int controlType = 0;
};

struct ExternalWindowObstacle
{
    RECT rect{};
    DWORD processId = 0;
    std::wstring className;
};

struct TaskbarShellSignature
{
    HWND taskbar = nullptr;
    HWND rebar = nullptr;
    HWND taskSwitch = nullptr;
    HWND notificationArea = nullptr;
    RECT taskbarRect{};
    RECT rebarRect{};
    RECT taskSwitchRect{};
    RECT notificationRect{};
    UINT dpi = 96;
};

enum class TaskbarProbeFailure
{
    None,
    TaskbarUnavailable,
    PrimaryMonitorUnavailable,
    TaskbarGeometryUnavailable,
    UnsupportedTaskbarLayout,
    ShellStructureUnavailable,
    NotificationAreaUnavailable,
    AutomationUnavailable,
    AutomationTreeUnavailable,
    AutomationQueryUnavailable,
    TaskButtonBoundaryUnavailable,
    InsufficientSafeSpace,
    InsufficientExternalSafeSpace,
};

[[nodiscard]] constexpr bool IsTransientTaskbarProbeFailure(TaskbarProbeFailure failure)
{
    switch (failure)
    {
    case TaskbarProbeFailure::TaskbarUnavailable:
    case TaskbarProbeFailure::PrimaryMonitorUnavailable:
    case TaskbarProbeFailure::TaskbarGeometryUnavailable:
    case TaskbarProbeFailure::ShellStructureUnavailable:
    case TaskbarProbeFailure::NotificationAreaUnavailable:
    case TaskbarProbeFailure::AutomationUnavailable:
    case TaskbarProbeFailure::AutomationTreeUnavailable:
    case TaskbarProbeFailure::AutomationQueryUnavailable:
    case TaskbarProbeFailure::TaskButtonBoundaryUnavailable:
        return true;
    default:
        return false;
    }
}

struct TaskbarProbeResult
{
    bool supported = false;
    TaskbarProbeFailure failure = TaskbarProbeFailure::None;
    HWND taskbar = nullptr;
    HWND notificationArea = nullptr;
    RECT taskbarRect{};
    RECT notificationRect{};
    RECT baseSafeRect{};
    RECT safeRect{};
    UINT dpi = 96;
    LONG rightmostOccupied = 0;
    std::vector<OccupiedElement> occupiedElements;
    std::vector<ExternalWindowObstacle> externalObstacles;
    TaskbarShellSignature shellSignature{};
    std::wstring reason;
};

class TaskbarHost
{
public:
    TaskbarHost() = default;
    ~TaskbarHost();
    TaskbarHost(const TaskbarHost&) = delete;
    TaskbarHost& operator=(const TaskbarHost&) = delete;

    TaskbarProbeResult ProbeCompatibility() const;
    bool Attach(HWND childWindow, const TaskbarProbeResult& probe, std::wstring& error,
                LONG desiredWidthDip = 84) const;
    bool ValidatePlacement(HWND childWindow, const TaskbarProbeResult& probe, std::wstring& error) const;
    bool RefreshExternalLayout(TaskbarProbeResult& probe) const;
    bool HasLightweightStructureChanged(const TaskbarProbeResult& probe) const;
    bool IsForegroundFullscreen(const TaskbarProbeResult& probe) const;
    bool RepositionWithinSafeRect(
        HWND childWindow,
        const TaskbarProbeResult& probe,
        std::wstring& error,
        LONG desiredWidthDip = 84) const;
    bool RepositionToRightmostSafeRect(
        HWND childWindow,
        const TaskbarProbeResult& probe,
        std::wstring& error,
        LONG desiredWidthDip = 84) const;
    bool ExternalLayoutChanged(const TaskbarProbeResult& probe) const;
    [[nodiscard]] bool StartEventMonitoring(
        HWND notificationWindow,
        UINT notificationMessage,
        const TaskbarProbeResult& probe);
    void StopEventMonitoring();

    static std::wstring BuildEnvironmentReport();
    static std::wstring DpiAwarenessName(DPI_AWARENESS_CONTEXT context);

private:
    static void CALLBACK WinEventProc(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND window,
        LONG objectId,
        LONG childId,
        DWORD eventThread,
        DWORD eventTime);
    void HandleWinEvent(DWORD event, HWND window, LONG objectId, LONG childId) const;
    static HWND FindDescendantByClass(HWND parent, const wchar_t* className);
    static bool CaptureShellSignature(HWND taskbar, TaskbarShellSignature& signature);
    static bool IsInteractiveControlType(int controlType);
    static bool RectInside(const RECT& inner, const RECT& outer, LONG tolerance = 0);

    HWND eventNotificationWindow_ = nullptr;
    HWND monitoredTaskbar_ = nullptr;
    HWND monitoredNotificationArea_ = nullptr;
    UINT eventNotificationMessage_ = 0;
    std::vector<HWINEVENTHOOK> eventHooks_;
};

} // namespace cqt
