#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "settings/Settings.h"

#include <string>
#include <optional>

namespace cqt
{

struct TaskbarRenderModel
{
    std::wstring fiveHour = L"--";
    std::wstring weekly = L"--";
    std::wstring statusText;
    bool showFiveHour = true;
    bool showWeekly = true;
    bool warningMarker = false;
    LayoutMode layout = LayoutMode::Vertical;
    ColorMode colorMode = ColorMode::QuotaAware;
    double fiveHourRemaining = 100.0;
    double weeklyRemaining = 100.0;

    bool operator==(const TaskbarRenderModel&) const = default;
};

class TaskbarRenderer
{
public:
    HRESULT Initialize();
    void DiscardDeviceResources();
    HRESULT Draw(HWND window, UINT dpi);
    HRESULT Draw(HWND window, UINT dpi, const TaskbarRenderModel& model);
    void DrawGdiFallback(HWND window, HDC deviceContext, UINT dpi, const TaskbarRenderModel& model) const;
    void InvalidateTheme() { lightTheme_.reset(); }

private:
    HRESULT EnsureDeviceResources(HWND window);
    HRESULT CreateCompositionResources(HWND window, D2D1_SIZE_U size);
    HRESULT ResizeCompositionSurface(D2D1_SIZE_U size);
    HRESULT BindCompositionTargetBitmap();
    HRESULT EnsureTextFormat(UINT dpi, LONG clientHeight);
    static bool QuerySystemUsesLightTheme();
    [[nodiscard]] bool SystemUsesLightTheme() const;

    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> compositionVisual_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> valueBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> compactTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> narrowTextFormat_;
    UINT formatDpi_ = 0;
    LONG formatHeight_ = 0;
    HWND compositionWindow_ = nullptr;
    D2D1_SIZE_U surfaceSize_{};
    mutable std::optional<bool> lightTheme_;
};

} // namespace cqt
