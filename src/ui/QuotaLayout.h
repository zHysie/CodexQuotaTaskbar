#pragma once

#include <algorithm>

namespace cqt
{

inline constexpr int kTaskbarDisplayWidthDip = 84;
inline constexpr float kWarningWidthDip = 10.0F;

struct QuotaColumnLayout
{
    float contentLeft = 0.0F;
    float contentRight = 0.0F;
    float labelRight = 0.0F;
    float valueRight = 0.0F;
};

[[nodiscard]] constexpr QuotaColumnLayout ComputeQuotaColumnLayout(float clientWidth)
{
    const float width = std::max(0.0F, clientWidth);
    const float contentWidth = width * 0.72F;
    const float contentLeft = (width - contentWidth) / 2.0F;
    const float labelWidth = contentWidth * 0.40F;
    const float valueWidth = contentWidth * 0.40F;
    return QuotaColumnLayout{
        contentLeft,
        contentLeft + contentWidth,
        contentLeft + labelWidth,
        contentLeft + labelWidth + valueWidth};
}

struct CompactQuotaItemLayout
{
    float labelLeft = 0.0F;
    float labelRight = 0.0F;
    float valueLeft = 0.0F;
    float valueRight = 0.0F;
};

[[nodiscard]] constexpr CompactQuotaItemLayout ComputeCompactQuotaItemLayout(
    float left, float right, float scale = 1.0F)
{
    const float orderedLeft = std::max(0.0F, left);
    const float orderedRight = std::max(orderedLeft, right);
    const float width = orderedRight - orderedLeft;
    const float boundary = orderedLeft + width * 0.39F;
    const float halfGap = std::min(std::max(0.0F, scale) * 0.5F, width * 0.05F);
    return CompactQuotaItemLayout{
        orderedLeft,
        std::max(orderedLeft, boundary - halfGap),
        std::min(orderedRight, boundary + halfGap),
        orderedRight};
}

struct TaskbarFontSizes
{
    float vertical = 0.0F;
    float compact = 0.0F;
    float narrow = 0.0F;
};

[[nodiscard]] constexpr bool UseCompactHorizontalQuotaLayout(
    bool horizontal, bool showFiveHour, bool showWeekly, bool hasStatusText)
{
    // A single visible quota intentionally uses the same fixed-column layout
    // and font as vertical mode. Compact horizontal rendering is only needed
    // when both quotas must share one line.
    return horizontal && showFiveHour && showWeekly && !hasStatusText;
}

[[nodiscard]] constexpr TaskbarFontSizes ComputeTaskbarFontSizes(
    unsigned int dpi, long clientHeight)
{
    const float scale = static_cast<float>(std::max(dpi, 1U)) / 96.0F;
    const float heightDip = static_cast<float>(std::max(clientHeight, 1L)) / scale;
    const float verticalDip = std::clamp(heightDip * 0.29F, 9.0F, 13.0F);
    return TaskbarFontSizes{
        verticalDip * scale,
        std::clamp(verticalDip * 0.78F, 9.0F, 9.5F) * scale,
        7.6F * scale};
}

} // namespace cqt
