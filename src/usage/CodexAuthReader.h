#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cqt
{

class SecureString
{
public:
    SecureString() = default;
    explicit SecureString(std::string value);
    ~SecureString();
    SecureString(const SecureString&) = delete;
    SecureString& operator=(const SecureString&) = delete;
    SecureString(SecureString&& other) noexcept;
    SecureString& operator=(SecureString&& other) noexcept;

    [[nodiscard]] std::string_view View() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    void Clear() noexcept;

private:
    std::string value_;
};

struct CodexCredentials
{
    SecureString accessToken;
    SecureString accountId;
    SecureString idToken;
};

struct AuthSearchPaths
{
    std::vector<std::filesystem::path> candidates;
};

struct AuthReadResult
{
    std::optional<CodexCredentials> credentials;
    std::filesystem::path sourcePath;
    std::vector<std::filesystem::path> checkedPaths;
    std::string errorCode;
    std::wstring errorMessage;
};

class CodexAuthReader
{
public:
    static constexpr unsigned long long kMaximumAuthFileBytes = 1024ULL * 1024ULL;

    [[nodiscard]] static AuthSearchPaths BuildSearchPathsFromEnvironment();
    [[nodiscard]] AuthReadResult Read(const AuthSearchPaths& searchPaths) const;
};

} // namespace cqt
