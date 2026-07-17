#include "ui/TaskbarPresentation.h"

#include <cmath>

namespace
{

std::wstring Percent(const cqt::UsageWindow& window)
{
    return window.available
        ? std::to_wstring(static_cast<int>(std::lround(window.remainingPercent)))
        : L"--";
}

bool AuthenticationMissing(const cqt::UsageSnapshot& snapshot)
{
    return snapshot.errorCode == "AUTH_NOT_FOUND";
}

bool AuthenticationInvalid(const cqt::UsageSnapshot& snapshot)
{
    return snapshot.httpStatusCode == 401 || snapshot.httpStatusCode == 403;
}

} // namespace

namespace cqt
{

TaskbarRenderModel BuildTaskbarRenderModel(
    const AppState& state,
    const SettingsData& settings)
{
    TaskbarRenderModel model;
    model.layout = settings.layout;
    model.showFiveHour = settings.showFiveHour;
    model.showWeekly = settings.showWeekly;
    model.colorMode = settings.colorMode;

    if (AuthenticationMissing(state.latestUsageAttempt))
    {
        model.statusText = L"Codex --";
        return model;
    }
    if (AuthenticationInvalid(state.latestUsageAttempt))
    {
        model.statusText = L"Codex !";
        return model;
    }
    if (!state.hasSuccessfulUsageData)
    {
        model.statusText = state.latestUsageAttempt.errorCode.empty() ? L"Codex …" : L"Codex ?";
        return model;
    }

    model.fiveHour = Percent(state.lastSuccessfulUsage.fiveHour);
    model.weekly = Percent(state.lastSuccessfulUsage.weekly);
    if (state.lastSuccessfulUsage.fiveHour.available)
        model.fiveHourRemaining = state.lastSuccessfulUsage.fiveHour.remainingPercent;
    if (state.lastSuccessfulUsage.weekly.available)
        model.weeklyRemaining = state.lastSuccessfulUsage.weekly.remainingPercent;
    model.warningMarker = !state.latestUsageAttempt.success
        && !state.latestUsageAttempt.errorCode.empty();
    return model;
}

} // namespace cqt
