#include "ui/TaskbarRenderer.h"
#include "ui/QuotaLayout.h"

#include <d2d1helper.h>

#include <algorithm>
#include <iterator>

namespace cqt
{

HRESULT TaskbarRenderer::Initialize()
{
    D2D1_FACTORY_OPTIONS options{};
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &options,
        reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        return hr;
    }

    return DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
}

void TaskbarRenderer::DiscardDeviceResources()
{
    if (deviceContext_) deviceContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
    textBrush_.Reset();
    valueBrush_.Reset();
    compositionVisual_.Reset();
    compositionTarget_.Reset();
    compositionDevice_.Reset();
    swapChain_.Reset();
    deviceContext_.Reset();
    d2dDevice_.Reset();
    dxgiDevice_.Reset();
    d3dDevice_.Reset();
    compositionWindow_ = nullptr;
    surfaceSize_ = {};
}

HRESULT TaskbarRenderer::EnsureDeviceResources(HWND window)
{
    RECT client{};
    GetClientRect(window, &client);
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
        static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));

    if (!deviceContext_ || compositionWindow_ != window)
    {
        DiscardDeviceResources();
        return CreateCompositionResources(window, size);
    }
    if (surfaceSize_.width != size.width || surfaceSize_.height != size.height)
    {
        return ResizeCompositionSurface(size);
    }
    return S_OK;
}

HRESULT TaskbarRenderer::CreateCompositionResources(HWND window, D2D1_SIZE_U size)
{
    constexpr UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL featureLevels[]{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        deviceFlags,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        d3dDevice_.ReleaseAndGetAddressOf(),
        &featureLevel,
        nullptr);
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            deviceFlags,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            d3dDevice_.ReleaseAndGetAddressOf(),
            &featureLevel,
            nullptr);
    }
    if (SUCCEEDED(hr)) hr = d3dDevice_.As(&dxgiDevice_);
    if (SUCCEEDED(hr)) hr = d2dFactory_->CreateDevice(dxgiDevice_.Get(), d2dDevice_.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr))
    {
        hr = d2dDevice_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
            deviceContext_.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(hr))
    {
        hr = DCompositionCreateDevice(
            dxgiDevice_.Get(),
            __uuidof(IDCompositionDevice),
            reinterpret_cast<void**>(compositionDevice_.ReleaseAndGetAddressOf()));
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (SUCCEEDED(hr)) hr = dxgiDevice_->GetAdapter(adapter.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr))
    {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = size.width;
        description.Height = size.height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = factory->CreateSwapChainForComposition(
            d3dDevice_.Get(),
            &description,
            nullptr,
            swapChain_.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(hr))
        hr = compositionDevice_->CreateTargetForHwnd(window, TRUE, compositionTarget_.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) hr = compositionDevice_->CreateVisual(compositionVisual_.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) hr = compositionVisual_->SetContent(swapChain_.Get());
    if (SUCCEEDED(hr)) hr = compositionTarget_->SetRoot(compositionVisual_.Get());
    if (SUCCEEDED(hr)) hr = compositionDevice_->Commit();
    if (SUCCEEDED(hr))
    {
        deviceContext_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        hr = BindCompositionTargetBitmap();
    }
    if (SUCCEEDED(hr))
        hr = deviceContext_->CreateSolidColorBrush(
            D2D1::ColorF(1.0F, 1.0F, 1.0F), textBrush_.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr))
        hr = deviceContext_->CreateSolidColorBrush(
            D2D1::ColorF(1.0F, 1.0F, 1.0F), valueBrush_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DiscardDeviceResources();
        return hr;
    }
    compositionWindow_ = window;
    surfaceSize_ = size;
    return S_OK;
}

HRESULT TaskbarRenderer::ResizeCompositionSurface(D2D1_SIZE_U size)
{
    deviceContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
    HRESULT hr = swapChain_->ResizeBuffers(
        0,
        size.width,
        size.height,
        DXGI_FORMAT_UNKNOWN,
        0);
    if (SUCCEEDED(hr)) hr = BindCompositionTargetBitmap();
    if (FAILED(hr))
    {
        DiscardDeviceResources();
        return hr;
    }
    surfaceSize_ = size;
    return S_OK;
}

HRESULT TaskbarRenderer::BindCompositionTargetBitmap()
{
    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(surface.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return hr;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0F,
        96.0F);
    hr = deviceContext_->CreateBitmapFromDxgiSurface(
        surface.Get(),
        &properties,
        targetBitmap_.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) deviceContext_->SetTarget(targetBitmap_.Get());
    return hr;
}

HRESULT TaskbarRenderer::EnsureTextFormat(UINT dpi, LONG clientHeight)
{
    if (textFormat_ && compactTextFormat_ && narrowTextFormat_
        && formatDpi_ == dpi && formatHeight_ == clientHeight)
    {
        return S_OK;
    }

    textFormat_.Reset();
    formatDpi_ = dpi;
    formatHeight_ = clientHeight;

    const TaskbarFontSizes fontSizes = ComputeTaskbarFontSizes(dpi, clientHeight);
    HRESULT hr = dwriteFactory_->CreateTextFormat(
        L"Microsoft YaHei UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSizes.vertical,
        L"zh-CN",
        textFormat_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        return hr;
    }

    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    hr = dwriteFactory_->CreateTextFormat(
        L"Microsoft YaHei UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSizes.compact,
        L"zh-CN",
        compactTextFormat_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        textFormat_.Reset();
        return hr;
    }
    compactTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    compactTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    compactTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    hr = dwriteFactory_->CreateTextFormat(
        L"Microsoft YaHei UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSizes.narrow,
        L"zh-CN",
        narrowTextFormat_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        textFormat_.Reset();
        compactTextFormat_.Reset();
        return hr;
    }
    narrowTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    narrowTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    narrowTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return S_OK;
}

bool TaskbarRenderer::QuerySystemUsesLightTheme()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return status != ERROR_SUCCESS || value != 0;
}

bool TaskbarRenderer::SystemUsesLightTheme() const
{
    if (!lightTheme_) lightTheme_ = QuerySystemUsesLightTheme();
    return *lightTheme_;
}

HRESULT TaskbarRenderer::Draw(HWND window, UINT dpi)
{
    TaskbarRenderModel model;
    model.fiveHour = L"85";
    model.weekly = L"72";
    model.fiveHourRemaining = 85.0;
    model.weeklyRemaining = 72.0;
    return Draw(window, dpi, model);
}

HRESULT TaskbarRenderer::Draw(HWND window, UINT dpi, const TaskbarRenderModel& model)
{
    HRESULT hr = EnsureDeviceResources(window);
    if (FAILED(hr))
    {
        return hr;
    }

    RECT client{};
    GetClientRect(window, &client);
    hr = EnsureTextFormat(dpi, client.bottom - client.top);
    if (FAILED(hr))
    {
        return hr;
    }

    const bool lightTheme = SystemUsesLightTheme();
    // DirectComposition blends this premultiplied surface with Explorer. True
    // zero alpha leaves every non-glyph pixel completely transparent while the
    // ordinary child HWND still owns the full rectangular input region.
    const D2D1_COLOR_F background = D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F);
    textBrush_->SetColor(lightTheme
            ? D2D1::ColorF(0.10F, 0.10F, 0.10F, 1.0F)
            : D2D1::ColorF(0.94F, 0.94F, 0.94F, 1.0F));

    auto systemColor = lightTheme ? D2D1::ColorF(0.10F, 0.10F, 0.10F, 1.0F)
                                  : D2D1::ColorF(0.94F, 0.94F, 0.94F, 1.0F);
    if (model.colorMode == ColorMode::White) systemColor = D2D1::ColorF(1.0F, 1.0F, 1.0F, 1.0F);
    if (model.colorMode == ColorMode::Black) systemColor = D2D1::ColorF(0.0F, 0.0F, 0.0F, 1.0F);
    textBrush_->SetColor(systemColor);

    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    const float warningWidth = kWarningWidthDip
        * static_cast<float>(std::max<UINT>(dpi, 1)) / 96.0F;
    const float contentRight = std::max(1.0F, width - (model.warningMarker ? warningWidth : 0.0F));
    const D2D1_RECT_F firstLine = D2D1::RectF(0.0F, 0.0F, contentRight, height / 2.0F + 1.0F);
    const D2D1_RECT_F secondLine = D2D1::RectF(0.0F, height / 2.0F - 1.0F, contentRight, height);

    // Keep the label, value, and percent sign in stable columns. The number is
    // right-aligned and the percent sign is left-aligned around the shared
    // boundary, so they stay close without losing vertical alignment.
    const QuotaColumnLayout columns = ComputeQuotaColumnLayout(contentRight);
    const auto quotaColor = [&](double remaining) {
        if (model.colorMode != ColorMode::QuotaAware) return systemColor;
        if (remaining <= 10.0) return D2D1::ColorF(0.95F, 0.22F, 0.18F, 1.0F);
        if (remaining <= 20.0) return D2D1::ColorF(1.0F, 0.65F, 0.05F, 1.0F);
        return systemColor;
    };
    const auto drawQuotaLine = [&](const D2D1_RECT_F& line, const wchar_t* label, UINT32 labelLength,
                                   const std::wstring& value, double remaining) {
        const D2D1_RECT_F labelColumn = D2D1::RectF(
            columns.contentLeft, line.top, columns.labelRight, line.bottom);
        constexpr float valuePercentGap = 1.0F;
        const D2D1_RECT_F valueColumn = D2D1::RectF(
            columns.labelRight, line.top, columns.valueRight - valuePercentGap, line.bottom);
        const D2D1_RECT_F percentColumn = D2D1::RectF(
            columns.valueRight + valuePercentGap, line.top, columns.contentRight, line.bottom);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        deviceContext_->DrawTextW(label, labelLength, textFormat_.Get(), labelColumn, textBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        valueBrush_->SetColor(quotaColor(remaining));
        deviceContext_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), textFormat_.Get(), valueColumn, valueBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        deviceContext_->DrawTextW(L"%", 1, textFormat_.Get(), percentColumn, valueBrush_.Get());
    };

    deviceContext_->BeginDraw();
    deviceContext_->Clear(background);
    if (!model.statusText.empty())
    {
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        deviceContext_->DrawTextW(model.statusText.c_str(), static_cast<UINT32>(model.statusText.size()),
            textFormat_.Get(), D2D1::RectF(0.0F, 0.0F, contentRight, height), textBrush_.Get());
    }
    else if (UseCompactHorizontalQuotaLayout(
                 model.layout == LayoutMode::Horizontal,
                 model.showFiveHour,
                 model.showWeekly,
                 !model.statusText.empty()))
    {
        const float scale = static_cast<float>(std::max<UINT>(dpi, 1)) / 96.0F;
        const bool needsNarrowFormat = model.warningMarker
            || model.fiveHour.size() >= 3 || model.weekly.size() >= 3;
        IDWriteTextFormat* dualItemFormat = needsNarrowFormat
            ? narrowTextFormat_.Get() : compactTextFormat_.Get();
        const auto drawHorizontalItem = [&](float left, float right, const wchar_t* label,
                                            const std::wstring& value, double remaining,
                                            IDWriteTextFormat* itemFormat) {
            const CompactQuotaItemLayout item = ComputeCompactQuotaItemLayout(left, right, scale);
            const D2D1_RECT_F labelRect = D2D1::RectF(
                item.labelLeft, 0.0F, item.labelRight, height);
            const D2D1_RECT_F valueRect = D2D1::RectF(
                item.valueLeft, 0.0F, item.valueRight, height);
            itemFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            deviceContext_->DrawTextW(label, 2, itemFormat, labelRect, textBrush_.Get());
            valueBrush_->SetColor(quotaColor(remaining));
            const std::wstring percentage = value + L"%";
            itemFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            deviceContext_->DrawTextW(percentage.c_str(), static_cast<UINT32>(percentage.size()),
                                      itemFormat, valueRect, valueBrush_.Get());
        };
        const float middle = contentRight / 2.0F;
        constexpr float outerPadding = 0.0F;
        const float separatorHalfWidth = scale;
        drawHorizontalItem(outerPadding, middle - separatorHalfWidth,
                           L"5h", model.fiveHour, model.fiveHourRemaining, dualItemFormat);
        dualItemFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        deviceContext_->DrawTextW(L"|", 1, dualItemFormat,
            D2D1::RectF(middle - separatorHalfWidth, 0.0F,
                        middle + separatorHalfWidth, height), textBrush_.Get());
        drawHorizontalItem(middle + separatorHalfWidth, contentRight - outerPadding,
                           L"1W", model.weekly, model.weeklyRemaining, dualItemFormat);
    }
    else if (model.showFiveHour && model.showWeekly)
    {
        drawQuotaLine(firstLine, L"5h", 2, model.fiveHour, model.fiveHourRemaining);
        drawQuotaLine(secondLine, L"1W", 2, model.weekly, model.weeklyRemaining);
    }
    else if (model.showFiveHour)
    {
        drawQuotaLine(D2D1::RectF(0.0F, 0.0F, width, height), L"5h", 2,
            model.fiveHour, model.fiveHourRemaining);
    }
    else
    {
        drawQuotaLine(D2D1::RectF(0.0F, 0.0F, width, height), L"1W", 2,
            model.weekly, model.weeklyRemaining);
    }
    if (model.warningMarker)
    {
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        deviceContext_->DrawTextW(L"!", 1, textFormat_.Get(),
            D2D1::RectF(contentRight, 0.0F, width, height), textBrush_.Get());
    }
    hr = deviceContext_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
        return hr;
    }
    if (SUCCEEDED(hr)) hr = swapChain_->Present(1, 0);
    if (FAILED(hr)) DiscardDeviceResources();
    return hr;
}

void TaskbarRenderer::DrawGdiFallback(HWND window, HDC dc, UINT dpi, const TaskbarRenderModel& model) const
{
    RECT client{};
    GetClientRect(window, &client);
    SetBkMode(dc, TRANSPARENT);
    COLORREF baseColor = SystemUsesLightTheme() ? RGB(26, 26, 26) : RGB(240, 240, 240);
    if (model.colorMode == ColorMode::White) baseColor = RGB(255, 255, 255);
    if (model.colorMode == ColorMode::Black) baseColor = RGB(0, 0, 0);
    SetTextColor(dc, baseColor);
    const bool compactHorizontalData = UseCompactHorizontalQuotaLayout(
        model.layout == LayoutMode::Horizontal,
        model.showFiveHour,
        model.showWeekly,
        !model.statusText.empty());
    const bool narrowHorizontalData = compactHorizontalData
        && (model.warningMarker || model.fiveHour.size() >= 3 || model.weekly.size() >= 3);
    int fontSizePoints = 10;
    if (compactHorizontalData) fontSizePoints = narrowHorizontalData ? 6 : 7;
    HFONT font = CreateFontW(-MulDiv(fontSizePoints, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ previous = SelectObject(dc, font);
    constexpr UINT common = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    const LONG warningWidth = MulDiv(
        static_cast<int>(kWarningWidthDip), static_cast<int>(std::max<UINT>(dpi, 1)), 96);
    RECT content = client;
    if (model.warningMarker) content.right = std::max(content.left + 1, content.right - warningWidth);
    if (!model.statusText.empty())
    {
        DrawTextW(dc, model.statusText.c_str(), static_cast<int>(model.statusText.size()),
                  &content, common | DT_CENTER);
    }
    else if (compactHorizontalData)
    {
        const float scale = static_cast<float>(std::max<UINT>(dpi, 1)) / 96.0F;
        const auto valueColor = [&](double remaining) {
            if (model.colorMode != ColorMode::QuotaAware) return baseColor;
            if (remaining <= 10.0) return RGB(242, 56, 46);
            if (remaining <= 20.0) return RGB(255, 166, 13);
            return baseColor;
        };
        const auto drawHorizontalItem = [&](float left, float right, const wchar_t* label,
                                            const std::wstring& value, double remaining) {
            const CompactQuotaItemLayout item = ComputeCompactQuotaItemLayout(left, right, scale);
            RECT labelRect{static_cast<LONG>(item.labelLeft), content.top,
                           static_cast<LONG>(item.labelRight), content.bottom};
            RECT valueRect{static_cast<LONG>(item.valueLeft), content.top,
                           static_cast<LONG>(item.valueRight), content.bottom};
            SetTextColor(dc, baseColor);
            DrawTextW(dc, label, 2, &labelRect, common | DT_RIGHT);
            const std::wstring percentage = value + L"%";
            SetTextColor(dc, valueColor(remaining));
            DrawTextW(dc, percentage.c_str(), static_cast<int>(percentage.size()),
                      &valueRect, common | DT_LEFT);
        };
        const float middle = static_cast<float>(content.right) / 2.0F;
        constexpr float outerPadding = 0.0F;
        const float separatorHalfWidth = scale;
        drawHorizontalItem(outerPadding, middle - separatorHalfWidth,
                           L"5h", model.fiveHour, model.fiveHourRemaining);
        RECT separator{static_cast<LONG>(middle - separatorHalfWidth), content.top,
                       static_cast<LONG>(middle + separatorHalfWidth), content.bottom};
        SetTextColor(dc, baseColor);
        DrawTextW(dc, L"|", 1, &separator, common | DT_CENTER);
        drawHorizontalItem(middle + separatorHalfWidth,
                           static_cast<float>(content.right) - outerPadding,
                           L"1W", model.weekly, model.weeklyRemaining);
    }
    else
    {
        const QuotaColumnLayout columns = ComputeQuotaColumnLayout(static_cast<float>(content.right));
        const auto drawLine = [&](RECT line, const wchar_t* label, const std::wstring& value, double remaining) {
            RECT labelRect{static_cast<LONG>(columns.contentLeft), line.top,
                           static_cast<LONG>(columns.labelRight), line.bottom};
            RECT valueRect{static_cast<LONG>(columns.labelRight), line.top,
                           static_cast<LONG>(columns.valueRight) - 1, line.bottom};
            RECT percentRect{static_cast<LONG>(columns.valueRight) + 1, line.top,
                             static_cast<LONG>(columns.contentRight), line.bottom};
            SetTextColor(dc, baseColor);
            DrawTextW(dc, label, -1, &labelRect, common | DT_CENTER);
            COLORREF valueColor = baseColor;
            if (model.colorMode == ColorMode::QuotaAware && remaining <= 10.0) valueColor = RGB(242, 56, 46);
            else if (model.colorMode == ColorMode::QuotaAware && remaining <= 20.0) valueColor = RGB(255, 166, 13);
            SetTextColor(dc, valueColor);
            DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &valueRect, common | DT_RIGHT);
            DrawTextW(dc, L"%", 1, &percentRect, common | DT_LEFT);
        };
        if (model.showFiveHour && model.showWeekly)
        {
            RECT first = content; first.bottom = (content.top + content.bottom) / 2;
            RECT second = content; second.top = first.bottom;
            drawLine(first, L"5h", model.fiveHour, model.fiveHourRemaining);
            drawLine(second, L"1W", model.weekly, model.weeklyRemaining);
        }
        else if (model.showFiveHour) drawLine(content, L"5h", model.fiveHour, model.fiveHourRemaining);
        else drawLine(content, L"1W", model.weekly, model.weeklyRemaining);
    }
    if (model.warningMarker)
    {
        RECT warning{content.right, client.top, client.right, client.bottom};
        SetTextColor(dc, baseColor);
        DrawTextW(dc, L"!", 1, &warning, common | DT_CENTER);
    }
    SelectObject(dc, previous);
    DeleteObject(font);
}

} // namespace cqt
