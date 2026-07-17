#include "settings/Settings.h"

#include <windows.h>

#include <array>
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>

namespace
{

std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int ParseInteger(const std::map<std::string, std::string, std::less<>>& values,
                 const char* key, int fallback)
{
    const auto iterator = values.find(key);
    if (iterator == values.end()) return fallback;
    int result = fallback;
    const std::string& text = iterator->second;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? result : fallback;
}

const char* LayoutName(cqt::LayoutMode mode)
{
    return mode == cqt::LayoutMode::Horizontal ? "Horizontal" : "Vertical";
}

const char* ColorName(cqt::ColorMode mode)
{
    switch (mode)
    {
    case cqt::ColorMode::System: return "System";
    case cqt::ColorMode::White: return "White";
    case cqt::ColorMode::Black: return "Black";
    default: return "QuotaAware";
    }
}

} // namespace

namespace cqt
{

std::filesystem::path Settings::DefaultPath()
{
    const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (required == 0) return {};
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"APPDATA", value.data(), required);
    if (written == 0 || written >= required) return {};
    value.resize(written);
    return std::filesystem::path(value) / L"CodexQuotaTaskbar" / L"settings.ini";
}

SettingsData Settings::Normalize(SettingsData settings)
{
    constexpr std::array allowed{60, 180, 300, 600, 1800};
    if (std::find(allowed.begin(), allowed.end(), settings.refreshIntervalSeconds) == allowed.end())
    {
        settings.refreshIntervalSeconds = 180;
    }
    if (!settings.showFiveHour && !settings.showWeekly)
    {
        settings.showFiveHour = true;
        settings.showWeekly = true;
    }
    settings.schemaVersion = 1;
    return settings;
}

SettingsData Settings::Load(const std::filesystem::path& path)
{
    SettingsData settings;
    std::ifstream file(path, std::ios::binary);
    std::map<std::string, std::string, std::less<>> values;
    bool inGeneral = false;
    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']')
        {
            inGeneral = line == "[General]";
            continue;
        }
        if (!inGeneral) continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        values[Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
    }

    settings.schemaVersion = ParseInteger(values, "SchemaVersion", 1);
    settings.refreshIntervalSeconds = ParseInteger(values, "RefreshIntervalSeconds", 180);
    if (const auto iterator = values.find("Layout"); iterator != values.end())
        settings.layout = iterator->second == "Horizontal" ? LayoutMode::Horizontal : LayoutMode::Vertical;
    settings.showFiveHour = ParseInteger(values, "ShowFiveHour", 1) != 0;
    settings.showWeekly = ParseInteger(values, "ShowWeekly", 1) != 0;
    if (const auto iterator = values.find("ColorMode"); iterator != values.end())
    {
        if (iterator->second == "System") settings.colorMode = ColorMode::System;
        else if (iterator->second == "White") settings.colorMode = ColorMode::White;
        else if (iterator->second == "Black") settings.colorMode = ColorMode::Black;
        else settings.colorMode = ColorMode::QuotaAware;
    }
    return Normalize(settings);
}

bool Settings::Save(const std::filesystem::path& path, const SettingsData& input, std::wstring& error)
{
    const SettingsData settings = Normalize(input);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = L"无法创建设置目录。";
        return false;
    }
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = L"无法写入临时设置文件。";
            return false;
        }
        file << "[General]\r\n"
             << "SchemaVersion=1\r\n"
             << "RefreshIntervalSeconds=" << settings.refreshIntervalSeconds << "\r\n"
             << "Layout=" << LayoutName(settings.layout) << "\r\n"
             << "ShowFiveHour=" << (settings.showFiveHour ? 1 : 0) << "\r\n"
             << "ShowWeekly=" << (settings.showWeekly ? 1 : 0) << "\r\n"
             << "ColorMode=" << ColorName(settings.colorMode) << "\r\n";
        if (!file)
        {
            error = L"无法完整写入设置。";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        error = L"无法原子替换设置文件。";
        return false;
    }
    return true;
}

} // namespace cqt
