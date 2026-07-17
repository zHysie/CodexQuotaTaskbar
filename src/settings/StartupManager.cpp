#include "settings/StartupManager.h"

#include <windows.h>

#include <vector>

namespace
{

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"CodexQuotaTaskbar";

} // namespace

namespace cqt
{

std::wstring StartupManager::BuildCommand(const std::filesystem::path& executable)
{
    return L"\"" + executable.wstring() + L"\"";
}

bool StartupManager::IsEnabled(const std::filesystem::path& executable)
{
    return IsEnabledAt(HKEY_CURRENT_USER, kRunKey, kValueName, executable);
}

bool StartupManager::IsEnabledAt(HKEY root, const wchar_t* subkey, const wchar_t* valueName,
                                 const std::filesystem::path& executable)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ,
                     &type, nullptr, &size) != ERROR_SUCCESS
        || size < sizeof(wchar_t) || size % sizeof(wchar_t) != 0)
    {
        return false;
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ,
                     &type, buffer.data(), &size) != ERROR_SUCCESS)
    {
        return false;
    }
    std::wstring command(buffer.data());
    const std::wstring expected = BuildCommand(executable);
    return !executable.empty()
        && CompareStringOrdinal(command.c_str(), static_cast<int>(command.size()),
                                expected.c_str(), static_cast<int>(expected.size()), TRUE)
            == CSTR_EQUAL;
}

bool StartupManager::SetEnabled(bool enabled, const std::filesystem::path& executable, std::wstring& error)
{
    return SetEnabledAt(HKEY_CURRENT_USER, kRunKey, kValueName, enabled, executable, error);
}

bool StartupManager::SetEnabledAt(HKEY root, const wchar_t* subkey, const wchar_t* valueName,
                                  bool enabled, const std::filesystem::path& executable, std::wstring& error)
{
    HKEY key = nullptr;
    const LSTATUS opened = RegCreateKeyExW(root, subkey, 0, nullptr, 0,
                                           KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS)
    {
        error = L"无法打开当前用户开机启动设置。";
        return false;
    }
    LSTATUS status = ERROR_SUCCESS;
    if (enabled)
    {
        const std::wstring command = BuildCommand(executable);
        status = RegSetValueExW(key, valueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        error = enabled ? L"无法启用当前用户开机启动。" : L"无法关闭当前用户开机启动。";
        return false;
    }
    return true;
}

} // namespace cqt
