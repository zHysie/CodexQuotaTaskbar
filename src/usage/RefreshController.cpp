#include "usage/RefreshController.h"

#include "usage/HttpPolicy.h"

#include <algorithm>
#include <ctime>

namespace
{

long long UnixNow()
{
    return static_cast<long long>(std::time(nullptr));
}

template <typename Snapshot>
void SetNotAttempted(Snapshot& snapshot, const char* code, const wchar_t* message)
{
    snapshot.errorCode = code;
    snapshot.errorMessage = message;
}

} // namespace

namespace cqt
{

RefreshController::RefreshController(CodexAuthReader& authReader, CodexUsageClient& usageClient)
    : authReader_(authReader), usageClient_(usageClient)
{
}

RefreshController::~RefreshController()
{
    Stop();
}

void RefreshController::Start(AuthSearchPaths searchPaths, int intervalSeconds, std::function<void()> resultReady)
{
    std::scoped_lock lock(mutex_);
    if (accepting_) return;
    searchPaths_ = std::move(searchPaths);
    intervalSeconds_ = intervalSeconds;
    resultReady_ = std::move(resultReady);
    accepting_ = true;
    pending_ = true;
    nextDue_ = std::chrono::steady_clock::now();
    worker_ = std::jthread([this](std::stop_token token) { ThreadMain(token); });
}

void RefreshController::Stop()
{
    {
        std::scoped_lock lock(mutex_);
        if (!accepting_ && !worker_.joinable()) return;
        accepting_ = false;
        pending_ = false;
        resultReady_ = {};
    }
    if (worker_.joinable())
    {
        worker_.request_stop();
        usageClient_.Cancel();
        condition_.notify_all();
        worker_.join();
    }
    std::scoped_lock lock(mutex_);
    running_ = false;
    pendingResult_.reset();
}

bool RefreshController::RequestManualRefresh()
{
    std::scoped_lock lock(mutex_);
    if (!accepting_ || running_ || pending_) return false;
    pending_ = true;
    condition_.notify_one();
    return true;
}

void RefreshController::NotifyCredentialsChanged()
{
    std::scoped_lock lock(mutex_);
    if (!accepting_) return;
    ++credentialVersion_;
    if (!running_) pending_ = true;
    condition_.notify_one();
}

void RefreshController::SetRefreshInterval(int seconds)
{
    std::scoped_lock lock(mutex_);
    intervalSeconds_ = seconds;
    if (!running_)
    {
        nextDue_ = std::chrono::steady_clock::now() + std::chrono::seconds(intervalSeconds_);
        condition_.notify_one();
    }
}

bool RefreshController::IsRefreshing() const
{
    std::scoped_lock lock(mutex_);
    return running_;
}

int RefreshController::SecondsUntilNextRefresh() const
{
    std::scoped_lock lock(mutex_);
    if (running_ || pending_) return 0;
    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        nextDue_ - std::chrono::steady_clock::now()).count();
    return static_cast<int>(std::max<long long>(0, remaining));
}

std::optional<RefreshResult> RefreshController::TakePendingResult()
{
    std::scoped_lock lock(mutex_);
    std::optional<RefreshResult> result = std::move(pendingResult_);
    pendingResult_.reset();
    return result;
}

void RefreshController::ThreadMain(std::stop_token stopToken)
{
    std::unique_lock lock(mutex_);
    while (!stopToken.stop_requested())
    {
        condition_.wait_until(lock, stopToken, nextDue_, [this] {
            return pending_ || !accepting_;
        });
        if (stopToken.stop_requested() || !accepting_) break;
        if (!pending_ && std::chrono::steady_clock::now() < nextDue_) continue;

        pending_ = false;
        running_ = true;
        const unsigned long long versionAtStart = credentialVersion_;
        const AuthSearchPaths paths = searchPaths_;
        lock.unlock();

        RefreshResult refresh;
        const long long now = UnixNow();
        AuthReadResult auth = authReader_.Read(paths);
        std::optional<long long> usageRetryAfter;
        std::optional<long long> resetRetryAfter;
        if (!auth.credentials)
        {
            refresh.usage.errorCode = auth.errorCode;
            refresh.usage.errorMessage = auth.errorMessage;
            refresh.usage.fetchedAtUnixSeconds = now;
            SetNotAttempted(refresh.resetCredits, "RESET_CREDITS_NOT_ATTEMPTED", L"没有可用登录信息，未请求重置机会。");
        }
        else
        {
            auto usage = usageClient_.FetchUsage(*auth.credentials, now);
            refresh.usage = std::move(usage.snapshot);
            usageRetryAfter = usage.retryAfterSeconds;
            const bool authInvalid = refresh.usage.httpStatusCode == 401 || refresh.usage.httpStatusCode == 403;
            if (stopToken.stop_requested() || authInvalid)
            {
                SetNotAttempted(refresh.resetCredits, "RESET_CREDITS_NOT_ATTEMPTED",
                    authInvalid ? L"登录已失效，未请求重置机会。" : L"应用正在退出。");
            }
            else if (now < nextResetEligibleUnixSeconds_)
            {
                SetNotAttempted(refresh.resetCredits, "RESET_CREDITS_RATE_LIMIT_PAUSED",
                    L"重置机会接口仍在暂停期。" );
            }
            else
            {
                auto reset = usageClient_.FetchResetCredits(*auth.credentials, now);
                refresh.resetCredits = std::move(reset.snapshot);
                resetRetryAfter = reset.retryAfterSeconds;
            }
        }

        lock.lock();
        running_ = false;
        if (!accepting_ || stopToken.stop_requested()) break;

        const auto completed = std::chrono::steady_clock::now();
        if (refresh.usage.success)
        {
            consecutiveFailures_ = 0;
            nextDue_ = completed + std::chrono::seconds(intervalSeconds_);
        }
        else
        {
            ++consecutiveFailures_;
            long long delay = UsageBackoffSeconds(consecutiveFailures_);
            if (refresh.usage.httpStatusCode == 429)
                delay = EffectiveRetryDelaySeconds(usageRetryAfter, delay);
            nextDue_ = completed + std::chrono::seconds(delay);
        }
        if (refresh.resetCredits.httpStatusCode == 429)
        {
            const long long delay = EffectiveRetryDelaySeconds(resetRetryAfter, 60);
            nextResetEligibleUnixSeconds_ = now + delay;
        }
        refresh.usage.fetchedAtUnixSeconds = refresh.usage.fetchedAtUnixSeconds == 0 ? now : refresh.usage.fetchedAtUnixSeconds;
        refresh.resetCredits.fetchedAtUnixSeconds = refresh.resetCredits.fetchedAtUnixSeconds == 0 ? now : refresh.resetCredits.fetchedAtUnixSeconds;
        pendingResult_ = std::move(refresh);
        if (credentialVersion_ != versionAtStart) pending_ = true;
        const std::function<void()> callback = resultReady_;
        lock.unlock();
        if (callback) callback();
        lock.lock();
    }
    running_ = false;
}

} // namespace cqt
