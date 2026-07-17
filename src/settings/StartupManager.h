#pragma once

#include <filesystem>
#include <string>
#include <windows.h>

namespace cqt
{

class StartupManager
{
public:
    [[nodiscard]] static bool IsEnabled(const std::filesystem::path& executable);
    [[nodiscard]] static bool SetEnabled(bool enabled, const std::filesystem::path& executable,
                                         std::wstring& error);
    [[nodiscard]] static std::wstring BuildCommand(const std::filesystem::path& executable);
    [[nodiscard]] static bool IsEnabledAt(HKEY root, const wchar_t* subkey, const wchar_t* valueName,
                                          const std::filesystem::path& executable);
    [[nodiscard]] static bool SetEnabledAt(HKEY root, const wchar_t* subkey, const wchar_t* valueName,
                                           bool enabled, const std::filesystem::path& executable,
                                           std::wstring& error);
};

} // namespace cqt
