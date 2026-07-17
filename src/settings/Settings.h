#pragma once

#include <filesystem>
#include <string>

namespace cqt
{

enum class LayoutMode { Vertical, Horizontal };
enum class ColorMode { QuotaAware, System, White, Black };

struct SettingsData
{
    int schemaVersion = 1;
    int refreshIntervalSeconds = 180;
    LayoutMode layout = LayoutMode::Vertical;
    bool showFiveHour = true;
    bool showWeekly = true;
    ColorMode colorMode = ColorMode::QuotaAware;
};

class Settings
{
public:
    [[nodiscard]] static std::filesystem::path DefaultPath();
    [[nodiscard]] static SettingsData Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SettingsData& settings,
                                   std::wstring& error);
    [[nodiscard]] static SettingsData Normalize(SettingsData settings);
};

} // namespace cqt
