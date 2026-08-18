#include "usage/CodexAuthReader.h"
#include "usage/HttpPolicy.h"
#include "usage/ResetCreditsParser.h"
#include "usage/UsageParser.h"
#include "usage/ZhipuAuthReader.h"
#include "usage/ZhipuUsageParser.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

int failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

std::string ReadFixture(const wchar_t* name)
{
    const std::filesystem::path path = std::filesystem::path(CQT_FIXTURE_DIR) / name;
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path TemporaryDirectory()
{
    wchar_t root[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(root)), root);
    const std::filesystem::path directory = std::filesystem::path(root)
        / (L"CodexQuotaTaskbar-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(directory);
    return directory;
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void TestUsageParser()
{
    const auto full = cqt::UsageParser::Parse(ReadFixture(L"usage_full.json"), 1700000000);
    Check(full.success && full.fiveHour.available && full.weekly.available, "usage dual windows");
    Check(std::fabs(full.fiveHour.remainingPercent - 84.75) < 0.001, "usage decimal percentage");
    Check(full.fiveHour.windowSeconds == 18000 && full.weekly.windowSeconds == 604800, "usage classified by duration");
    Check(full.email == L"fixture@example.invalid" && full.planType == L"plus", "usage optional identity fields");

    const auto swapped = cqt::UsageParser::Parse(ReadFixture(L"usage_swapped.json"), 1000);
    Check(swapped.success && swapped.fiveHour.remainingPercent == 90.0 && swapped.weekly.remainingPercent == 60.0,
          "usage swapped primary secondary");
    Check(swapped.fiveHour.resetAtUnixSeconds == 1050, "usage reset_after fallback");

    const auto weeklyOnly = cqt::UsageParser::Parse(ReadFixture(L"usage_weekly_only.json"), 1000);
    Check(weeklyOnly.success && !weeklyOnly.fiveHour.available && weeklyOnly.weekly.available,
          "usage weekly only");
    Check(weeklyOnly.email.empty() && weeklyOnly.planType.empty(), "usage invalid optional fields ignored");

    const auto invalidJson = cqt::UsageParser::Parse("{not-json", 0);
    Check(!invalidJson.success && invalidJson.errorCode == "USAGE_JSON_INVALID", "usage invalid json");
    const auto invalidRange = cqt::UsageParser::Parse(ReadFixture(L"usage_out_of_range.json"), 0);
    Check(!invalidRange.success && invalidRange.errorCode == "USAGE_SCHEMA_INVALID", "usage out of range rejected");
    const auto shortOnly = cqt::UsageParser::Parse(
        R"({"rate_limit":{"secondary_window":{"used_percent":0,"limit_window_seconds":18000}}})", 0);
    Check(shortOnly.success && shortOnly.fiveHour.remainingPercent == 100.0 && !shortOnly.weekly.available,
          "usage short only and zero used");
    const auto overflowingReset = cqt::UsageParser::Parse(
        R"({"rate_limit":{"secondary_window":{"used_percent":10,"limit_window_seconds":18000,"reset_after_seconds":9223372036854770000}}})",
        10000);
    Check(overflowingReset.success && overflowingReset.fiveHour.available
          && overflowingReset.fiveHour.resetAtUnixSeconds == 0,
          "usage reset_after overflow is rejected without losing quota data");
}

void TestResetParser()
{
    const auto valid = cqt::ResetCreditsParser::Parse(ReadFixture(L"reset_valid.json"), 1000);
    Check(valid.success && valid.availableCount == 3 && valid.availableCredits.size() == 3,
          "reset positive count and status filtering");
    Check(valid.availableCredits[0].hasValidExpiry && valid.availableCredits[1].hasValidExpiry
          && !valid.availableCredits[2].hasValidExpiry, "reset expiry sort and invalid placeholder");
    const auto variants = cqt::ResetCreditsParser::Parse(ReadFixture(L"reset_time_variants.json"), 1000);
    Check(variants.success && variants.availableCredits.size() == 4
          && variants.availableCredits[0].expiresAtUnixSeconds == 1893456000
          && variants.availableCredits[1].expiresAtUnixSeconds == 1893542400
          && variants.availableCredits[2].expiresAtUnixSeconds == 1893628800
          && variants.availableCredits[3].expiresAtUnixSeconds == 1893715200,
          "reset expiry accepts fractional RFC3339, offsets, Unix seconds and milliseconds");
    const auto zero = cqt::ResetCreditsParser::Parse(ReadFixture(L"reset_zero.json"), 1000);
    Check(zero.success && zero.availableCount == 0 && zero.availableCredits.empty(), "reset zero count");
    const auto mismatch = cqt::ResetCreditsParser::Parse(ReadFixture(L"reset_mismatch.json"), 1000);
    Check(!mismatch.success && mismatch.errorCode == "RESET_CREDITS_COUNT_MISMATCH", "reset count mismatch");
    Check(!cqt::ResetCreditsParser::Parse("[]", 0).success, "reset root type rejected");
    Check(!cqt::ResetCreditsParser::Parse("{\"available_count\":-1,\"credits\":[]}", 0).success,
          "reset negative count rejected");
}

void TestAuthReader()
{
    const std::filesystem::path root = TemporaryDirectory();
    const std::filesystem::path preferred = root / L"preferred" / L"auth.json";
    const std::filesystem::path fallback = root / L"fallback" / L"auth.json";
    WriteText(fallback, R"({"account_id":"fixture-account","tokens":{"access_token":"fixture-token","id_token":"fixture-id"}})");

    cqt::AuthSearchPaths paths{{preferred, fallback, fallback}};
    cqt::CodexAuthReader reader;
    auto result = reader.Read(paths);
    Check(result.credentials.has_value() && result.sourcePath == fallback, "auth ordered fallback and duplicate removal");
    Check(result.credentials && result.credentials->accessToken.View() == "fixture-token"
          && result.credentials->accountId.View() == "fixture-account", "auth root account id compatibility");
    Check(result.checkedPaths.size() == 2, "auth checked paths are deterministic");

    WriteText(preferred, R"({"tokens":{"access_token":"preferred-token","account_id":"nested-account"}})");
    result = reader.Read(paths);
    Check(result.credentials && result.credentials->accessToken.View() == "preferred-token"
          && result.sourcePath == preferred, "auth preferred path and nested account id");

    WriteText(preferred, "{bad-json");
    result = reader.Read(paths);
    Check(!result.credentials && result.errorCode == "AUTH_JSON_INVALID", "auth invalid json is not skipped");
    WriteText(preferred, R"({"tokens":{"account_id":"fixture"}})");
    result = reader.Read(paths);
    Check(!result.credentials && result.errorCode == "AUTH_ACCESS_TOKEN_MISSING", "auth missing token");

    {
        std::ofstream oversized(preferred, std::ios::binary | std::ios::trunc);
        oversized.seekp(static_cast<std::streamoff>(cqt::CodexAuthReader::kMaximumAuthFileBytes));
        oversized.put('x');
    }
    result = reader.Read(paths);
    Check(!result.credentials && result.errorCode == "AUTH_FILE_TOO_LARGE", "auth oversized file rejected");

    std::filesystem::remove_all(root);
}

void TestZhipuParser()
{
    const auto full = cqt::ZhipuUsageParser::Parse(ReadFixture(L"zhipu_quota_full.json"), 1700000000);
    Check(full.success && full.fiveHour.available && full.weekly.available, "zhipu dual windows");
    Check(std::fabs(full.fiveHour.remainingPercent - 87.5) < 0.001
          && std::fabs(full.weekly.remainingPercent - 54.75) < 0.001,
          "zhipu percentage is used ratio converted to remaining");
    Check(full.fiveHour.resetAtUnixSeconds == 1893456000
          && full.weekly.resetAtUnixSeconds == 1893628800,
          "zhipu millisecond reset time converted to unix seconds");
    Check(full.monthlyMcpAvailable && std::fabs(full.monthlyMcpUsedPercent - 3.2) < 0.001,
          "zhipu optional monthly mcp usage captured");

    const auto weeklyOnly = cqt::ZhipuUsageParser::Parse(ReadFixture(L"zhipu_quota_weekly_only.json"), 1000);
    Check(weeklyOnly.success && !weeklyOnly.fiveHour.available && weeklyOnly.weekly.available
          && std::fabs(weeklyOnly.weekly.remainingPercent - 67.0) < 0.001,
          "zhipu legacy single-tier plan degrades to weekly only");
    Check(!weeklyOnly.monthlyMcpAvailable, "zhipu missing time limit omits mcp line");

    const auto unitMissing = cqt::ZhipuUsageParser::Parse(ReadFixture(L"zhipu_quota_unit_missing.json"), 1000);
    Check(unitMissing.success && unitMissing.fiveHour.available && unitMissing.weekly.available,
          "zhipu missing unit falls back to heuristic classification");
    Check(std::fabs(unitMissing.fiveHour.remainingPercent - 40.0) < 0.001
          && std::fabs(unitMissing.weekly.remainingPercent - 80.0) < 0.001,
          "zhipu heuristic prefers resetless entry for five-hour window");
    Check(unitMissing.weekly.resetAtUnixSeconds == 1893542400
          && unitMissing.fiveHour.resetAtUnixSeconds == 0,
          "zhipu seconds reset time accepted and missing reset stays zero");

    const auto invalidJson = cqt::ZhipuUsageParser::Parse("{not-json", 0);
    Check(!invalidJson.success && invalidJson.errorCode == "ZHIPU_JSON_INVALID", "zhipu invalid json");
    const auto outOfRange = cqt::ZhipuUsageParser::Parse(ReadFixture(L"zhipu_quota_out_of_range.json"), 0);
    Check(!outOfRange.success && outOfRange.errorCode == "ZHIPU_SCHEMA_INVALID",
          "zhipu out-of-range percentages rejected without clamp");
    Check(!cqt::ZhipuUsageParser::Parse(R"({"data":{}})", 0).success, "zhipu missing limits rejected");
    Check(!cqt::ZhipuUsageParser::Parse("[]", 0).success, "zhipu root type rejected");
    const auto stringReset = cqt::ZhipuUsageParser::Parse(
        R"({"data":{"limits":[{"type":"TOKENS_LIMIT","unit":3,"percentage":10,"nextResetTime":"soon"}]}})", 0);
    Check(stringReset.success && stringReset.fiveHour.available
          && stringReset.fiveHour.resetAtUnixSeconds == 0,
          "zhipu invalid reset time keeps quota window without reset countdown");
    const auto lowerType = cqt::ZhipuUsageParser::Parse(
        R"({"data":{"limits":[{"type":"tokens_limit","unit":3,"percentage":10},{"type":"CREDIT_LIMIT","unit":6,"percentage":20}]}})", 0);
    Check(lowerType.success && lowerType.fiveHour.available && lowerType.weekly.available,
          "zhipu type matching is case-insensitive and accepts credit limit");
}

void TestZhipuAuthReader()
{
    const std::filesystem::path root = TemporaryDirectory();
    const std::filesystem::path preferred = root / L"claude-a" / L"settings.json";
    const std::filesystem::path fallback = root / L"claude-b" / L"settings.json";
    WriteText(fallback,
        R"({"model":"fixture","env":{"ANTHROPIC_AUTH_TOKEN":"fixture-key","ANTHROPIC_BASE_URL":"https://open.bigmodel.cn/api/anthropic"}})");

    cqt::ZhipuAuthSearchPaths paths{{preferred, fallback, fallback}};
    cqt::ZhipuAuthReader reader;
    auto result = reader.Read(paths);
    Check(result.credentials.has_value() && result.sourcePath == fallback,
          "zhipu auth ordered fallback and duplicate removal");
    Check(result.credentials && result.credentials->apiKey.View() == "fixture-key"
          && result.credentials->quotaHost == L"open.bigmodel.cn",
          "zhipu auth extracts key and maps bigmodel base url");
    Check(result.checkedPaths.size() == 2, "zhipu auth checked paths are deterministic");

    WriteText(preferred,
        R"({"env":{"ANTHROPIC_AUTH_TOKEN":"fixture-key","ANTHROPIC_BASE_URL":"https://api.z.ai/api/anthropic"}})");
    result = reader.Read(paths);
    Check(result.credentials && result.credentials->quotaHost == L"api.z.ai"
          && result.sourcePath == preferred, "zhipu auth maps international base url");

    WriteText(preferred,
        R"({"env":{"ANTHROPIC_AUTH_TOKEN":"fixture-key","ANTHROPIC_BASE_URL":"https://api.deepseek.com/anthropic"}})");
    result = reader.Read(paths);
    Check(!result.credentials && result.errorCode == "ZHIPU_BASE_URL_UNSUPPORTED",
          "zhipu auth rejects non-zhipu base url before any request");

    WriteText(preferred, R"({"env":{"ANTHROPIC_BASE_URL":"https://open.bigmodel.cn/api/anthropic"}})");
    result = reader.Read(paths);
    Check(!result.credentials && result.errorCode == "ZHIPU_AUTH_TOKEN_MISSING",
          "zhipu auth missing token reported");

    WriteText(preferred, "{bad-json");
    result = reader.Read(paths);
    Check(!result.credentials && result.errorCode == "ZHIPU_AUTH_JSON_INVALID",
          "zhipu auth invalid json is not skipped");

    result = reader.Read(cqt::ZhipuAuthSearchPaths{});
    Check(!result.credentials && result.errorCode == "ZHIPU_AUTH_NOT_FOUND",
          "zhipu auth no candidates reported");

    std::filesystem::remove_all(root);
}

void TestHttpPolicy()
{
    cqt::HttpResponse response;
    response.statusCode = 200;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::Success, "http 2xx");
    response.statusCode = 401;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::AuthenticationFailure, "http 401");
    response.statusCode = 429;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::RateLimited, "http 429");
    response.statusCode = 302;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::RedirectRejected, "http redirect rejected");
    response.statusCode = 0;
    response.transportError = cqt::TransportError::Timeout;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::TransportFailure, "http timeout");
    response.transportError = cqt::TransportError::ResponseBodyTooLarge;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::ResponseTooLarge, "http oversized body");
    response.transportError = cqt::TransportError::ResponseHeadersTooLarge;
    Check(cqt::ClassifyResponse(response) == cqt::HttpResponseClass::ResponseTooLarge, "http oversized headers");

    Check(cqt::ParseRetryAfterSeconds(L"120", 0).value_or(0) == 120, "retry-after seconds");
    constexpr long long dateEpoch = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT
    Check(cqt::ParseRetryAfterSeconds(L"Sun, 06 Nov 1994 08:49:37 GMT", dateEpoch - 77).value_or(0) == 77,
          "retry-after http date");
    Check(!cqt::ParseRetryAfterSeconds(L"invalid", 0).has_value(), "retry-after invalid");
    Check(cqt::ParseRetryAfterSeconds(L"999999", 0).value_or(0) == 24 * 60 * 60,
          "retry-after limited to 24 hours");
    Check(cqt::EffectiveRetryDelaySeconds(60, 120) == 120, "retry-after max with local backoff");
    Check(cqt::UsageBackoffSeconds(1) == 60 && cqt::UsageBackoffSeconds(2) == 120
          && cqt::UsageBackoffSeconds(3) == 300 && cqt::UsageBackoffSeconds(4) == 600,
          "usage backoff schedule");
}

} // namespace

int main()
{
    TestUsageParser();
    TestResetParser();
    TestAuthReader();
    TestZhipuParser();
    TestZhipuAuthReader();
    TestHttpPolicy();
    std::printf("data layer summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
