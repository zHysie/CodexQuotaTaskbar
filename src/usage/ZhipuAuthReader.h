#pragma once

#include "usage/CodexAuthReader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cqt
{

struct ZhipuCredentials
{
    SecureString apiKey;
    std::wstring quotaHost;  // open.bigmodel.cn 或 api.z.ai
};

struct ZhipuAuthSearchPaths
{
    std::vector<std::filesystem::path> candidates;
};

struct ZhipuAuthReadResult
{
    std::optional<ZhipuCredentials> credentials;
    std::filesystem::path sourcePath;
    std::vector<std::filesystem::path> checkedPaths;
    std::string errorCode;
    std::wstring errorMessage;
};

class ZhipuAuthReader
{
public:
    static constexpr unsigned long long kMaximumSettingsFileBytes = 1024ULL * 1024ULL;

    [[nodiscard]] static ZhipuAuthSearchPaths BuildSearchPathsFromEnvironment();
    [[nodiscard]] ZhipuAuthReadResult Read(const ZhipuAuthSearchPaths& searchPaths) const;
};

} // namespace cqt
