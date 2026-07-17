#include "usage/UsageParser.h"

#include "common/JsonAdapter.h"
#include "common/StringUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{

constexpr long long kTwelveHours = 12LL * 60 * 60;
constexpr long long kFiveHours = 5LL * 60 * 60;
constexpr long long kOneWeek = 7LL * 24 * 60 * 60;

const double* Number(const cqt::JsonValue* value)
{
    return value ? value->AsNumber() : nullptr;
}

bool ToNonNegativeSeconds(const cqt::JsonValue* value, long long& output)
{
    const double* number = Number(value);
    if (!number || !std::isfinite(*number) || *number < 0.0
        || *number > static_cast<double>(std::numeric_limits<long long>::max()))
    {
        return false;
    }
    output = static_cast<long long>(*number);
    return true;
}

struct Candidate
{
    cqt::UsageWindow window;
    bool shortTerm = false;
};

bool ParseWindow(const cqt::JsonValue& value, long long receivedAt, Candidate& candidate)
{
    if (!value.IsObject())
    {
        return false;
    }
    const double* used = Number(value.Find("used_percent"));
    long long duration = 0;
    if (!used || !std::isfinite(*used) || *used < 0.0 || *used > 100.0
        || !ToNonNegativeSeconds(value.Find("limit_window_seconds"), duration) || duration == 0)
    {
        return false;
    }

    cqt::UsageWindow parsed;
    parsed.available = true;
    parsed.usedPercent = *used;
    parsed.remainingPercent = 100.0 - *used;
    parsed.windowSeconds = duration;
    ToNonNegativeSeconds(value.Find("reset_after_seconds"), parsed.resetAfterSeconds);
    if (!ToNonNegativeSeconds(value.Find("reset_at"), parsed.resetAtUnixSeconds)
        && parsed.resetAfterSeconds > 0)
    {
        parsed.resetAtUnixSeconds = receivedAt + parsed.resetAfterSeconds;
    }
    candidate.window = parsed;
    candidate.shortTerm = duration <= kTwelveHours;
    return true;
}

void SelectClosest(const std::vector<Candidate>& candidates, bool shortTerm, cqt::UsageWindow& destination)
{
    const long long target = shortTerm ? kFiveHours : kOneWeek;
    const Candidate* best = nullptr;
    long long bestDistance = std::numeric_limits<long long>::max();
    for (const Candidate& candidate : candidates)
    {
        if (candidate.shortTerm != shortTerm)
        {
            continue;
        }
        const long long distance = candidate.window.windowSeconds > target
            ? candidate.window.windowSeconds - target : target - candidate.window.windowSeconds;
        if (!best || distance < bestDistance)
        {
            best = &candidate;
            bestDistance = distance;
        }
    }
    if (best)
    {
        destination = best->window;
    }
}

} // namespace

namespace cqt
{

UsageSnapshot UsageParser::Parse(std::string_view json, long long receivedAtUnixSeconds)
{
    UsageSnapshot snapshot;
    snapshot.fetchedAtUnixSeconds = receivedAtUnixSeconds;
    const JsonParseResult parsed = JsonAdapter::Parse(json);
    if (!parsed.success || !parsed.root.IsObject())
    {
        snapshot.errorCode = "USAGE_JSON_INVALID";
        snapshot.errorMessage = L"无法解析 Codex 额度数据。";
        return snapshot;
    }

    const JsonValue* rateLimit = parsed.root.Find("rate_limit");
    if (!rateLimit || !rateLimit->IsObject())
    {
        snapshot.errorCode = "USAGE_SCHEMA_INVALID";
        snapshot.errorMessage = L"Codex 额度数据缺少有效窗口。";
        return snapshot;
    }

    std::vector<Candidate> candidates;
    for (const char* key : {"primary_window", "secondary_window"})
    {
        const JsonValue* window = rateLimit->Find(key);
        Candidate candidate;
        if (window && ParseWindow(*window, receivedAtUnixSeconds, candidate))
        {
            candidates.push_back(candidate);
        }
    }
    SelectClosest(candidates, true, snapshot.fiveHour);
    SelectClosest(candidates, false, snapshot.weekly);

    if (!snapshot.fiveHour.available && !snapshot.weekly.available)
    {
        snapshot.errorCode = "USAGE_SCHEMA_INVALID";
        snapshot.errorMessage = L"Codex 额度数据没有可用的短期或周窗口。";
        return snapshot;
    }

    if (const JsonValue* email = parsed.root.Find("email"); email && email->IsString())
    {
        snapshot.email = Utf8ToWide(*email->AsString());
    }
    if (const JsonValue* plan = parsed.root.Find("plan_type"); plan && plan->IsString())
    {
        snapshot.planType = Utf8ToWide(*plan->AsString());
    }
    snapshot.success = true;
    return snapshot;
}

} // namespace cqt
