#include "usage/ResetCreditsParser.h"

#include "common/JsonAdapter.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

bool UnixFromSystemTime(const SYSTEMTIME& systemTime, long long& unixSeconds)
{
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&systemTime, &fileTime))
    {
        return false;
    }
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    constexpr unsigned long long kUnixEpochTicks = 116444736000000000ULL;
    if (value.QuadPart < kUnixEpochTicks)
    {
        return false;
    }
    unixSeconds = static_cast<long long>((value.QuadPart - kUnixEpochTicks) / 10000000ULL);
    return true;
}

bool ReadDigits(std::string_view text, std::size_t offset, std::size_t count, unsigned& value)
{
    if (offset + count > text.size()) return false;
    value = 0;
    for (std::size_t index = offset; index < offset + count; ++index)
    {
        const char digit = text[index];
        if (digit < '0' || digit > '9') return false;
        value = value * 10 + static_cast<unsigned>(digit - '0');
    }
    return true;
}

bool ParseRfc3339(std::string_view text, long long& unixSeconds)
{
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T'
        || text[13] != ':' || text[16] != ':') return false;

    unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!ReadDigits(text, 0, 4, year) || !ReadDigits(text, 5, 2, month)
        || !ReadDigits(text, 8, 2, day) || !ReadDigits(text, 11, 2, hour)
        || !ReadDigits(text, 14, 2, minute) || !ReadDigits(text, 17, 2, second)
        || year < 1970 || year > 30827 || month < 1 || month > 12 || day < 1 || day > 31
        || hour > 23 || minute > 59 || second > 59) return false;

    std::size_t position = 19;
    if (position < text.size() && text[position] == '.')
    {
        const std::size_t fractionStart = ++position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') ++position;
        if (position == fractionStart) return false;
    }

    int offsetSign = 0;
    unsigned offsetHour = 0;
    unsigned offsetMinute = 0;
    if (position < text.size() && text[position] == 'Z')
    {
        ++position;
    }
    else if (position < text.size() && (text[position] == '+' || text[position] == '-'))
    {
        offsetSign = text[position] == '+' ? 1 : -1;
        ++position;
        if (!ReadDigits(text, position, 2, offsetHour)
            || position + 2 >= text.size() || text[position + 2] != ':'
            || !ReadDigits(text, position + 3, 2, offsetMinute)
            || offsetHour > 23 || offsetMinute > 59) return false;
        position += 5;
    }
    else return false;
    if (position != text.size()) return false;

    SYSTEMTIME value{};
    value.wYear = static_cast<WORD>(year);
    value.wMonth = static_cast<WORD>(month);
    value.wDay = static_cast<WORD>(day);
    value.wHour = static_cast<WORD>(hour);
    value.wMinute = static_cast<WORD>(minute);
    value.wSecond = static_cast<WORD>(second);
    if (!UnixFromSystemTime(value, unixSeconds)) return false;
    const long long offsetSeconds = static_cast<long long>(offsetHour) * 3600
        + static_cast<long long>(offsetMinute) * 60;
    unixSeconds -= static_cast<long long>(offsetSign) * offsetSeconds;
    return unixSeconds >= 0;
}

bool ParseUnixTimestamp(const cqt::JsonValue& value, long long& unixSeconds)
{
    const double* number = value.AsNumber();
    if (!number || !std::isfinite(*number) || *number < 0.0 || std::floor(*number) != *number)
        return false;
    constexpr double kMaximumUnixSeconds = 253402300799.0;
    if (*number <= kMaximumUnixSeconds)
    {
        unixSeconds = static_cast<long long>(*number);
        return true;
    }
    if (*number <= kMaximumUnixSeconds * 1000.0)
    {
        unixSeconds = static_cast<long long>(*number / 1000.0);
        return true;
    }
    return false;
}

bool ReadAvailableCount(const cqt::JsonValue* value, int& count)
{
    const double* number = value ? value->AsNumber() : nullptr;
    if (!number || !std::isfinite(*number) || *number < 0.0 || std::floor(*number) != *number
        || *number > static_cast<double>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    count = static_cast<int>(*number);
    return true;
}

} // namespace

namespace cqt
{

ResetCreditsSnapshot ResetCreditsParser::Parse(std::string_view json, long long receivedAtUnixSeconds)
{
    ResetCreditsSnapshot snapshot;
    snapshot.fetchedAtUnixSeconds = receivedAtUnixSeconds;
    const JsonParseResult parsed = JsonAdapter::Parse(json);
    if (!parsed.success || !parsed.root.IsObject())
    {
        snapshot.errorCode = "RESET_CREDITS_JSON_INVALID";
        snapshot.errorMessage = L"无法解析重置机会数据。";
        return snapshot;
    }

    if (!ReadAvailableCount(parsed.root.Find("available_count"), snapshot.availableCount))
    {
        snapshot.errorCode = "RESET_CREDITS_SCHEMA_INVALID";
        snapshot.errorMessage = L"重置机会次数格式不兼容。";
        return snapshot;
    }
    const JsonValue* creditsValue = parsed.root.Find("credits");
    const JsonValue::Array* credits = creditsValue ? creditsValue->AsArray() : nullptr;
    if (!credits)
    {
        snapshot.errorCode = "RESET_CREDITS_SCHEMA_INVALID";
        snapshot.errorMessage = L"重置机会列表格式不兼容。";
        return snapshot;
    }

    for (const JsonValue& item : *credits)
    {
        const JsonValue* status = item.Find("status");
        if (!status || !status->IsString() || *status->AsString() != "available")
        {
            continue;
        }
        ResetCredit credit;
        const JsonValue* expiry = item.Find("expires_at");
        if (expiry && expiry->IsString())
        {
            credit.hasValidExpiry = ParseRfc3339(*expiry->AsString(), credit.expiresAtUnixSeconds);
        }
        else if (expiry) credit.hasValidExpiry = ParseUnixTimestamp(*expiry, credit.expiresAtUnixSeconds);
        snapshot.availableCredits.push_back(credit);
    }

    if (snapshot.availableCredits.size() != static_cast<std::size_t>(snapshot.availableCount))
    {
        snapshot.availableCredits.clear();
        snapshot.errorCode = "RESET_CREDITS_COUNT_MISMATCH";
        snapshot.errorMessage = L"重置机会次数与列表不一致。";
        return snapshot;
    }

    std::stable_sort(snapshot.availableCredits.begin(), snapshot.availableCredits.end(),
        [](const ResetCredit& left, const ResetCredit& right)
        {
            if (left.hasValidExpiry != right.hasValidExpiry)
            {
                return left.hasValidExpiry;
            }
            return !left.hasValidExpiry || left.expiresAtUnixSeconds < right.expiresAtUnixSeconds;
        });
    snapshot.success = true;
    return snapshot;
}

} // namespace cqt
