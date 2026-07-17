#include "usage/AuthFileWatcher.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <set>

namespace
{

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

} // namespace

namespace cqt
{

struct AuthFileWatcher::DirectoryWatcher
{
    std::filesystem::path target;
    bool targetPresent = false;
    std::mutex handleMutex;
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::jthread thread;
};

AuthFileWatcher::AuthFileWatcher() = default;

AuthFileWatcher::~AuthFileWatcher()
{
    Stop();
}

bool AuthFileWatcher::Start(const AuthSearchPaths& paths, std::function<void()> changed)
{
    Stop();
    std::set<std::wstring, std::less<>> uniqueTargets;
    for (const auto& candidate : paths.candidates)
    {
        std::error_code ec;
        const std::filesystem::path target = std::filesystem::absolute(candidate, ec).lexically_normal();
        if (ec || target.empty() || target.parent_path().empty()) continue;
        if (!uniqueTargets.insert(Lower(target.wstring())).second) continue;
        auto watcher = std::make_unique<DirectoryWatcher>();
        watcher->target = target;
        watcher->targetPresent = std::filesystem::is_regular_file(target, ec);
        watchers_.push_back(std::move(watcher));
    }

    changed_ = std::move(changed);
    running_ = true;
    debounceThread_ = std::jthread([this](std::stop_token token) { DebounceMain(token); });
    for (auto& watcher : watchers_)
    {
        DirectoryWatcher* pointer = watcher.get();
        watcher->thread = std::jthread([this, pointer](std::stop_token token) {
            WatchDirectory(*pointer, token);
        });
    }
    return !watchers_.empty();
}

void AuthFileWatcher::Stop()
{
    {
        std::scoped_lock lock(debounceMutex_);
        running_ = false;
        changed_ = {};
    }
    for (auto& watcher : watchers_)
    {
        if (watcher->thread.joinable()) watcher->thread.request_stop();
        std::scoped_lock handleLock(watcher->handleMutex);
        if (watcher->handle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(watcher->handle, nullptr);
            CloseHandle(watcher->handle);
            watcher->handle = INVALID_HANDLE_VALUE;
        }
    }
    for (auto& watcher : watchers_)
    {
        if (watcher->thread.joinable()) watcher->thread.join();
        watcher->handle = INVALID_HANDLE_VALUE;
    }
    watchers_.clear();
    if (debounceThread_.joinable())
    {
        debounceThread_.request_stop();
        debounceCondition_.notify_all();
        debounceThread_.join();
    }
}

void AuthFileWatcher::WatchDirectory(DirectoryWatcher& watcher, std::stop_token stopToken)
{
    alignas(DWORD) std::array<std::byte, 16 * 1024> buffer{};
    while (!stopToken.stop_requested())
    {
        std::error_code targetError;
        const bool targetPresent = std::filesystem::is_regular_file(
            watcher.target, targetError);
        if (!targetError && targetPresent != watcher.targetPresent)
        {
            watcher.targetPresent = targetPresent;
            SignalChange();
        }

        std::filesystem::path directory = watcher.target.parent_path();
        std::error_code ec;
        while (!directory.empty() && !std::filesystem::is_directory(directory, ec))
        {
            ec.clear();
            const std::filesystem::path parent = directory.parent_path();
            if (parent == directory)
            {
                directory.clear();
                break;
            }
            directory = parent;
        }
        if (directory.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        const std::filesystem::path relativeTarget = watcher.target.lexically_relative(directory);
        const auto component = relativeTarget.begin();
        if (component == relativeTarget.end()) return;
        const std::wstring watchedName = Lower(component->wstring());

        HANDLE handle = CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        {
            std::scoped_lock handleLock(watcher.handleMutex);
            if (stopToken.stop_requested())
            {
                CloseHandle(handle);
                return;
            }
            watcher.handle = handle;
        }

        DWORD bytes = 0;
        const bool read = ReadDirectoryChangesW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
            &bytes, nullptr, nullptr) != FALSE;
        {
            std::scoped_lock handleLock(watcher.handleMutex);
            if (watcher.handle == handle)
            {
                watcher.handle = INVALID_HANDLE_VALUE;
                CloseHandle(handle);
            }
        }
        if (!read)
        {
            if (!stopToken.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        bool relevantChange = false;
        std::size_t offset = 0;
        while (offset < bytes)
        {
            const auto* information = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            const std::wstring filename(information->FileName,
                information->FileNameLength / sizeof(wchar_t));
            if (Lower(filename) == watchedName) relevantChange = true;
            if (information->NextEntryOffset == 0) break;
            offset += information->NextEntryOffset;
        }
        if (relevantChange)
        {
            // This is either auth.json itself or the next missing directory in
            // its path. Notify the controller and immediately rebind closer to
            // the target so a create-and-write sequence cannot be missed.
            SignalChange();
        }
    }
}

void AuthFileWatcher::SignalChange()
{
    std::scoped_lock lock(debounceMutex_);
    if (!running_) return;
    ++changeVersion_;
    debounceCondition_.notify_one();
}

void AuthFileWatcher::DebounceMain(std::stop_token stopToken)
{
    std::unique_lock lock(debounceMutex_);
    unsigned long long observed = changeVersion_;
    while (!stopToken.stop_requested())
    {
        debounceCondition_.wait(lock, stopToken, [this, observed] {
            return !running_ || changeVersion_ != observed;
        });
        if (!running_ || stopToken.stop_requested()) break;
        observed = changeVersion_;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        while (debounceCondition_.wait_until(lock, stopToken, deadline, [this, observed] {
            return !running_ || changeVersion_ != observed;
        }))
        {
            if (!running_ || stopToken.stop_requested()) return;
            observed = changeVersion_;
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        }
        const std::function<void()> callback = changed_;
        lock.unlock();
        if (callback) callback();
        lock.lock();
    }
}

} // namespace cqt
