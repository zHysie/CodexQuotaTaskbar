#pragma once

#include "taskbar/TooltipController.h"
#include "ui/TaskbarRenderer.h"

#include <windows.h>

#include <functional>

namespace cqt
{

class RenderRecoveryState
{
public:
    [[nodiscard]] bool NextDelay(UINT& delayMilliseconds) noexcept;
    void Reset() noexcept { attempt_ = 0; }
    [[nodiscard]] unsigned int Attempts() const noexcept { return attempt_; }

private:
    unsigned int attempt_ = 0;
};

class TaskbarWindow
{
public:
    using ContextHandler = std::function<void(int, int)>;

    [[nodiscard]] bool Initialize(HINSTANCE instance, ContextHandler contextHandler);
    [[nodiscard]] bool Create();
    void Destroy();
    void Update(TaskbarRenderModel model, std::wstring tooltip);
    void HideTooltip() { tooltip_.Hide(); }
    [[nodiscard]] HWND Window() const noexcept { return window_; }
    [[nodiscard]] const TaskbarRenderModel& Model() const noexcept { return model_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    ContextHandler contextHandler_;
    TaskbarRenderer renderer_;
    TooltipController tooltip_;
    TaskbarRenderModel model_;
    RenderRecoveryState renderRecovery_;
};

} // namespace cqt
