#pragma once

#include <string>
#include <vector>

namespace cqt
{

struct UsageWindow
{
    bool available = false;
    double usedPercent = 0.0;
    double remainingPercent = 0.0;
    long long windowSeconds = 0;
    long long resetAfterSeconds = 0;
    long long resetAtUnixSeconds = 0;
};

struct UsageSnapshot
{
    bool success = false;
    std::wstring email;
    std::wstring planType;
    UsageWindow fiveHour;
    UsageWindow weekly;
    long long fetchedAtUnixSeconds = 0;
    std::wstring errorMessage;
    std::string errorCode;
    int httpStatusCode = 0;
};

struct ResetCredit
{
    bool hasValidExpiry = false;
    long long expiresAtUnixSeconds = 0;
};

struct ResetCreditsSnapshot
{
    bool success = false;
    int availableCount = 0;
    std::vector<ResetCredit> availableCredits;
    long long fetchedAtUnixSeconds = 0;
    std::wstring errorMessage;
    std::string errorCode;
    int httpStatusCode = 0;
};

struct RefreshResult
{
    UsageSnapshot usage;
    ResetCreditsSnapshot resetCredits;
};

struct AppState
{
    UsageSnapshot lastSuccessfulUsage;
    UsageSnapshot latestUsageAttempt;
    ResetCreditsSnapshot lastSuccessfulResetCredits;
    ResetCreditsSnapshot latestResetCreditsAttempt;
    bool refreshing = false;
    bool hasSuccessfulUsageData = false;
    bool hasSuccessfulResetCreditsData = false;
    long long nextResetCreditsEligibleAtUnixSeconds = 0;
};

} // namespace cqt
