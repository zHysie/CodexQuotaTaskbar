#pragma once

#include "usage/CodexAuthReader.h"
#include "usage/CodexUsageClient.h"
#include "usage/UsageModels.h"
#include "usage/ZhipuAuthReader.h"
#include "usage/ZhipuUsageClient.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace cqt
{

class RefreshController
{
public:
    RefreshController(CodexAuthReader& authReader, CodexUsageClient& usageClient,
                      ZhipuAuthReader& zhipuAuthReader, ZhipuUsageClient& zhipuUsageClient);
    ~RefreshController();
    RefreshController(const RefreshController&) = delete;
    RefreshController& operator=(const RefreshController&) = delete;

    void Start(AuthSearchPaths searchPaths, ZhipuAuthSearchPaths zhipuSearchPaths,
               QuotaProvider provider, int intervalSeconds, std::function<void()> resultReady);
    void Stop();
    [[nodiscard]] bool RequestManualRefresh();
    void NotifyCredentialsChanged();
    void SetRefreshInterval(int seconds);
    void SetActiveProvider(QuotaProvider provider);
    [[nodiscard]] QuotaProvider ActiveProvider() const;
    [[nodiscard]] bool IsRefreshing() const;
    [[nodiscard]] int SecondsUntilNextRefresh() const;
    [[nodiscard]] std::optional<RefreshResult> TakePendingResult();

private:
    void ThreadMain(std::stop_token stopToken);

    CodexAuthReader& authReader_;
    CodexUsageClient& usageClient_;
    ZhipuAuthReader& zhipuAuthReader_;
    ZhipuUsageClient& zhipuUsageClient_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::jthread worker_;
    AuthSearchPaths searchPaths_;
    ZhipuAuthSearchPaths zhipuSearchPaths_;
    QuotaProvider activeProvider_ = QuotaProvider::Codex;
    std::function<void()> resultReady_;
    std::optional<RefreshResult> pendingResult_;
    std::chrono::steady_clock::time_point nextDue_{};
    long long nextResetEligibleUnixSeconds_ = 0;
    unsigned long long credentialVersion_ = 0;
    int intervalSeconds_ = 180;
    int codexConsecutiveFailures_ = 0;
    int zhipuConsecutiveFailures_ = 0;
    bool pending_ = false;
    bool running_ = false;
    bool accepting_ = false;
};

} // namespace cqt
