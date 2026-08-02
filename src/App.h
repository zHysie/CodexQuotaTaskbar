#pragma once

#include "common/SingleInstanceGuard.h"
#include "settings/Settings.h"
#include "taskbar/TaskbarCollision.h"
#include "taskbar/TaskbarHost.h"
#include "taskbar/TaskbarWindow.h"
#include "usage/AuthFileWatcher.h"
#include "usage/CodexAuthReader.h"
#include "usage/CodexUsageClient.h"
#include "usage/RefreshController.h"
#include "usage/WinHttpTransport.h"

#include <windows.h>

#include <filesystem>

namespace cqt
{

class App
{
public:
    int Run(HINSTANCE instance);

private:
    static LRESULT CALLBACK ControllerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleControllerMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool RegisterControllerClass();
    [[nodiscard]] bool AttachToTaskbar(std::wstring& error, bool* retryable = nullptr);
    [[nodiscard]] bool WaitForStartupAttachRetry(ULONGLONG delayMilliseconds);
    void RequestReattach();
    void RequestSoftValidation();
    void RequestResumeValidation();
    void ValidateHost();
    void ResetSoftValidationSamples();
    void ResetHostValidationState();
    void HandleContextMenu(int x, int y);
    void HandleCommand(UINT command);
    void ApplyRefreshResult();
    void UpdatePresentation();
    void SaveSettings();
    void ShowError(const std::wstring& message, bool exitAfter);
    void BeginShutdown();
    void Cleanup();
    [[nodiscard]] std::filesystem::path ExecutablePath() const;

    HINSTANCE instance_ = nullptr;
    HWND controller_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    bool comInitialized_ = false;
    bool shuttingDown_ = false;
    bool errorShown_ = false;
    bool reattachPending_ = false;
    bool softValidationPending_ = false;
    ULONGLONG nextReattachAttempt_ = 0;
    ULONGLONG resumeValidationDeadline_ = 0;
    ULONGLONG nextExternalCollisionSample_ = 0;
    int reattachAttempts_ = 0;
    int structuralChangeSamples_ = 0;
    StableCollisionState externalCollisionState_;
    StableReclaimState trayReclaimState_;
    bool trayReclaimArmed_ = false;
    bool structuralCandidateValid_ = false;
    RECT structuralTaskbarCandidate_{};
    UINT structuralDpiCandidate_ = 0;
    SettingsData settings_;
    std::filesystem::path settingsPath_;
    AppState state_;
    AuthSearchPaths authPaths_;
    TaskbarProbeResult currentProbe_;
    SingleInstanceGuard instanceGuard_;
    TaskbarHost host_;
    TaskbarWindow taskbarWindow_;
    CodexAuthReader authReader_;
    WinHttpTransport transport_;
    CodexUsageClient usageClient_{transport_};
    RefreshController refreshController_{authReader_, usageClient_};
    AuthFileWatcher authWatcher_;
};

} // namespace cqt
