#include "ui/QuotaLayout.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace
{

bool NearlyEqual(float left, float right)
{
    return std::fabs(left - right) < 0.001F;
}

bool VerifyWidth(float width)
{
    const cqt::QuotaColumnLayout columns = cqt::ComputeQuotaColumnLayout(width);
    const float contentWidth = width * 0.72F;
    const bool ordered = columns.contentLeft >= 0.0F
        && columns.contentLeft < columns.labelRight
        && columns.labelRight < columns.valueRight
        && columns.valueRight < columns.contentRight
        && columns.contentRight <= width;
    const bool expectedWidths = NearlyEqual(columns.contentRight - columns.contentLeft, contentWidth)
        && NearlyEqual(columns.labelRight - columns.contentLeft, contentWidth * 0.40F)
        && NearlyEqual(columns.valueRight - columns.labelRight, contentWidth * 0.40F)
        && NearlyEqual(columns.contentRight - columns.valueRight, contentWidth * 0.20F);
    if (!ordered || !expectedWidths)
    {
        std::printf("invalid layout for width %.1f\n", width);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    static_assert(cqt::kTaskbarDisplayWidthDip == 84);

    constexpr std::array<float, 8> widths{60.0F, 72.0F, 84.0F, 90.0F, 105.0F, 126.0F, 168.0F, 240.0F};
    for (const float width : widths)
    {
        if (!VerifyWidth(width))
        {
            return 1;
        }
    }

    const cqt::QuotaColumnLayout zero = cqt::ComputeQuotaColumnLayout(-10.0F);
    if (!NearlyEqual(zero.contentLeft, 0.0F) || !NearlyEqual(zero.contentRight, 0.0F))
    {
        return 1;
    }

    const cqt::CompactQuotaItemLayout compact =
        cqt::ComputeCompactQuotaItemLayout(1.0F, 40.0F);
    if (!NearlyEqual(compact.labelLeft, 1.0F)
        || !NearlyEqual(compact.valueRight, 40.0F)
        || !NearlyEqual(compact.valueLeft - compact.labelRight, 1.0F)
        || compact.labelRight <= compact.labelLeft
        || compact.valueRight <= compact.valueLeft)
    {
        return 1;
    }

    const cqt::TaskbarFontSizes fonts100 = cqt::ComputeTaskbarFontSizes(96, 42);
    const cqt::TaskbarFontSizes fonts150 = cqt::ComputeTaskbarFontSizes(144, 63);
    const cqt::TaskbarFontSizes fonts200 = cqt::ComputeTaskbarFontSizes(192, 84);
    if (!NearlyEqual(fonts150.vertical, fonts100.vertical * 1.5F)
        || !NearlyEqual(fonts150.narrow, fonts100.narrow * 1.5F)
        || !NearlyEqual(fonts200.vertical, fonts100.vertical * 2.0F)
        || !NearlyEqual(fonts200.compact, fonts100.compact * 2.0F))
    {
        std::printf("taskbar fonts do not scale with physical DPI\n");
        return 1;
    }

    if (!cqt::UseCompactHorizontalQuotaLayout(true, true, true, false)
        || cqt::UseCompactHorizontalQuotaLayout(true, true, false, false)
        || cqt::UseCompactHorizontalQuotaLayout(true, false, true, false)
        || cqt::UseCompactHorizontalQuotaLayout(false, true, true, false)
        || cqt::UseCompactHorizontalQuotaLayout(true, true, true, true))
    {
        std::printf("single horizontal quota does not reuse vertical layout\n");
        return 1;
    }

    std::printf("quota column layout: %zu widths and single-quota layout parity passed\n", widths.size());
    return 0;
}
