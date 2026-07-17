#include "taskbar/TaskbarWindow.h"
#include "ui/QuotaLayout.h"

#include <commctrl.h>
#include <dwrite.h>

#include <cstdio>
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

} // namespace

int main()
{
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
