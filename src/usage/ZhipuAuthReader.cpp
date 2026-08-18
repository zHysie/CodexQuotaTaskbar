#include "usage/ZhipuAuthReader.h"

#include "common/JsonAdapter.h"
#include "common/StringUtils.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <limits>

namespace
{

std::wstring EnvironmentValue(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
    {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required)
    {
        return {};
    }
    value.resize(written);
    return value;
}

std::wstring ComparablePath(const std::filesystem::path& path)
{
    std::wstring value = path.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

bool ReadFileBounded(const std::filesystem::path& path, std::string& content, std::string& errorCode)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        errorCode = GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
            ? "ZHIPU_AUTH_NOT_FOUND" : "ZHIPU_AUTH_OPEN_FAILED";
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0
        || static_cast<unsigned long long>(size.QuadPart) > cqt::ZhipuAuthReader::kMaximumSettingsFileBytes)
    {
        CloseHandle(file);
        errorCode = "ZHIPU_AUTH_FILE_TOO_LARGE";
        return false;
    }

    content.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD total = 0;
    while (total < content.size())
    {
        const DWORD remaining = static_cast<DWORD>(std::min<std::size_t>(
            content.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file, content.data() + total, remaining, &read, nullptr))
        {
            CloseHandle(file);
            errorCode = "ZHIPU_AUTH_READ_FAILED";
            return false;
        }
        if (read == 0)
        {
            break;
        }
        total += read;
    }
    CloseHandle(file);
    content.resize(total);
    return true;
}

// 智谱额度站点由 ANTHROPIC_BASE_URL 判定：bigmodel.cn 与 z.ai 两个域共用同一路径
// 与响应结构（与智谱官方插件、cc-switch 的路由规则一致）；其他域一律不支持，
// 避免把 API Key 发往无关主机。
std::wstring QuotaHostForBaseUrl(const std::string& baseUrl)
{
    std::wstring wide = cqt::Utf8ToWide(baseUrl);
    std::transform(wide.begin(), wide.end(), wide.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    if (wide.find(L"bigmodel.cn") != std::wstring::npos) return L"open.bigmodel.cn";
    if (wide.find(L"z.ai") != std::wstring::npos) return L"api.z.ai";
    return {};
}

void SecureClear(std::string& value)
{
    if (!value.empty())
    {
        SecureZeroMemory(value.data(), value.size());
        value.clear();
    }
}

} // namespace

namespace cqt
{

ZhipuAuthSearchPaths ZhipuAuthReader::BuildSearchPathsFromEnvironment()
{
    ZhipuAuthSearchPaths paths;
    if (const std::wstring configDir = EnvironmentValue(L"CLAUDE_CONFIG_DIR"); !configDir.empty())
    {
        paths.candidates.emplace_back(std::filesystem::path(configDir) / L"settings.json");
    }
    if (const std::wstring userProfile = EnvironmentValue(L"USERPROFILE"); !userProfile.empty())
    {
        const std::filesystem::path fallback =
            std::filesystem::path(userProfile) / L".claude" / L"settings.json";
        const std::wstring comparable = ComparablePath(fallback);
        const bool duplicate = std::any_of(paths.candidates.begin(), paths.candidates.end(),
            [&](const auto& existing) { return ComparablePath(existing) == comparable; });
        if (!duplicate)
        {
            paths.candidates.push_back(fallback);
        }
    }
    return paths;
}

ZhipuAuthReadResult ZhipuAuthReader::Read(const ZhipuAuthSearchPaths& searchPaths) const
{
    ZhipuAuthReadResult result;
    for (const auto& path : searchPaths.candidates)
    {
        const std::wstring comparable = ComparablePath(path);
        if (std::any_of(result.checkedPaths.begin(), result.checkedPaths.end(),
            [&](const auto& existing) { return ComparablePath(existing) == comparable; }))
        {
            continue;
        }
        result.checkedPaths.push_back(path);

        std::string raw;
        std::string readError;
        if (!ReadFileBounded(path, raw, readError))
        {
            if (readError == "ZHIPU_AUTH_NOT_FOUND")
            {
                continue;
            }
            result.errorCode = readError;
            result.errorMessage = readError == "ZHIPU_AUTH_FILE_TOO_LARGE"
                ? L"智谱配置文件超过安全大小限制。" : L"无法安全读取智谱配置文件。";
            SecureClear(raw);
            return result;
        }

        JsonParseResult parsed = JsonAdapter::Parse(raw);
        SecureClear(raw);
        if (!parsed.success || !parsed.root.IsObject())
        {
            result.errorCode = "ZHIPU_AUTH_JSON_INVALID";
            result.errorMessage = L"智谱配置文件不是有效 JSON。";
            return result;
        }

        const JsonValue* env = parsed.root.Find("env");
        const JsonValue* token = env ? env->Find("ANTHROPIC_AUTH_TOKEN") : nullptr;
        const std::string* apiKey = token ? token->AsString() : nullptr;
        if (!apiKey || apiKey->empty())
        {
            result.errorCode = "ZHIPU_AUTH_TOKEN_MISSING";
            result.errorMessage = L"智谱配置中没有 API Key。";
            return result;
        }

        const JsonValue* baseUrlValue = env ? env->Find("ANTHROPIC_BASE_URL") : nullptr;
        const std::string* baseUrl = baseUrlValue ? baseUrlValue->AsString() : nullptr;
        const std::wstring quotaHost = baseUrl ? QuotaHostForBaseUrl(*baseUrl) : std::wstring();
        if (quotaHost.empty())
        {
            result.errorCode = "ZHIPU_BASE_URL_UNSUPPORTED";
            result.errorMessage = L"当前 Claude 配置的不是智谱服务。";
            return result;
        }

        ZhipuCredentials credentials;
        credentials.apiKey = SecureString(*apiKey);
        credentials.quotaHost = quotaHost;
        result.sourcePath = path;
        result.credentials.emplace(std::move(credentials));
        return result;
    }

    result.errorCode = "ZHIPU_AUTH_NOT_FOUND";
    result.errorMessage = L"未找到智谱配置。";
    return result;
}

} // namespace cqt
