#include "usage/CodexAuthReader.h"

#include "common/JsonAdapter.h"

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
            ? "AUTH_NOT_FOUND" : "AUTH_OPEN_FAILED";
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0
        || static_cast<unsigned long long>(size.QuadPart) > cqt::CodexAuthReader::kMaximumAuthFileBytes)
    {
        CloseHandle(file);
        errorCode = "AUTH_FILE_TOO_LARGE";
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
            errorCode = "AUTH_READ_FAILED";
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

const std::string* StringAt(const cqt::JsonValue* object, std::string_view key)
{
    const cqt::JsonValue* value = object ? object->Find(key) : nullptr;
    return value ? value->AsString() : nullptr;
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

SecureString::SecureString(std::string value) : value_(std::move(value)) {}
SecureString::~SecureString() { Clear(); }

SecureString::SecureString(SecureString&& other) noexcept : value_(std::move(other.value_))
{
    other.Clear();
}

SecureString& SecureString::operator=(SecureString&& other) noexcept
{
    if (this != &other)
    {
        Clear();
        value_ = std::move(other.value_);
        other.Clear();
    }
    return *this;
}

std::string_view SecureString::View() const noexcept { return value_; }
bool SecureString::Empty() const noexcept { return value_.empty(); }

void SecureString::Clear() noexcept
{
    if (!value_.empty())
    {
        SecureZeroMemory(value_.data(), value_.size());
        value_.clear();
    }
}

AuthSearchPaths CodexAuthReader::BuildSearchPathsFromEnvironment()
{
    AuthSearchPaths paths;
    if (const std::wstring codexHome = EnvironmentValue(L"CODEX_HOME"); !codexHome.empty())
    {
        paths.candidates.emplace_back(std::filesystem::path(codexHome) / L"auth.json");
    }
    if (const std::wstring userProfile = EnvironmentValue(L"USERPROFILE"); !userProfile.empty())
    {
        const std::filesystem::path fallback = std::filesystem::path(userProfile) / L".codex" / L"auth.json";
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

AuthReadResult CodexAuthReader::Read(const AuthSearchPaths& searchPaths) const
{
    AuthReadResult result;
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
            if (readError == "AUTH_NOT_FOUND")
            {
                continue;
            }
            result.errorCode = readError;
            result.errorMessage = readError == "AUTH_FILE_TOO_LARGE"
                ? L"Codex 登录文件超过安全大小限制。" : L"无法安全读取 Codex 登录文件。";
            SecureClear(raw);
            return result;
        }

        JsonParseResult parsed = JsonAdapter::Parse(raw);
        SecureClear(raw);
        if (!parsed.success || !parsed.root.IsObject())
        {
            result.errorCode = "AUTH_JSON_INVALID";
            result.errorMessage = L"Codex 登录文件不是有效 JSON。";
            return result;
        }

        const JsonValue* tokens = parsed.root.Find("tokens");
        const std::string* accessToken = StringAt(tokens, "access_token");
        const std::string* accountId = StringAt(tokens, "account_id");
        if (!accountId)
        {
            accountId = StringAt(&parsed.root, "account_id");
        }
        const std::string* idToken = StringAt(tokens, "id_token");
        if (!accessToken || accessToken->empty())
        {
            result.errorCode = "AUTH_ACCESS_TOKEN_MISSING";
            result.errorMessage = L"Codex 登录文件缺少访问凭证。";
            return result;
        }

        CodexCredentials credentials;
        credentials.accessToken = SecureString(*accessToken);
        if (accountId)
        {
            credentials.accountId = SecureString(*accountId);
        }
        if (idToken)
        {
            credentials.idToken = SecureString(*idToken);
        }
        result.sourcePath = path;
        result.credentials.emplace(std::move(credentials));
        return result;
    }

    result.errorCode = "AUTH_NOT_FOUND";
    result.errorMessage = L"未找到 Codex 登录信息。";
    return result;
}

} // namespace cqt
