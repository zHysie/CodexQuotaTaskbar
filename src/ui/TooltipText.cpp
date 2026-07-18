#include "ui/TooltipText.h"

#include <cmath>
#include <ctime>
#include <optional>
#include <sstream>

namespace
{

std::wstring FormatDuration(long long target, long long now)
{
    long long seconds = target - now;
    if (seconds <= 0) return L"即将重置";
    const long long days = seconds / 86400; seconds %= 86400;
    const long long hours = seconds / 3600; seconds %= 3600;
    const long long minutes = seconds / 60;
    std::wostringstream output;
    if (days > 0) output << days << L" 天 ";
    if (hours > 0 || days > 0) output << hours << L" 小时 ";
    output << minutes << L" 分钟";
    return output.str();
}

std::wstring FormatLocalTime(long long unixSeconds, const wchar_t* format)
{
    if (unixSeconds <= 0) return L"--";
    const std::time_t value = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
    if (localtime_s(&local, &value) != 0) return L"--";
    wchar_t buffer[64]{};
    return std::wcsftime(buffer, std::size(buffer), format, &local) ? buffer : L"--";
}

std::wstring Percent(const cqt::UsageWindow& window)
{
    return window.available
        ? std::to_wstring(static_cast<int>(std::lround(window.remainingPercent))) : L"--";
}

void AppendUsageWindow(std::wostringstream& output, const wchar_t* title,
                       const cqt::UsageWindow& window, long long now)
{
    output << title << L"：剩余 " << (window.available ? Percent(window) + L"%" : L"--") << L"\r\n";
    output << L"距离重置(" << FormatLocalTime(window.resetAtUnixSeconds, L"%m/%d %H:%M")
           << L")：" << (window.available && window.resetAtUnixSeconds > 0
                                ? FormatDuration(window.resetAtUnixSeconds, now)
                                : L"--") << L"\r\n";
}

bool AuthenticationMissing(const cqt::UsageSnapshot& snapshot)
{
    return snapshot.errorCode == "AUTH_NOT_FOUND";
}

bool AuthenticationInvalid(const cqt::UsageSnapshot& snapshot)
{
    return snapshot.httpStatusCode == 401 || snapshot.httpStatusCode == 403;
}

const wchar_t* FailureHeading(const cqt::UsageSnapshot& snapshot)
{
    if (snapshot.errorCode == "HTTP_TIMEOUT"
        || snapshot.errorCode == "HTTP_REQUEST_FAILED"
        || snapshot.errorCode == "HTTP_CONNECT_FAILED"
        || snapshot.errorCode == "HTTP_NETWORK_FAILED"
        || snapshot.errorCode == "HTTP_BODY_READ_FAILED")
        return L"当前网络刷新失败";
    if (snapshot.httpStatusCode == 429 || snapshot.httpStatusCode >= 500)
        return L"Codex 额度服务暂不可用";
    return L"Codex 额度数据暂时无法更新";
}

} // namespace

namespace cqt
{

std::wstring BuildTooltipText(
    const AppState& state,
    const AuthSearchPaths& authPaths,
    long long nowUnixSeconds)
{
    std::wostringstream tooltip;
    tooltip << L"Codex 额度\r\n\r\n";
    if (AuthenticationMissing(state.latestUsageAttempt))
    {
        tooltip << state.latestUsageAttempt.errorMessage << L"\r\n"
                << L"\r\n请先打开 Codex 并完成登录。\r\n查找路径：\r\n";
        for (const auto& path : authPaths.candidates) tooltip << path.wstring() << L"\r\n";
    }
    else if (AuthenticationInvalid(state.latestUsageAttempt))
    {
        tooltip << L"Codex 登录已失效\r\n\r\n"
                << L"请打开 Codex 重新登录，然后右键选择「立即刷新」。\r\n";
    }
    else if (state.hasSuccessfulUsageData)
    {
        AppendUsageWindow(tooltip, L"5 小时额度", state.lastSuccessfulUsage.fiveHour, nowUnixSeconds);
        tooltip << L"\r\n";
        AppendUsageWindow(tooltip, L"周额度", state.lastSuccessfulUsage.weekly, nowUnixSeconds);
        tooltip << L"\r\n";
        if (state.hasSuccessfulResetCreditsData)
        {
            const auto& reset = state.lastSuccessfulResetCredits;
            int available = 0;
            std::optional<long long> earliestExpiry;
            bool hasUnknownExpiry = false;
            for (const auto& credit : reset.availableCredits)
            {
                if (credit.hasValidExpiry && credit.expiresAtUnixSeconds <= nowUnixSeconds) continue;
                ++available;
                if (!credit.hasValidExpiry)
                {
                    hasUnknownExpiry = true;
                    continue;
                }
                if (!earliestExpiry || credit.expiresAtUnixSeconds < *earliestExpiry)
                    earliestExpiry = credit.expiresAtUnixSeconds;
            }
            tooltip << L"可用重置：" << available << L" 次\r\n";
            if (available > 0)
            {
                tooltip << L"最早到期："
                        << (!hasUnknownExpiry && earliestExpiry
                                ? FormatLocalTime(*earliestExpiry, L"%y/%m/%d %H 时 %M 分")
                                : L"--")
                        << L"\r\n";
            }
            if (!state.latestResetCreditsAttempt.success
                && !state.latestResetCreditsAttempt.errorCode.empty())
            {
                tooltip << L"重置机会显示的是 "
                        << FormatLocalTime(reset.fetchedAtUnixSeconds, L"%H:%M:%S")
                        << L" 获取的旧数据。\r\n";
            }
        }
        else tooltip << L"可用重置：暂不可获取\r\n";
        tooltip << L"\r\n账号："
                << (state.lastSuccessfulUsage.email.empty() ? L"--" : state.lastSuccessfulUsage.email)
                << L"\r\n当前套餐："
                << (state.lastSuccessfulUsage.planType.empty() ? L"--" : state.lastSuccessfulUsage.planType)
                << L"\r\n最后更新："
                << FormatLocalTime(state.lastSuccessfulUsage.fetchedAtUnixSeconds, L"%H:%M:%S") << L"\r\n";
        if (!state.latestUsageAttempt.success && !state.latestUsageAttempt.errorCode.empty())
            tooltip << L"\r\n" << FailureHeading(state.latestUsageAttempt)
                    << L"，显示的是旧数据。\r\n错误："
                    << state.latestUsageAttempt.errorMessage << L"\r\n";
    }
    else if (!state.latestUsageAttempt.errorMessage.empty())
    {
        tooltip << state.latestUsageAttempt.errorMessage << L"\r\n";
    }
    else tooltip << L"正在准备首次刷新。\r\n";
    if (state.refreshing) tooltip << L"\r\n正在刷新";
    return tooltip.str();
}

} // namespace cqt
