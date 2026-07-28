#include "taskbar/TaskbarWindow.h"
#include "ui/QuotaLayout.h"

#include <commctrl.h>
#include <dwrite.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string_view>

namespace
{

int failures = 0;

void Check(bool condition, const char* description)
{
    if (!condition)
    {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}

float MeasureText(IDWriteFactory* factory, std::wstring_view text, float fontSize)
{
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            fontSize, L"zh-CN", format.ReleaseAndGetAddressOf())))
    {
        return -1.0F;
    }
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(
            text.data(), static_cast<UINT32>(text.size()), format.Get(),
            1000.0F, 1000.0F, layout.ReleaseAndGetAddressOf())))
    {
        return -1.0F;
    }
    DWRITE_TEXT_METRICS metrics{};
    return SUCCEEDED(layout->GetMetrics(&metrics))
        ? metrics.widthIncludingTrailingWhitespace : -1.0F;
}

std::size_t CountVisiblePixels(const cqt::GdiFallbackFrame& frame, RECT bounds)
{
    bounds.left = std::clamp<LONG>(bounds.left, 0, static_cast<LONG>(frame.width));
    bounds.top = std::clamp<LONG>(bounds.top, 0, static_cast<LONG>(frame.height));
    bounds.right = std::clamp<LONG>(bounds.right, bounds.left, static_cast<LONG>(frame.width));
    bounds.bottom = std::clamp<LONG>(bounds.bottom, bounds.top, static_cast<LONG>(frame.height));
    std::size_t count = 0;
    for (LONG y = bounds.top; y < bounds.bottom; ++y)
    {
        for (LONG x = bounds.left; x < bounds.right; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * frame.width
                + static_cast<std::size_t>(x);
            if ((frame.bgraPremultiplied[index] >> 24U) != 0) ++count;
        }
    }
    return count;
}

bool FramesMatchOutsideRect(
    const cqt::GdiFallbackFrame& left,
    const cqt::GdiFallbackFrame& right,
    RECT excluded)
{
    if (left.width != right.width || left.height != right.height
        || left.bgraPremultiplied.size() != right.bgraPremultiplied.size())
    {
        return false;
    }
    for (UINT y = 0; y < left.height; ++y)
    {
        for (UINT x = 0; x < left.width; ++x)
        {
            if (static_cast<LONG>(x) >= excluded.left && static_cast<LONG>(x) < excluded.right
                && static_cast<LONG>(y) >= excluded.top && static_cast<LONG>(y) < excluded.bottom)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(y) * left.width + x;
            if (left.bgraPremultiplied[index] != right.bgraPremultiplied[index]) return false;
        }
    }
    return true;
}

bool HasRedWarningPixel(const cqt::GdiFallbackFrame& frame, RECT bounds)
{
    bounds.left = std::clamp<LONG>(bounds.left, 0, static_cast<LONG>(frame.width));
    bounds.top = std::clamp<LONG>(bounds.top, 0, static_cast<LONG>(frame.height));
    bounds.right = std::clamp<LONG>(bounds.right, bounds.left, static_cast<LONG>(frame.width));
    bounds.bottom = std::clamp<LONG>(bounds.bottom, bounds.top, static_cast<LONG>(frame.height));
    for (LONG y = bounds.top; y < bounds.bottom; ++y)
    {
        for (LONG x = bounds.left; x < bounds.right; ++x)
        {
            const std::uint32_t pixel = frame.bgraPremultiplied[
                static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)];
            const unsigned blue = pixel & 0xFFU;
            const unsigned green = (pixel >> 8U) & 0xFFU;
            const unsigned red = (pixel >> 16U) & 0xFFU;
            if (red > green && red > blue) return true;
        }
    }
    return false;
}

bool IsPremultipliedAlpha(const cqt::GdiFallbackFrame& frame)
{
    for (const std::uint32_t pixel : frame.bgraPremultiplied)
    {
        const unsigned alpha = (pixel >> 24U) & 0xFFU;
        if ((pixel & 0xFFU) > alpha || ((pixel >> 8U) & 0xFFU) > alpha
            || ((pixel >> 16U) & 0xFFU) > alpha)
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    cqt::TaskbarRenderModel labelModel;
    Check(cqt::ShouldDrawQuotaLabels(labelModel),
          "default render model draws quota labels");
    labelModel.showSingleQuotaLabel = false;
    Check(cqt::ShouldDrawQuotaLabels(labelModel),
          "dual quotas force labels when single-quota preference is hidden");
    labelModel.showWeekly = false;
    Check(!cqt::ShouldDrawQuotaLabels(labelModel),
          "DirectWrite and GDI shared rule hides five-hour-only label");
    labelModel.showFiveHour = false;
    labelModel.showWeekly = true;
    Check(!cqt::ShouldDrawQuotaLabels(labelModel),
          "DirectWrite and GDI shared rule hides weekly-only label");
    labelModel.showSingleQuotaLabel = true;
    Check(cqt::ShouldDrawQuotaLabels(labelModel),
          "single quota draws label when preference is enabled");

    constexpr UINT fallbackWidth = cqt::kTaskbarDisplayWidthDip;
    constexpr UINT fallbackHeight = 42;
    cqt::TaskbarRenderModel singleQuotaModel;
    singleQuotaModel.showWeekly = false;
    singleQuotaModel.fiveHour = L"85";
    singleQuotaModel.showSingleQuotaLabel = true;
    const cqt::GdiFallbackFrame visibleLabelFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, singleQuotaModel);
    singleQuotaModel.showSingleQuotaLabel = false;
    const cqt::GdiFallbackFrame hiddenLabelFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, singleQuotaModel);
    const cqt::QuotaColumnLayout fallbackColumns =
        cqt::ComputeQuotaColumnLayout(static_cast<float>(fallbackWidth));
    const RECT fallbackLabelRect{
        static_cast<LONG>(fallbackColumns.contentLeft),
        0,
        static_cast<LONG>(fallbackColumns.labelRight),
        static_cast<LONG>(fallbackHeight),
    };
    Check(CountVisiblePixels(visibleLabelFrame, fallbackLabelRect) > 0,
          "GDI fallback visible frame rasterizes the single-quota label");
    Check(CountVisiblePixels(hiddenLabelFrame, fallbackLabelRect) == 0,
          "GDI fallback hidden frame clears all pixels from the prior label column");
    Check(FramesMatchOutsideRect(visibleLabelFrame, hiddenLabelFrame, fallbackLabelRect),
          "GDI fallback label toggle leaves value and percent pixels unchanged");
    Check(IsPremultipliedAlpha(visibleLabelFrame) && IsPremultipliedAlpha(hiddenLabelFrame),
          "GDI fallback produces independent premultiplied-alpha frames");

    cqt::TaskbarRenderModel weeklyOnlyModel;
    weeklyOnlyModel.showFiveHour = false;
    weeklyOnlyModel.weekly = L"72";
    weeklyOnlyModel.showSingleQuotaLabel = false;
    const cqt::GdiFallbackFrame hiddenWeeklyLabelFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, weeklyOnlyModel);
    Check(CountVisiblePixels(hiddenWeeklyLabelFrame, fallbackLabelRect) == 0,
          "GDI fallback also hides the weekly-only label");
    weeklyOnlyModel.layout = cqt::LayoutMode::Horizontal;
    const cqt::GdiFallbackFrame horizontalSingleQuotaFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, weeklyOnlyModel);
    Check(horizontalSingleQuotaFrame.bgraPremultiplied
              == hiddenWeeklyLabelFrame.bgraPremultiplied,
          "single-quota GDI layout keeps identical fixed columns in both layout modes");

    cqt::TaskbarRenderModel dualQuotaModel;
    dualQuotaModel.showSingleQuotaLabel = false;
    dualQuotaModel.fiveHour = L"85";
    dualQuotaModel.weekly = L"72";
    const cqt::GdiFallbackFrame dualQuotaFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, dualQuotaModel);
    RECT firstLabelRect = fallbackLabelRect;
    firstLabelRect.bottom = static_cast<LONG>(fallbackHeight / 2);
    RECT secondLabelRect = fallbackLabelRect;
    secondLabelRect.top = firstLabelRect.bottom;
    Check(CountVisiblePixels(dualQuotaFrame, firstLabelRect) > 0
          && CountVisiblePixels(dualQuotaFrame, secondLabelRect) > 0,
          "GDI fallback forces both labels when both quotas are visible");

    dualQuotaModel.layout = cqt::LayoutMode::Horizontal;
    const cqt::GdiFallbackFrame horizontalDualQuotaFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, dualQuotaModel);
    const float horizontalMiddle = static_cast<float>(fallbackWidth) / 2.0F;
    const cqt::CompactQuotaItemLayout firstHorizontalItem =
        cqt::ComputeCompactQuotaItemLayout(0.0F, horizontalMiddle - 1.0F);
    const cqt::CompactQuotaItemLayout secondHorizontalItem =
        cqt::ComputeCompactQuotaItemLayout(horizontalMiddle + 1.0F,
                                           static_cast<float>(fallbackWidth));
    const RECT firstHorizontalLabelRect{
        static_cast<LONG>(firstHorizontalItem.labelLeft),
        0,
        static_cast<LONG>(firstHorizontalItem.labelRight),
        static_cast<LONG>(fallbackHeight),
    };
    const RECT secondHorizontalLabelRect{
        static_cast<LONG>(secondHorizontalItem.labelLeft),
        0,
        static_cast<LONG>(secondHorizontalItem.labelRight),
        static_cast<LONG>(fallbackHeight),
    };
    Check(CountVisiblePixels(horizontalDualQuotaFrame, firstHorizontalLabelRect) > 0
          && CountVisiblePixels(horizontalDualQuotaFrame, secondHorizontalLabelRect) > 0,
          "horizontal dual-quota GDI layout also forces both labels");

    cqt::TaskbarRenderModel boundaryModel;
    boundaryModel.showWeekly = false;
    boundaryModel.showSingleQuotaLabel = false;
    boundaryModel.fiveHour = L"--";
    const cqt::GdiFallbackFrame unavailableFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, boundaryModel);
    const RECT valueRect{
        static_cast<LONG>(fallbackColumns.labelRight),
        0,
        static_cast<LONG>(fallbackColumns.valueRight),
        static_cast<LONG>(fallbackHeight),
    };
    const RECT percentRect{
        static_cast<LONG>(fallbackColumns.valueRight),
        0,
        static_cast<LONG>(fallbackColumns.contentRight),
        static_cast<LONG>(fallbackHeight),
    };
    Check(CountVisiblePixels(unavailableFrame, fallbackLabelRect) == 0
          && CountVisiblePixels(unavailableFrame, valueRect) > 0
          && CountVisiblePixels(unavailableFrame, percentRect) > 0,
          "GDI fallback keeps --% visible while the single label is hidden");

    boundaryModel.fiveHour = L"100";
    boundaryModel.fiveHourRemaining = 9.0;
    boundaryModel.warningMarker = true;
    const cqt::GdiFallbackFrame warningFrame = cqt::RenderGdiFallbackFrame(
        fallbackWidth, fallbackHeight, 96, false, boundaryModel);
    const LONG warningWidth = MulDiv(
        static_cast<int>(cqt::kWarningWidthDip), 96, 96);
    const cqt::QuotaColumnLayout warningColumns = cqt::ComputeQuotaColumnLayout(
        static_cast<float>(fallbackWidth - warningWidth));
    const RECT warningValueRect{
        static_cast<LONG>(warningColumns.labelRight),
        0,
        static_cast<LONG>(warningColumns.valueRight),
        static_cast<LONG>(fallbackHeight),
    };
    const RECT warningPercentRect{
        static_cast<LONG>(warningColumns.valueRight),
        0,
        static_cast<LONG>(warningColumns.contentRight),
        static_cast<LONG>(fallbackHeight),
    };
    const RECT warningRect{
        static_cast<LONG>(fallbackWidth) - warningWidth,
        0,
        static_cast<LONG>(fallbackWidth),
        static_cast<LONG>(fallbackHeight),
    };
    Check(CountVisiblePixels(warningFrame, warningValueRect) > 0
          && CountVisiblePixels(warningFrame, warningPercentRect) > 0,
          "GDI fallback keeps 100% visible with a hidden label");
    Check(HasRedWarningPixel(warningFrame, warningValueRect),
          "GDI fallback keeps quota-aware warning color on the value");
    Check(CountVisiblePixels(warningFrame, warningRect) > 0,
          "GDI fallback keeps the stale-data warning marker");

    cqt::RenderRecoveryState recovery;
    constexpr UINT expectedDelays[]{100, 250, 500, 1000, 2000};
    for (const UINT expected : expectedDelays)
    {
        UINT delay = 0;
        Check(recovery.NextDelay(delay) && delay == expected,
              "render recovery uses bounded increasing delays");
    }
    UINT exhaustedDelay = 0;
    Check(!recovery.NextDelay(exhaustedDelay) && recovery.Attempts() == 5,
          "render recovery stops after five attempts");
    recovery.Reset();
    UINT resetDelay = 0;
    Check(recovery.NextDelay(resetDelay) && resetDelay == expectedDelays[0],
          "successful render or new presentation resets recovery");

    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    Check(SUCCEEDED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory.ReleaseAndGetAddressOf()))),
        "create DirectWrite factory for compact layout measurement");
    if (dwriteFactory)
    {
        const float contentRight = static_cast<float>(cqt::kTaskbarDisplayWidthDip)
            - cqt::kWarningWidthDip;
        const float middle = contentRight / 2.0F;
        const cqt::CompactQuotaItemLayout item =
            cqt::ComputeCompactQuotaItemLayout(0.0F, middle - 1.0F);
        const cqt::TaskbarFontSizes fonts = cqt::ComputeTaskbarFontSizes(96, 42);
        const float labelWidth = item.labelRight - item.labelLeft;
        const float valueWidth = item.valueRight - item.valueLeft;
        const float fiveHourWidth = MeasureText(dwriteFactory.Get(), L"5h", fonts.narrow);
        const float weeklyWidth = MeasureText(dwriteFactory.Get(), L"1W", fonts.narrow);
        const float percentageWidth = MeasureText(dwriteFactory.Get(), L"100%", fonts.narrow);
        if (fiveHourWidth > labelWidth || weeklyWidth > labelWidth
            || percentageWidth > valueWidth)
        {
            std::printf("compact widths: label=%.2f value=%.2f 5h=%.2f 1W=%.2f 100%%=%.2f\n",
                labelWidth, valueWidth, fiveHourWidth, weeklyWidth, percentageWidth);
        }
        Check(fiveHourWidth <= labelWidth && weeklyWidth <= labelWidth,
              "compact warning layout fits both quota labels");
        Check(percentageWidth <= valueWidth,
              "compact warning layout fits a three-digit percentage");
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    Check(InitCommonControlsEx(&controls) != FALSE, "initialize common controls");

    int contextCalls = 0;
    int contextX = 0;
    int contextY = 0;
    cqt::TaskbarWindow window;
    Check(window.Initialize(GetModuleHandleW(nullptr), [&](int x, int y) {
        ++contextCalls;
        contextX = x;
        contextY = y;
    }), "initialize taskbar window");
    Check(window.Create(), "create DirectComposition window");

    const HWND visual = window.Window();
    Check(visual != nullptr, "create taskbar window");

    if (visual)
    {
        const LONG_PTR extendedStyle = GetWindowLongPtrW(visual, GWL_EXSTYLE);
        Check((extendedStyle & WS_EX_NOREDIRECTIONBITMAP) != 0,
              "window uses DirectComposition redirection bypass");
        Check((extendedStyle & WS_EX_LAYERED) == 0,
              "window does not use layered-window alpha or a color key");

        const LPARAM point = MAKELPARAM(10, 10);
        Check(SendMessageW(visual, WM_NCHITTEST, 0, point) == HTCLIENT,
              "composition window accepts the whole client rectangle");

        SendMessageW(visual, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(visual, WM_LBUTTONUP, 0, point);
        Check(contextCalls == 0, "left click remains inert");

        SendMessageW(visual, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(visual), MAKELPARAM(37, 19));
        Check(contextCalls == 1 && contextX == 37 && contextY == 19,
              "composition window forwards context menu from transparent area");
    }

    window.Destroy();
    Check(window.Window() == nullptr, "destroy taskbar window");
    std::printf("taskbar window summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
