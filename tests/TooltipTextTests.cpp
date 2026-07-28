#include "settings/Settings.h"
#include "ui/TaskbarPresentation.h"
#include "ui/TooltipText.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace
{

int failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

cqt::AppState SuccessfulState(long long now)
{
    cqt::AppState state;
    state.hasSuccessfulUsageData = true;
    state.lastSuccessfulUsage.success = true;
    state.lastSuccessfulUsage.fetchedAtUnixSeconds = now - 30;
    state.lastSuccessfulUsage.email = L"fixture@example.invalid";
    state.lastSuccessfulUsage.planType = L"plus";
    state.lastSuccessfulUsage.fiveHour = {true, 20.0, 80.0, 18000, 0, now + 3690};
    state.lastSuccessfulUsage.weekly = {true, 30.0, 70.0, 604800, 0, now + 172890};
    state.latestUsageAttempt = state.lastSuccessfulUsage;
    state.hasSuccessfulResetCreditsData = true;
    state.lastSuccessfulResetCredits.success = true;
    state.lastSuccessfulResetCredits.availableCount = 2;
    state.lastSuccessfulResetCredits.fetchedAtUnixSeconds = now - 30;
    state.lastSuccessfulResetCredits.availableCredits = {
        {true, now + 86400},
        {false, 0}};
    state.latestResetCreditsAttempt = state.lastSuccessfulResetCredits;
    return state;
}

std::wstring ExpectedExpiry(long long unixSeconds)
{
    const std::time_t value = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
    if (localtime_s(&local, &value) != 0) return L"--";
    wchar_t buffer[64]{};
    return std::wcsftime(
        buffer, std::size(buffer), L"%y/%m/%d %H 时 %M 分", &local)
        ? buffer : L"--";
}

} // namespace

int main()
{
    constexpr long long now = 1893456000;
    cqt::AuthSearchPaths paths;
    auto state = SuccessfulState(now);
    const std::wstring text = cqt::BuildTooltipText(state, paths, now);
    Check(text.find(L"可用重置：2 次") != std::wstring::npos, "reset count is displayed");
    Check(text.find(L"最早到期：--") != std::wstring::npos,
          "unknown reset expiry prevents a misleading earliest time");
    Check(text.find(L"下次刷新") == std::wstring::npos, "next refresh countdown is omitted");
    Check(text == cqt::BuildTooltipText(state, paths, now + 1),
          "tooltip text remains stable between minute boundaries");

    state.refreshing = true;
    Check(cqt::BuildTooltipText(state, paths, now).find(L"正在刷新") != std::wstring::npos,
          "active refresh state remains visible");

    state = SuccessfulState(now);
    state.lastSuccessfulResetCredits.availableCount = 3;
    state.lastSuccessfulResetCredits.availableCredits = {
        {true, now + 172800},
        {true, now + 86400},
        {true, now + 259200}};
    state.latestResetCreditsAttempt = state.lastSuccessfulResetCredits;
    const std::wstring earliest = cqt::BuildTooltipText(state, paths, now);
    Check(earliest.find(L"最早到期：" + ExpectedExpiry(now + 86400)) != std::wstring::npos,
          "only the earliest reset expiry is displayed with local date and minute precision");
    Check(earliest.find(ExpectedExpiry(now + 172800)) == std::wstring::npos
          && earliest.find(ExpectedExpiry(now + 259200)) == std::wstring::npos,
          "later reset expiries are omitted");

    state = SuccessfulState(now);
    state.lastSuccessfulResetCredits.availableCount = 0;
    state.lastSuccessfulResetCredits.availableCredits.clear();
    state.latestResetCreditsAttempt = state.lastSuccessfulResetCredits;
    const std::wstring zero = cqt::BuildTooltipText(state, paths, now);
    Check(zero.find(L"可用重置：0 次") != std::wstring::npos
          && zero.find(L"最早到期：") == std::wstring::npos,
          "zero reset credits omit empty expiry line");

    state = SuccessfulState(now);
    state.latestUsageAttempt = {};
    state.latestUsageAttempt.errorCode = "HTTP_TIMEOUT";
    state.latestUsageAttempt.errorMessage = L"连接 Codex 额度服务超时。";
    auto model = cqt::BuildTaskbarRenderModel(state, cqt::SettingsData{});
    Check(model.statusText.empty() && model.warningMarker,
          "stale network data keeps values and enables warning marker");

    auto displayState = SuccessfulState(now);
    cqt::SettingsData displaySettings;
    displaySettings.showFiveHour = false;
    displaySettings.showSingleQuotaLabel = false;
    model = cqt::BuildTaskbarRenderModel(displayState, displaySettings);
    Check(!model.showFiveHour && model.showWeekly
          && !model.showSingleQuotaLabel && model.weekly == L"70"
          && !cqt::ShouldDrawQuotaLabels(model),
          "weekly-only presentation hides label without changing percentage value");

    displaySettings.showFiveHour = true;
    displaySettings.showWeekly = false;
    model = cqt::BuildTaskbarRenderModel(displayState, displaySettings);
    Check(model.showFiveHour && !model.showWeekly
          && model.fiveHour == L"80" && !cqt::ShouldDrawQuotaLabels(model),
          "five-hour-only presentation uses the same hidden-label rule");

    displaySettings.showWeekly = true;
    model = cqt::BuildTaskbarRenderModel(displayState, displaySettings);
    Check(cqt::ShouldDrawQuotaLabels(model),
          "dual-quota presentation always draws labels despite hidden preference");

    displayState.lastSuccessfulUsage.fiveHour.available = false;
    displayState.lastSuccessfulUsage.weekly.remainingPercent = 100.0;
    displayState.latestUsageAttempt = displayState.lastSuccessfulUsage;
    displaySettings.showFiveHour = true;
    displaySettings.showWeekly = false;
    model = cqt::BuildTaskbarRenderModel(displayState, displaySettings);
    Check(model.fiveHour == L"--" && !cqt::ShouldDrawQuotaLabels(model),
          "hidden label preserves unavailable percentage placeholder");
    displaySettings.showFiveHour = false;
    displaySettings.showWeekly = true;
    model = cqt::BuildTaskbarRenderModel(displayState, displaySettings);
    Check(model.weekly == L"100" && !cqt::ShouldDrawQuotaLabels(model),
          "hidden label preserves three-digit percentage");

    displayState.latestUsageAttempt = {};
    displayState.latestUsageAttempt.errorCode = "HTTP_TIMEOUT";
    model = cqt::BuildTaskbarRenderModel(displayState, displaySettings);
    Check(model.warningMarker && !cqt::ShouldDrawQuotaLabels(model),
          "hidden label does not suppress stale-data warning marker");

    state.latestUsageAttempt = {};
    state.latestUsageAttempt.errorCode = "AUTH_NOT_FOUND";
    state.latestUsageAttempt.errorMessage = L"未找到 Codex 登录信息。";
    model = cqt::BuildTaskbarRenderModel(state, cqt::SettingsData{});
    Check(model.statusText == L"Codex --" && !model.warningMarker,
          "missing authentication overrides stale values");
    paths.candidates.emplace_back(L"C:\\fixture\\.codex\\auth.json");
    const std::wstring missingAuth = cqt::BuildTooltipText(state, paths, now);
    Check(missingAuth.find(L"fixture@example.invalid") == std::wstring::npos
          && missingAuth.find(L"C:\\fixture\\.codex\\auth.json") != std::wstring::npos,
          "missing authentication tooltip hides stale account and lists checked paths");

    state.latestUsageAttempt = {};
    state.latestUsageAttempt.httpStatusCode = 401;
    state.latestUsageAttempt.errorCode = "HTTP_AUTH_INVALID";
    state.latestUsageAttempt.errorMessage = L"Codex 登录已失效。";
    model = cqt::BuildTaskbarRenderModel(state, cqt::SettingsData{});
    Check(model.statusText == L"Codex !" && !model.warningMarker,
          "invalid authentication overrides stale values");

    state = SuccessfulState(now);
    state.lastSuccessfulUsage.fiveHour.resetAtUnixSeconds = 0;
    state.latestUsageAttempt = state.lastSuccessfulUsage;
    const std::wstring unknownReset = cqt::BuildTooltipText(state, paths, now);
    Check(unknownReset.find(L"距离重置(--)：--") != std::wstring::npos,
          "unknown reset time is not described as imminent");

    state.latestUsageAttempt = {};
    state.latestUsageAttempt.httpStatusCode = 503;
    state.latestUsageAttempt.errorCode = "HTTP_SERVER_ERROR";
    state.latestUsageAttempt.errorMessage = L"Codex 额度服务暂不可用。";
    Check(cqt::BuildTooltipText(state, paths, now).find(L"Codex 额度服务暂不可用，显示的是旧数据")
              != std::wstring::npos,
          "server failure is distinguished from a network failure");

    std::printf("tooltip text summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
