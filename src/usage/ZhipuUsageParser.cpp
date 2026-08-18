#include "usage/ZhipuUsageParser.h"

#include "common/JsonAdapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

namespace
{

constexpr double kMillisecondThreshold = 1.0e12;

struct TierEntry
{
    cqt::UsageWindow window;
    long long resetAtUnixSeconds = 0;
    bool hasResetAt = false;
    int unit = 0;
    bool hasUnit = false;
};

const double* ValidPercentage(const cqt::JsonValue* value)
{
    const double* number = value ? value->AsNumber() : nullptr;
    if (!number || !std::isfinite(*number) || *number < 0.0 || *number > 100.0)
    {
        return nullptr;
    }
    return number;
}

bool ToUnixSeconds(const cqt::JsonValue* value, long long& output)
{
    const double* number = value ? value->AsNumber() : nullptr;
    if (!number || !std::isfinite(*number) || *number < 0.0)
    {
        return false;
    }
    // nextResetTime 实测为毫秒；大于 10^12 视为毫秒，其余按秒处理。
    const double seconds = *number > kMillisecondThreshold ? *number / 1000.0 : *number;
    if (seconds >= 9.2e18)
    {
        return false;
    }
    output = static_cast<long long>(seconds);
    return true;
}

bool IsTokenLimitType(const cqt::JsonValue& item)
{
    const cqt::JsonValue* typeValue = item.Find("type");
    const std::string* type = typeValue ? typeValue->AsString() : nullptr;
    if (!type) return false;
    auto equalsInsensitive = [type](const char* expected) {
        const std::size_t size = type->size();
        return size == std::strlen(expected)
            && std::equal(type->begin(), type->end(), expected, [](char left, char right) {
                   return std::tolower(static_cast<unsigned char>(left))
                       == std::tolower(static_cast<unsigned char>(right));
               });
    };
    return equalsInsensitive("TOKENS_LIMIT") || equalsInsensitive("CREDIT_LIMIT");
}

bool ParseTier(const cqt::JsonValue& item, TierEntry& entry)
{
    const double* percentage = ValidPercentage(item.Find("percentage"));
    if (!percentage)
    {
        return false;
    }
    entry.window.available = true;
    entry.window.usedPercent = *percentage;
    entry.window.remainingPercent = 100.0 - *percentage;
    long long resetAt = 0;
    if (ToUnixSeconds(item.Find("nextResetTime"), resetAt))
    {
        entry.hasResetAt = true;
        entry.resetAtUnixSeconds = resetAt;
        entry.window.resetAtUnixSeconds = resetAt;
    }
    if (const cqt::JsonValue* unitValue = item.Find("unit"))
    {
        if (const double* unit = unitValue->AsNumber(); unit && std::isfinite(*unit))
        {
            entry.unit = static_cast<int>(*unit);
            entry.hasUnit = true;
        }
    }
    return true;
}

// unit 显式分类：3 → 5 小时窗口，6 → 每周窗口。不能用重置时间排序代替——
// 周期末尾每周窗口可能先于 5 小时窗口重置。
int ClassifiedSlot(const TierEntry& entry)
{
    if (!entry.hasUnit) return -1;
    if (entry.unit == 3) return 0;
    if (entry.unit == 6) return 1;
    return -1;
}

} // namespace

namespace cqt
{

ZhipuUsageSnapshot ZhipuUsageParser::Parse(std::string_view json, long long receivedAtUnixSeconds)
{
    ZhipuUsageSnapshot snapshot;
    snapshot.fetchedAtUnixSeconds = receivedAtUnixSeconds;
    const JsonParseResult parsed = JsonAdapter::Parse(json);
    if (!parsed.success || !parsed.root.IsObject())
    {
        snapshot.errorCode = "ZHIPU_JSON_INVALID";
        snapshot.errorMessage = L"无法解析智谱额度数据。";
        return snapshot;
    }

    const JsonValue* data = parsed.root.Find("data");
    const JsonValue* limits = data && data->IsObject() ? data->Find("limits") : nullptr;
    if (!limits || !limits->IsArray())
    {
        snapshot.errorCode = "ZHIPU_SCHEMA_INVALID";
        snapshot.errorMessage = L"智谱额度数据缺少有效窗口。";
        return snapshot;
    }

    std::optional<TierEntry> fiveHour;
    std::optional<TierEntry> weekly;
    std::vector<TierEntry> unclassified;
    for (const JsonValue& item : *limits->AsArray())
    {
        if (!item.IsObject() || !IsTokenLimitType(item)) continue;
        TierEntry entry;
        if (!ParseTier(item, entry)) continue;
        const int slot = ClassifiedSlot(entry);
        if (slot == 0 && !fiveHour) fiveHour = entry;
        else if (slot == 1 && !weekly) weekly = entry;
        else unclassified.push_back(entry);
    }

    // unit 缺失或无法识别时的兜底：无重置时间的条目优先归 5 小时窗口（5 小时桶
    // 在 0% 等状态下可能没有 reset），其余按重置时间升序补入仍空缺的槽位。
    std::sort(unclassified.begin(), unclassified.end(), [](const TierEntry& left, const TierEntry& right) {
        if (left.hasResetAt != right.hasResetAt) return !left.hasResetAt;
        return left.resetAtUnixSeconds < right.resetAtUnixSeconds;
    });
    for (const TierEntry& entry : unclassified)
    {
        if (!fiveHour) fiveHour = entry;
        else if (!weekly) weekly = entry;
        // 智谱当前最多两条 TOKENS_LIMIT，多余的忽略
    }

    if (!fiveHour && !weekly)
    {
        snapshot.errorCode = "ZHIPU_SCHEMA_INVALID";
        snapshot.errorMessage = L"智谱额度数据没有可用的短期或周窗口。";
        return snapshot;
    }

    if (fiveHour) snapshot.fiveHour = fiveHour->window;
    if (weekly) snapshot.weekly = weekly->window;

    // TIME_LIMIT 是可选的月度 MCP 用量；缺失或字段无效不影响成功状态。
    for (const JsonValue& item : *limits->AsArray())
    {
        if (!item.IsObject()) continue;
        const JsonValue* typeValue = item.Find("type");
        const std::string* type = typeValue ? typeValue->AsString() : nullptr;
        if (!type || *type != "TIME_LIMIT") continue;
        const double* percentage = ValidPercentage(item.Find("percentage"));
        if (!percentage) continue;
        snapshot.monthlyMcpAvailable = true;
        snapshot.monthlyMcpUsedPercent = *percentage;
        break;
    }

    snapshot.success = true;
    return snapshot;
}

} // namespace cqt
