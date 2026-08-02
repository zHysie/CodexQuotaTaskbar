#include "App.h"

#include "AppVersion.h"
#include "settings/StartupManager.h"
#include "taskbar/ResumeValidationPolicy.h"
#include "taskbar/StartupAttachPolicy.h"
#include "ui/ContextMenu.h"
#include "ui/QuotaLayout.h"
#include "ui/TaskbarPresentation.h"
#include "ui/TooltipText.h"

#include <commctrl.h>
#include <objbase.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace
{

constexpr wchar_t kControllerClass[] = L"CodexQuotaTaskbar.Controller";
constexpr UINT_PTR kCountdownTimer = 1;
constexpr UINT_PTR kValidationTimer = 2;
constexpr UINT kQuotaUpdated = WM_APP + 1;
constexpr UINT kTaskbarStructureChanged = WM_APP + 2;
constexpr ULONGLONG kInitialReattachDelay = 1500;
constexpr ULONGLONG kRetryReattachDelay = 3000;
constexpr UINT kNormalValidationInterval = 2000;
constexpr UINT kFastValidationInterval = 250;
constexpr ULONGLONG kTrayReclaimStableDelay = 1000;
constexpr int kMaximumReattachAttempts = 3;

long long UnixNow() { return static_cast<long long>(std::time(nullptr)); }

LONG GeometryTolerance(UINT dpi)
{
    return std::max<LONG>(1, MulDiv(2, dpi == 0 ? 96 : dpi, 96));
}

bool RectApproximatelyEqual(const RECT& left, const RECT& right, LONG tolerance)
{
    return std::abs(left.left - right.left) <= tolerance
        && std::abs(left.top - right.top) <= tolerance
        && std::abs(left.right - right.right) <= tolerance
        && std::abs(left.bottom - right.bottom) <= tolerance;
}

bool IsTaskbarTemporarilyHidden(HWND taskbar, const RECT& taskbarRect, UINT dpi)
{
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) return false;

    RECT intersection{};
    if (!IntersectRect(&intersection, &taskbarRect, &monitorInfo.rcMonitor)) return true;

    const LONG visibleHeight = intersection.bottom - intersection.top;
    return visibleHeight <= GeometryTolerance(dpi);
}

} // namespace

namespace cqt
{

int App::Run(HINSTANCE instance)
{
    instance_ = instance;
    if (!instanceGuard_.Acquire())
    {
        ShowError(L"无法建立单实例互斥。", false);
        return 1;
    }
    if (instanceGuard_.AlreadyRunning()) return 0;
    if (!transport_.Initialize())
    {
        ShowError(L"无法初始化安全网络组件。", false);
        return 1;
    }
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        && GetLastError() != ERROR_ACCESS_DENIED)
    {
        ShowError(L"无法启用 Per-Monitor DPI Aware V2。", false);
        return 1;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        ShowError(L"无法初始化 COM。", false);
        return 1;
    }
    comInitialized_ = true;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls) || !RegisterControllerClass()
        || !taskbarWindow_.Initialize(instance_, [this](int x, int y) { HandleContextMenu(x, y); }))
    {
        ShowError(L"无法初始化正式版窗口组件。", false);
        Cleanup();
        return 1;
    }

    settingsPath_ = Settings::DefaultPath();
    settings_ = Settings::Load(settingsPath_);
    authPaths_ = CodexAuthReader::BuildSearchPathsFromEnvironment();
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    controller_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kControllerClass,
        L"CodexQuotaTaskbar Controller", WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, instance_, this);
    if (!controller_)
    {
        ShowError(L"无法创建应用控制窗口。", false);
        Cleanup();
        return 1;
    }
    std::wstring error;
    const bool attached = RunStartupAttachSequence(
        kStartupAttachPolicy,
        [this, &error](bool& retryable)
        {
            if (AttachToTaskbar(error, &retryable)) return true;

            // A failed SetParent or event-monitor setup may leave a partial
            // child relationship. Remove it before the next full UIA probe so
            // Explorer never enumerates our attached child during recovery.
            host_.StopEventMonitoring();
            taskbarWindow_.Destroy();
            currentProbe_ = {};
            return false;
        },
        [this](ULONGLONG delayMilliseconds)
        {
            return WaitForStartupAttachRetry(delayMilliseconds);
        });
    if (!attached)
    {
        if (shuttingDown_)
        {
            Cleanup();
            return 0;
        }
        ShowError(error.empty() ? L"无法附着主任务栏。" : error, false);
        Cleanup();
        return 2;
    }

    SetTimer(controller_, kCountdownTimer, 1000, nullptr);
    SetTimer(controller_, kValidationTimer, kNormalValidationInterval, nullptr);
    refreshController_.Start(authPaths_, settings_.refreshIntervalSeconds,
        [this] { if (controller_) PostMessageW(controller_, kQuotaUpdated, 0, 0); });
    static_cast<void>(authWatcher_.Start(authPaths_, [this] { refreshController_.NotifyCredentialsChanged(); }));
    UpdatePresentation();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Cleanup();
    return static_cast<int>(message.wParam);
}

bool App::RegisterControllerClass()
{
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = ControllerWindowProc;
    windowClass.lpszClassName = kControllerClass;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT CALLBACK App::ControllerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleControllerMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::HandleControllerMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0)
    {
        // During the bounded initial readiness wait there is no attached child
        // to replace; the next startup attempt will probe the newly created bar.
        if (currentProbe_.supported) RequestReattach();
        return 0;
    }
    switch (message)
    {
    case kQuotaUpdated: ApplyRefreshResult(); return 0;
    case kTaskbarStructureChanged: RequestSoftValidation(); return 0;
    case WM_TIMER:
        if (wParam == kCountdownTimer) { UpdatePresentation(); return 0; }
        if (wParam == kValidationTimer) { ValidateHost(); return 0; }
        break;
    case WM_DISPLAYCHANGE: RequestSoftValidation(); return 0;
    case WM_SETTINGCHANGE: UpdatePresentation(); return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
            RequestResumeValidation();
        return TRUE;
    case WM_CLOSE: BeginShutdown(); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool App::AttachToTaskbar(std::wstring& error, bool* retryable)
{
    if (retryable) *retryable = false;
    if (!taskbarWindow_.Create())
    {
        error = L"无法创建任务栏额度窗口。";
        return false;
    }
    const TaskbarProbeResult probe = host_.ProbeCompatibility();
    if (!probe.supported)
    {
        if (retryable) *retryable = IsTransientTaskbarProbeFailure(probe.failure);
        error = probe.reason;
        return false;
    }
    if (!host_.Attach(taskbarWindow_.Window(), probe, error, kTaskbarDisplayWidthDip))
    {
        if (retryable) *retryable = true;
        return false;
    }
    if (!host_.StartEventMonitoring(controller_, kTaskbarStructureChanged, probe))
    {
        if (retryable) *retryable = true;
        error = L"无法监听任务栏关键控件变化。";
        return false;
    }
    currentProbe_ = probe;
    ResetHostValidationState();
    return true;
}

bool App::WaitForStartupAttachRetry(ULONGLONG delayMilliseconds)
{
    const ULONGLONG deadline = GetTickCount64() + delayMilliseconds;
    while (!shuttingDown_)
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return true;
        const ULONGLONG remaining = deadline - now;
        const DWORD timeout = static_cast<DWORD>(std::min<ULONGLONG>(remaining, MAXDWORD));
        const DWORD waitResult = MsgWaitForMultipleObjectsEx(
            0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_TIMEOUT) return true;
        if (waitResult == WAIT_FAILED) return false;

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) return false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return false;
}

void App::RequestReattach()
{
    if (shuttingDown_ || reattachPending_) return;
    reattachPending_ = true;
    ResetHostValidationState();
    host_.StopEventMonitoring();
    nextReattachAttempt_ = GetTickCount64() + kInitialReattachDelay;
    reattachAttempts_ = 0;
    if (taskbarWindow_.Window()) ShowWindow(taskbarWindow_.Window(), SW_HIDE);
}

void App::RequestSoftValidation()
{
    if (shuttingDown_) return;
    softValidationPending_ = true;
    if (controller_) SetTimer(controller_, kValidationTimer, kFastValidationInterval, nullptr);
}

void App::RequestResumeValidation()
{
    if (shuttingDown_) return;
    resumeValidationDeadline_ = kResumeValidationPolicy.DeadlineFrom(GetTickCount64());
    softValidationPending_ = true;
    ResetSoftValidationSamples();
    if (controller_)
        SetTimer(
            controller_,
            kValidationTimer,
            kResumeValidationPolicy.validationIntervalMilliseconds,
            nullptr);
}

void App::ResetSoftValidationSamples()
{
    structuralChangeSamples_ = 0;
    externalCollisionState_ = {};
    trayReclaimState_ = {};
    trayReclaimArmed_ = false;
    nextExternalCollisionSample_ = 0;
    structuralCandidateValid_ = false;
    structuralTaskbarCandidate_ = {};
    structuralDpiCandidate_ = 0;
}

void App::ResetHostValidationState()
{
    softValidationPending_ = false;
    resumeValidationDeadline_ = 0;
    ResetSoftValidationSamples();
}

void App::ValidateHost()
{
    if (shuttingDown_) return;
    if (softValidationPending_)
    {
        softValidationPending_ = false;
        if (controller_) SetTimer(controller_, kValidationTimer, kNormalValidationInterval, nullptr);
    }
    if (reattachPending_)
    {
        const ULONGLONG now = GetTickCount64();
        if (now < nextReattachAttempt_) return;
        taskbarWindow_.Destroy();
        currentProbe_ = {};
        ++reattachAttempts_;
        std::wstring error;
        if (AttachToTaskbar(error))
        {
            reattachPending_ = false;
            reattachAttempts_ = 0;
            UpdatePresentation();
            return;
        }
        if (reattachAttempts_ >= kMaximumReattachAttempts)
        {
            ShowError(error.empty() ? L"无法重新附着主任务栏。" : error, true);
            return;
        }
        nextReattachAttempt_ = now + kRetryReattachDelay;
        return;
    }

    const HWND display = taskbarWindow_.Window();
    if (!display || !IsWindow(display) || !IsWindow(currentProbe_.taskbar)
        || GetParent(display) != currentProbe_.taskbar)
    {
        RequestReattach();
        return;
    }

    const ULONGLONG validationTime = GetTickCount64();
    if (kResumeValidationPolicy.ShouldDefer(validationTime, resumeValidationDeadline_))
    {
        // Explorer can publish Shell_TrayWnd before TrayNotifyWnd and the task
        // switcher hierarchy have settled after resume. Keep the attached
        // child and discard pre-suspend structural/collision samples until the
        // bounded grace period ends; persistent failures then follow the
        // normal validation and reattach path.
        softValidationPending_ = true;
        ResetSoftValidationSamples();
        if (controller_)
            SetTimer(
                controller_,
                kValidationTimer,
                kResumeValidationPolicy.validationIntervalMilliseconds,
                nullptr);
        return;
    }
    resumeValidationDeadline_ = 0;

    if (host_.IsForegroundFullscreen(currentProbe_))
    {
        // Full-screen games and capture overlays can temporarily reshape or hide
        // Shell surfaces. Keep the existing child and let its taskbar parent own
        // visibility; only the background quota state continues to update.
        softValidationPending_ = true;
        structuralChangeSamples_ = 0;
        externalCollisionState_ = {};
        trayReclaimState_ = {};
        trayReclaimArmed_ = false;
        nextExternalCollisionSample_ = 0;
        structuralCandidateValid_ = false;
        return;
    }

    RECT taskbarRect{};
    RECT notificationRect{};
    const bool hasTaskbarRect = GetWindowRect(currentProbe_.taskbar, &taskbarRect) != FALSE;
    const bool hasNotificationRect = IsWindow(currentProbe_.notificationArea)
        && GetWindowRect(currentProbe_.notificationArea, &notificationRect) != FALSE;
    if (hasTaskbarRect
        && IsTaskbarTemporarilyHidden(currentProbe_.taskbar, taskbarRect, currentProbe_.dpi))
    {
        // Explorer owns the child visibility while an auto-hidden taskbar is
        // off-screen. Keep the existing HWND and baseline so it slides back in
        // with the same parent and coordinates when Explorer restores it.
        softValidationPending_ = true;
        structuralChangeSamples_ = 0;
        externalCollisionState_ = {};
        trayReclaimState_ = {};
        trayReclaimArmed_ = false;
        nextExternalCollisionSample_ = 0;
        structuralCandidateValid_ = false;
        return;
    }

    const UINT currentDpi = GetDpiForWindow(currentProbe_.taskbar);
    const LONG tolerance = GeometryTolerance(currentProbe_.dpi);
    const bool structureChanged = !hasTaskbarRect || !hasNotificationRect
        || currentDpi != currentProbe_.dpi
        || !RectApproximatelyEqual(taskbarRect, currentProbe_.taskbarRect, tolerance)
        || host_.HasLightweightStructureChanged(currentProbe_);
    if (structureChanged)
    {
        // A structural sample interrupts the external-obstacle sequence. Do
        // not let collision confirmations survive across a different layout.
        externalCollisionState_ = {};
        trayReclaimState_ = {};
        trayReclaimArmed_ = false;
        nextExternalCollisionSample_ = 0;
        const bool sameCandidate = structuralCandidateValid_
            && currentDpi == structuralDpiCandidate_
            && RectApproximatelyEqual(taskbarRect, structuralTaskbarCandidate_, tolerance);
        if (sameCandidate)
        {
            ++structuralChangeSamples_;
        }
        else
        {
            structuralCandidateValid_ = true;
            structuralTaskbarCandidate_ = taskbarRect;
            structuralDpiCandidate_ = currentDpi;
            structuralChangeSamples_ = 1;
        }
        if (structuralChangeSamples_ >= 2) RequestReattach();
        return;
    }

    structuralChangeSamples_ = 0;
    structuralCandidateValid_ = false;
    softValidationPending_ = false;

    TaskbarProbeResult externalSample = currentProbe_;
    const bool hasSafeSpace = host_.RefreshExternalLayout(externalSample);
    std::wstring placementError;
    if (hasSafeSpace && host_.ValidatePlacement(display, externalSample, placementError))
    {
        // A temporary system indicator (for example the microphone button) can
        // expand TrayNotifyWnd and push us left. Once that system-owned boundary
        // contracts, reclaim the rightmost position only after the target has
        // stayed unchanged for a while. External obstacle removal alone does not
        // arm this path, so TrafficMonitor-style widgets still cannot make us
        // bounce right.
        if (externalSample.notificationRect.left
            > currentProbe_.notificationRect.left + tolerance)
        {
            trayReclaimArmed_ = true;
            trayReclaimState_ = {};
        }
        else if (externalSample.notificationRect.left
            < currentProbe_.notificationRect.left - tolerance)
        {
            trayReclaimArmed_ = false;
            trayReclaimState_ = {};
        }

        RECT childRect{};
        std::optional<HorizontalInterval> reclaimTarget;
        bool canReclaim = false;
        if (trayReclaimArmed_ && GetWindowRect(display, &childRect))
        {
            const LONG childWidth = childRect.right - childRect.left;
            if (childWidth > 0 && externalSample.safeRect.right - externalSample.safeRect.left >= childWidth)
            {
                reclaimTarget = HorizontalInterval{
                    externalSample.safeRect.right - childWidth,
                    externalSample.safeRect.right};
                canReclaim = reclaimTarget->left > childRect.left + tolerance;
            }
        }

        const StableReclaimDecision reclaimDecision = ObserveStableReclaimSample(
            canReclaim,
            reclaimTarget,
            tolerance,
            GetTickCount64(),
            kTrayReclaimStableDelay,
            trayReclaimState_);
        if (reclaimDecision == StableReclaimDecision::Confirmed)
        {
            // Reclaim is optional: if the light-weight move races with another
            // Shell change, retain the already-valid placement and retry only
            // after a future notification-area contraction.
            static_cast<void>(host_.RepositionToRightmostSafeRect(
                display, externalSample, placementError, kTaskbarDisplayWidthDip));
            trayReclaimArmed_ = false;
            trayReclaimState_ = {};
            if (controller_)
                SetTimer(controller_, kValidationTimer, kNormalValidationInterval, nullptr);
        }
        else if (reclaimDecision == StableReclaimDecision::Clear)
        {
            trayReclaimArmed_ = false;
            if (controller_)
                SetTimer(controller_, kValidationTimer, kNormalValidationInterval, nullptr);
        }
        else if (controller_)
        {
            // Poll quickly only while a system-notification recovery target is
            // being confirmed. Normal steady-state validation remains 2 seconds.
            SetTimer(controller_, kValidationTimer, kFastValidationInterval, nullptr);
        }

        // Accept the latest obstacle snapshot while retaining the current HWND.
        currentProbe_ = std::move(externalSample);
        externalCollisionState_ = {};
        nextExternalCollisionSample_ = 0;
        return;
    }

    if (trayReclaimArmed_ || trayReclaimState_.stableSince)
    {
        trayReclaimArmed_ = false;
        trayReclaimState_ = {};
        if (controller_)
            SetTimer(controller_, kValidationTimer, kNormalValidationInterval, nullptr);
    }

    const ULONGLONG collisionSampleTime = GetTickCount64();
    if (collisionSampleTime < nextExternalCollisionSample_) return;
    nextExternalCollisionSample_ = collisionSampleTime + 2000;
    const std::optional<HorizontalInterval> sampledSafeInterval = hasSafeSpace
        ? std::optional<HorizontalInterval>{{externalSample.safeRect.left, externalSample.safeRect.right}}
        : std::nullopt;
    const StableCollisionDecision collisionDecision = ObserveCollisionSample(
        true, sampledSafeInterval, tolerance, 3, externalCollisionState_);
    if (collisionDecision != StableCollisionDecision::Confirmed) return;

    externalCollisionState_ = {};
    trayReclaimState_ = {};
    trayReclaimArmed_ = false;
    nextExternalCollisionSample_ = 0;
    if (!hasSafeSpace)
    {
        ShowWindow(display, SW_HIDE);
        ShowError(
            externalSample.reason.empty()
                ? L"第三方任务栏窗口之后没有足够的连续安全空白。"
                : externalSample.reason,
            true);
        return;
    }

    if (!host_.RepositionWithinSafeRect(display, externalSample, placementError, kTaskbarDisplayWidthDip))
    {
        ShowError(
            placementError.empty() ? L"无法在任务栏安全空白区稳定定位额度窗口。" : placementError,
            true);
        return;
    }
    currentProbe_ = std::move(externalSample);
}

void App::HandleContextMenu(int x, int y)
{
    if (shuttingDown_ || !taskbarWindow_.Window()) return;
    if (x == -1 && y == -1)
    {
        RECT rect{}; GetWindowRect(taskbarWindow_.Window(), &rect); x = rect.left; y = rect.top;
    }
    const UINT command = ContextMenu::Show(
        taskbarWindow_.Window(), x, y, settings_, StartupManager::IsEnabled(ExecutablePath()));
    HandleCommand(command);
}

void App::HandleCommand(UINT command)
{
    bool settingsChanged = false;
    switch (command)
    {
    case CommandRefresh: static_cast<void>(refreshController_.RequestManualRefresh()); break;
    case CommandLayoutVertical: settings_.layout = LayoutMode::Vertical; settingsChanged = true; break;
    case CommandLayoutHorizontal: settings_.layout = LayoutMode::Horizontal; settingsChanged = true; break;
    case CommandShowFiveHour:
        if (settings_.showWeekly || !settings_.showFiveHour) { settings_.showFiveHour = !settings_.showFiveHour; settingsChanged = true; }
        break;
    case CommandShowWeekly:
        if (settings_.showFiveHour || !settings_.showWeekly) { settings_.showWeekly = !settings_.showWeekly; settingsChanged = true; }
        break;
    case CommandShowSingleQuotaLabel:
        settingsChanged = ToggleSingleQuotaLabel(settings_);
        break;
    case CommandInterval60: settings_.refreshIntervalSeconds = 60; settingsChanged = true; break;
    case CommandInterval180: settings_.refreshIntervalSeconds = 180; settingsChanged = true; break;
    case CommandInterval300: settings_.refreshIntervalSeconds = 300; settingsChanged = true; break;
    case CommandInterval600: settings_.refreshIntervalSeconds = 600; settingsChanged = true; break;
    case CommandInterval1800: settings_.refreshIntervalSeconds = 1800; settingsChanged = true; break;
    case CommandColorSystem: settings_.colorMode = ColorMode::System; settingsChanged = true; break;
    case CommandColorWhite: settings_.colorMode = ColorMode::White; settingsChanged = true; break;
    case CommandColorBlack: settings_.colorMode = ColorMode::Black; settingsChanged = true; break;
    case CommandColorQuotaAware: settings_.colorMode = ColorMode::QuotaAware; settingsChanged = true; break;
    case CommandStartup:
    {
        std::wstring error;
        const std::filesystem::path executable = ExecutablePath();
        if (!StartupManager::SetEnabled(!StartupManager::IsEnabled(executable), executable, error))
            MessageBoxW(taskbarWindow_.Window(), error.c_str(), L"CodexQuotaTaskbar", MB_OK | MB_ICONERROR);
        break;
    }
    case CommandRevalidate: RequestReattach(); break;
    case CommandAbout:
        MessageBoxW(taskbarWindow_.Window(), L"CodexQuotaTaskbar v" CQT_VERSION_W L"\r\n\r\n"
            L"只读显示当前用户 Codex 额度，不收集遥测。", L"关于", MB_OK | MB_ICONINFORMATION);
        break;
    case CommandExit: BeginShutdown(); return;
    default: break;
    }
    if (settingsChanged)
    {
        settings_ = Settings::Normalize(settings_);
        refreshController_.SetRefreshInterval(settings_.refreshIntervalSeconds);
        SaveSettings();
        // Display mode and content changes keep the same taskbar rectangle, so
        // repaint in place. Reattaching would hide the window for the bounded
        // host-recovery delay even though the Explorer structure did not change.
        UpdatePresentation();
    }
}

void App::ApplyRefreshResult()
{
    if (shuttingDown_) return;
    std::optional<RefreshResult> result = refreshController_.TakePendingResult();
    if (!result) return;
    state_.latestUsageAttempt = result->usage;
    state_.latestResetCreditsAttempt = result->resetCredits;
    if (result->usage.success)
    {
        state_.lastSuccessfulUsage = result->usage;
        state_.hasSuccessfulUsageData = true;
    }
    if (result->resetCredits.success)
    {
        state_.lastSuccessfulResetCredits = result->resetCredits;
        state_.hasSuccessfulResetCreditsData = true;
    }
    UpdatePresentation();
}

void App::UpdatePresentation()
{
    if (!taskbarWindow_.Window()) return;
    state_.refreshing = refreshController_.IsRefreshing();
    taskbarWindow_.Update(
        BuildTaskbarRenderModel(state_, settings_),
        BuildTooltipText(state_, authPaths_, UnixNow()));
}

void App::SaveSettings()
{
    std::wstring error;
    if (!settingsPath_.empty() && !Settings::Save(settingsPath_, settings_, error))
        MessageBoxW(taskbarWindow_.Window(), error.c_str(), L"CodexQuotaTaskbar", MB_OK | MB_ICONERROR);
}

void App::ShowError(const std::wstring& message, bool exitAfter)
{
    if (exitAfter && errorShown_) return;
    if (exitAfter) errorShown_ = true;
    MessageBoxW(nullptr, message.c_str(), L"CodexQuotaTaskbar", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    if (exitAfter) BeginShutdown();
}

void App::BeginShutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;
    if (controller_)
    {
        KillTimer(controller_, kCountdownTimer);
        KillTimer(controller_, kValidationTimer);
    }
    authWatcher_.Stop();
    refreshController_.Stop();
    host_.StopEventMonitoring();
    taskbarWindow_.Destroy();
    if (controller_ && IsWindow(controller_))
    {
        HWND window = controller_;
        controller_ = nullptr;
        DestroyWindow(window);
    }
}

void App::Cleanup()
{
    BeginShutdown();
    if (comInitialized_)
    {
        CoUninitialize();
        comInitialized_ = false;
    }
}

std::filesystem::path App::ExecutablePath() const
{
    std::wstring buffer(32768, L'\0');
    const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(written);
    return buffer;
}

} // namespace cqt
