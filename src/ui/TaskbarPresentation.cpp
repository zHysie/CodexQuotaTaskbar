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

bool ZhipuAuthenticationMissing(const cqt::ZhipuUsageSnapshot& snapshot)
{
    return snapshot.errorCode == "ZHIPU_AUTH_NOT_FOUND"
        || snapshot.errorCode == "ZHIPU_AUTH_TOKEN_MISSING"
        || snapshot.errorCode == "ZHIPU_BASE_URL_UNSUPPORTED";
}

bool ZhipuAuthenticationInvalid(const cqt::ZhipuUsageSnapshot& snapshot)
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
    model.showSingleQuotaLabel = settings.showSingleQuotaLabel;
    model.colorMode = settings.colorMode;

    if (state.activeProvider == QuotaProvider::Zhipu)
    {
        if (ZhipuAuthenticationMissing(state.latestZhipuAttempt))
        {
            model.statusText = L"GLM --";
            return model;
        }
        if (ZhipuAuthenticationInvalid(state.latestZhipuAttempt))
        {
            model.statusText = L"GLM !";
            return model;
        }
        if (!state.hasSuccessfulZhipuData)
        {
            model.statusText = state.latestZhipuAttempt.errorCode.empty() ? L"GLM …" : L"GLM ?";
            return model;
        }
        model.fiveHour = Percent(state.lastSuccessfulZhipuUsage.fiveHour);
        model.weekly = Percent(state.lastSuccessfulZhipuUsage.weekly);
        if (state.lastSuccessfulZhipuUsage.fiveHour.available)
            model.fiveHourRemaining = state.lastSuccessfulZhipuUsage.fiveHour.remainingPercent;
        if (state.lastSuccessfulZhipuUsage.weekly.available)
            model.weeklyRemaining = state.lastSuccessfulZhipuUsage.weekly.remainingPercent;
        model.warningMarker = !state.latestZhipuAttempt.success
            && !state.latestZhipuAttempt.errorCode.empty();
        return model;
    }

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
