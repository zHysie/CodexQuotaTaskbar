#include "settings/Settings.h"
#include "settings/StartupManager.h"
#include "usage/AuthFileWatcher.h"
#include "usage/CodexAuthReader.h"
#include "usage/CodexUsageClient.h"
#include "usage/RefreshController.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace
{

int failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

std::filesystem::path TempRoot()
{
    wchar_t root[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(root)), root);
    const auto path = std::filesystem::path(root)
        / (L"CQT-logic-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(path);
    return path;
}

void Write(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

class BlockingFakeTransport final : public cqt::IHttpTransport
{
public:
    cqt::HttpResponse Get(std::wstring_view, std::wstring_view path,
                          std::string_view, std::string_view) override
    {
        std::unique_lock lock(mutex_);
        ++calls_;
        if (calls_ == 1)
        {
            entered_ = true;
            condition_.notify_all();
            condition_.wait(lock, [this] { return released_; });
        }
        cqt::HttpResponse response;
        response.statusCode = 200;
        if (path == L"/backend-api/wham/usage")
        {
            response.body = R"({"email":"fixture@example.invalid","plan_type":"plus","rate_limit":{"primary_window":{"used_percent":25,"limit_window_seconds":604800},"secondary_window":{"used_percent":10,"limit_window_seconds":18000}}})";
        }
        else
        {
            response.body = R"({"available_count":0,"credits":[]})";
        }
        return response;
    }

    void Cancel() override
    {
        std::scoped_lock lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }

    bool WaitEntered()
    {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(2), [this] { return entered_; });
    }

    void Release()
    {
        std::scoped_lock lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }

    int Calls() const { std::scoped_lock lock(mutex_); return calls_; }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    int calls_ = 0;
    bool entered_ = false;
    bool released_ = false;
};

class RecoveringFakeTransport final : public cqt::IHttpTransport
{
public:
    cqt::HttpResponse Get(std::wstring_view, std::wstring_view path,
                          std::string_view, std::string_view) override
    {
        cqt::HttpResponse response;
        if (path == L"/backend-api/wham/usage")
        {
            const int attempt = ++usageAttempts_;
            if (attempt == 2)
            {
                response.transportError = cqt::TransportError::Timeout;
                response.errorCode = "HTTP_TIMEOUT";
                return response;
            }
            response.statusCode = 200;
            response.body = R"({"email":"fixture@example.invalid","plan_type":"plus","rate_limit":{"primary_window":{"used_percent":25,"limit_window_seconds":604800},"secondary_window":{"used_percent":10,"limit_window_seconds":18000}}})";
        }
        else
        {
            response.statusCode = 200;
            response.body = R"({"available_count":0,"credits":[]})";
        }
        return response;
    }

    void Cancel() override {}

private:
    std::atomic<int> usageAttempts_ = 0;
};

class BackoffFakeTransport final : public cqt::IHttpTransport
{
public:
    cqt::HttpResponse Get(std::wstring_view, std::wstring_view path,
                          std::string_view, std::string_view) override
    {
        cqt::HttpResponse response;
        if (path == L"/backend-api/wham/usage")
        {
            const int attempt = ++usageAttempts_;
            response.statusCode = attempt == 1 ? 429 : 503;
            if (attempt == 1) response.headers.emplace(L"Retry-After", L"120");
        }
        else
        {
            response.statusCode = 200;
            response.body = R"({"available_count":0,"credits":[]})";
        }
        return response;
    }

    void Cancel() override {}

private:
    std::atomic<int> usageAttempts_ = 0;
};

class ResetRateLimitedTransport final : public cqt::IHttpTransport
{
public:
    cqt::HttpResponse Get(std::wstring_view, std::wstring_view path,
                          std::string_view, std::string_view) override
    {
        cqt::HttpResponse response;
        if (path == L"/backend-api/wham/usage")
        {
            ++usageCalls_;
            response.statusCode = 200;
            response.body = R"({"rate_limit":{"primary_window":{"used_percent":25,"limit_window_seconds":604800},"secondary_window":{"used_percent":10,"limit_window_seconds":18000}}})";
        }
        else
        {
            const int call = ++resetCalls_;
            if (call == 1)
            {
                response.statusCode = 429;
                response.headers.emplace(L"Retry-After", L"3600");
            }
            else
            {
                response.statusCode = 200;
                response.body = R"({"available_count":0,"credits":[]})";
            }
        }
        return response;
    }

    void Cancel() override {}
    int UsageCalls() const { return usageCalls_.load(); }
    int ResetCalls() const { return resetCalls_.load(); }

private:
    std::atomic<int> usageCalls_ = 0;
    std::atomic<int> resetCalls_ = 0;
};

void TestSettings(const std::filesystem::path& root)
{
    const auto path = root / L"settings" / L"settings.ini";
    Write(path, "[General]\nRefreshIntervalSeconds=77\nShowFiveHour=0\nShowWeekly=0\nColorMode=Unknown\nUnknownKey=ignored\n");
    auto settings = cqt::Settings::Load(path);
    Check(settings.refreshIntervalSeconds == 180, "settings invalid interval defaults");
    Check(settings.showFiveHour && settings.showWeekly, "settings cannot hide both quotas");
    Check(settings.colorMode == cqt::ColorMode::QuotaAware, "settings unknown color defaults");
    settings.layout = cqt::LayoutMode::Horizontal;
    settings.refreshIntervalSeconds = 600;
    std::wstring error;
    Check(cqt::Settings::Save(path, settings, error), "settings atomic save");
    const auto loaded = cqt::Settings::Load(path);
    Check(loaded.layout == cqt::LayoutMode::Horizontal && loaded.refreshIntervalSeconds == 600,
          "settings persistence");
    Check(cqt::StartupManager::BuildCommand(L"C:\\Program Files\\CodexQuotaTaskbar.exe")
          == L"\"C:\\Program Files\\CodexQuotaTaskbar.exe\"", "startup command quoting");

    const std::wstring testKey = L"Software\\CodexQuotaTaskbar\\Tests\\" + std::to_wstring(GetCurrentProcessId());
    Check(cqt::StartupManager::SetEnabledAt(HKEY_CURRENT_USER, testKey.c_str(), L"StartupTest", true,
          L"C:\\Program Files\\CodexQuotaTaskbar.exe", error), "startup registry enable");
    Check(cqt::StartupManager::IsEnabledAt(HKEY_CURRENT_USER, testKey.c_str(), L"StartupTest",
          L"C:\\Program Files\\CodexQuotaTaskbar.exe"),
          "startup registry value matches current executable");
    Check(!cqt::StartupManager::IsEnabledAt(HKEY_CURRENT_USER, testKey.c_str(), L"StartupTest",
          L"D:\\Moved\\CodexQuotaTaskbar.exe"),
          "startup registry rejects stale executable path");
    Check(cqt::StartupManager::SetEnabledAt(HKEY_CURRENT_USER, testKey.c_str(), L"StartupTest", false,
          L"C:\\Program Files\\CodexQuotaTaskbar.exe", error), "startup registry disable");
    Check(!cqt::StartupManager::IsEnabledAt(HKEY_CURRENT_USER, testKey.c_str(), L"StartupTest",
          L"C:\\Program Files\\CodexQuotaTaskbar.exe"),
          "startup registry value removed");
    RegDeleteTreeW(HKEY_CURRENT_USER, testKey.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\CodexQuotaTaskbar\\Tests");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\CodexQuotaTaskbar");
}

void TestWatcher(const std::filesystem::path& root)
{
    const auto path = root / L"watch" / L"auth.json";
    std::filesystem::create_directories(path.parent_path());
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    cqt::AuthFileWatcher watcher;
    Check(watcher.Start(cqt::AuthSearchPaths{{path}}, [&] {
        std::scoped_lock lock(mutex);
        ++callbacks;
        condition.notify_all();
    }), "auth watcher starts on candidate directory");
    Write(path, "{}");
    Write(path, "{ }\n");
    Write(path, "{  }\n");
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks > 0; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    {
        std::scoped_lock lock(mutex);
        Check(callbacks == 1, "auth watcher debounces contiguous events");
    }

    std::filesystem::remove(path);
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks >= 2; });
        Check(callbacks >= 2, "auth watcher observes deletion");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));

    const auto replacement = path.parent_path() / L"auth-replacement.tmp";
    Write(replacement, "{}");
    std::filesystem::rename(replacement, path);
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks >= 3; });
        Check(callbacks >= 3, "auth watcher observes rename into candidate path");
    }
    watcher.Stop();
}

void TestWatcherBeforeDirectoryExists(const std::filesystem::path& root)
{
    const auto directory = root / L"watch-created-later" / L".codex";
    const auto path = directory / L"auth.json";
    std::filesystem::remove_all(directory.parent_path());
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    cqt::AuthFileWatcher watcher;
    Check(watcher.Start(cqt::AuthSearchPaths{{path}}, [&] {
        std::scoped_lock lock(mutex);
        ++callbacks;
        condition.notify_all();
    }), "auth watcher starts before candidate directory exists");

    Write(path, "{}");
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks > 0; });
        Check(callbacks > 0, "auth watcher observes first login directory creation");
    }

    std::filesystem::remove_all(directory);
    std::this_thread::sleep_for(std::chrono::milliseconds(650));
    int callbacksBeforeRecreate = 0;
    {
        std::scoped_lock lock(mutex);
        callbacksBeforeRecreate = callbacks;
    }
    Write(path, "{ }\n");
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] {
            return callbacks > callbacksBeforeRecreate;
        });
        Check(callbacks > callbacksBeforeRecreate,
              "auth watcher rebinds after candidate directory recreation");
    }
    watcher.Stop();
}

void TestRefreshController(const std::filesystem::path& root)
{
    const auto authPath = root / L"controller" / L"auth.json";
    Write(authPath, R"({"tokens":{"access_token":"fixture-token","account_id":"fixture-account"}})");
    BlockingFakeTransport transport;
    cqt::CodexUsageClient client(transport);
    cqt::CodexAuthReader reader;
    cqt::RefreshController controller(reader, client);
    std::mutex mutex;
    std::condition_variable condition;
    bool ready = false;
    controller.Start(cqt::AuthSearchPaths{{authPath}}, 180, [&] {
        std::scoped_lock lock(mutex);
        ready = true;
        condition.notify_all();
    });
    Check(transport.WaitEntered(), "refresh starts immediately in background");
    Check(!controller.RequestManualRefresh(), "manual refresh ignored while running");
    transport.Release();
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] { return ready; });
    }
    auto result = controller.TakePendingResult();
    Check(result && result->usage.success && result->resetCredits.success, "refresh merges independent subresults");
    Check(transport.Calls() == 2, "refresh performs two sequential requests");
    Check(controller.RequestManualRefresh(), "manual refresh accepted while idle");
    controller.Stop();
}

void TestNetworkRecovery(const std::filesystem::path& root)
{
    const auto authPath = root / L"recovery" / L"auth.json";
    Write(authPath, R"({"tokens":{"access_token":"fixture-token","account_id":"fixture-account"}})");
    RecoveringFakeTransport transport;
    cqt::CodexUsageClient client(transport);
    cqt::CodexAuthReader reader;
    cqt::RefreshController controller(reader, client);
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    controller.Start(cqt::AuthSearchPaths{{authPath}}, 180, [&] {
        std::scoped_lock lock(mutex);
        ++callbacks;
        condition.notify_all();
    });

    const auto waitForCallback = [&](int expected) {
        std::unique_lock lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks >= expected; });
    };

    Check(waitForCallback(1), "network recovery initial callback");
    auto result = controller.TakePendingResult();
    Check(result && result->usage.success, "network recovery starts from success");

    Check(controller.RequestManualRefresh(), "network fault refresh accepted");
    Check(waitForCallback(2), "network fault callback remains responsive");
    result = controller.TakePendingResult();
    Check(result && !result->usage.success && result->usage.errorCode == "HTTP_TIMEOUT",
          "network timeout is surfaced without stopping controller");

    Check(controller.RequestManualRefresh(), "network recovery refresh accepted");
    Check(waitForCallback(3), "network recovery callback remains responsive");
    result = controller.TakePendingResult();
    Check(result && result->usage.success, "network refresh succeeds after timeout");
    controller.Stop();
}

void TestCredentialChangesCoalesce(const std::filesystem::path& root)
{
    const auto authPath = root / L"credential-change" / L"auth.json";
    Write(authPath, R"({"tokens":{"access_token":"fixture-token","account_id":"fixture-account"}})");
    BlockingFakeTransport transport;
    cqt::CodexUsageClient client(transport);
    cqt::CodexAuthReader reader;
    cqt::RefreshController controller(reader, client);
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    controller.Start(cqt::AuthSearchPaths{{authPath}}, 180, [&] {
        std::scoped_lock lock(mutex);
        ++callbacks;
        condition.notify_all();
    });
    Check(transport.WaitEntered(), "credential change test enters running refresh");
    controller.NotifyCredentialsChanged();
    controller.NotifyCredentialsChanged();
    controller.NotifyCredentialsChanged();
    transport.Release();
    {
        std::unique_lock lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks >= 2; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Check(callbacks == 2 && transport.Calls() == 4,
          "credential changes during refresh coalesce into one follow-up cycle");
    controller.Stop();
}

void TestBackoffAndRetryAfter(const std::filesystem::path& root)
{
    const auto authPath = root / L"backoff" / L"auth.json";
    Write(authPath, R"({"tokens":{"access_token":"fixture-token","account_id":"fixture-account"}})");
    BackoffFakeTransport transport;
    cqt::CodexUsageClient client(transport);
    cqt::CodexAuthReader reader;
    cqt::RefreshController controller(reader, client);
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    controller.Start(cqt::AuthSearchPaths{{authPath}}, 180, [&] {
        std::scoped_lock lock(mutex);
        ++callbacks;
        condition.notify_all();
    });

    constexpr int expectedDelays[]{120, 120, 300, 600};
    for (int index = 0; index < 4; ++index)
    {
        {
            std::unique_lock lock(mutex);
            condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks >= index + 1; });
        }
        const auto result = controller.TakePendingResult();
        const int remaining = controller.SecondsUntilNextRefresh();
        Check(result && !result->usage.success
              && remaining >= expectedDelays[index] - 2
              && remaining <= expectedDelays[index],
              index == 0 ? "usage retry-after extends first backoff"
                         : "usage failure advances backoff schedule");
        if (index < 3) Check(controller.RequestManualRefresh(), "manual refresh can advance usage backoff wait");
    }
    controller.Stop();
}

void TestResetRateLimitIsolation(const std::filesystem::path& root)
{
    const auto authPath = root / L"reset-rate-limit" / L"auth.json";
    Write(authPath, R"({"tokens":{"access_token":"fixture-token","account_id":"fixture-account"}})");
    ResetRateLimitedTransport transport;
    cqt::CodexUsageClient client(transport);
    cqt::CodexAuthReader reader;
    cqt::RefreshController controller(reader, client);
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    controller.Start(cqt::AuthSearchPaths{{authPath}}, 180, [&] {
        std::scoped_lock lock(mutex);
        ++callbacks;
        condition.notify_all();
    });
    const auto waitFor = [&](int expected) {
        std::unique_lock lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(3), [&] { return callbacks >= expected; });
    };

    Check(waitFor(1), "reset rate-limit first callback");
    auto result = controller.TakePendingResult();
    Check(result && result->usage.success && result->resetCredits.httpStatusCode == 429,
          "reset 429 does not invalidate usage result");
    Check(controller.RequestManualRefresh(), "manual usage refresh remains available during reset pause");
    Check(waitFor(2), "reset rate-limit second callback");
    result = controller.TakePendingResult();
    Check(result && result->usage.success
          && result->resetCredits.errorCode == "RESET_CREDITS_RATE_LIMIT_PAUSED"
          && transport.UsageCalls() == 2 && transport.ResetCalls() == 1,
          "manual refresh cannot bypass reset endpoint retry-after");
    controller.Stop();
}

void TestStopCancelsActiveRequest(const std::filesystem::path& root)
{
    const auto authPath = root / L"stop" / L"auth.json";
    Write(authPath, R"({"tokens":{"access_token":"fixture-token","account_id":"fixture-account"}})");
    BlockingFakeTransport transport;
    cqt::CodexUsageClient client(transport);
    cqt::CodexAuthReader reader;
    cqt::RefreshController controller(reader, client);
    controller.Start(cqt::AuthSearchPaths{{authPath}}, 180, [] {});
    Check(transport.WaitEntered(), "stop test enters active request");
    const auto started = std::chrono::steady_clock::now();
    controller.Stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    Check(elapsed < 2000, "stop cancels and joins active request within bound");
}

} // namespace

int main()
{
    const auto root = TempRoot();
    TestSettings(root);
    TestWatcher(root);
    TestWatcherBeforeDirectoryExists(root);
    TestRefreshController(root);
    TestNetworkRecovery(root);
    TestCredentialChangesCoalesce(root);
    TestBackoffAndRetryAfter(root);
    TestResetRateLimitIsolation(root);
    TestStopCancelsActiveRequest(root);
    std::filesystem::remove_all(root);
    std::printf("production logic summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
