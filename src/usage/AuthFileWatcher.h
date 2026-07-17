#pragma once

#include "usage/CodexAuthReader.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cqt
{

class AuthFileWatcher
{
public:
    AuthFileWatcher();
    ~AuthFileWatcher();
    AuthFileWatcher(const AuthFileWatcher&) = delete;
    AuthFileWatcher& operator=(const AuthFileWatcher&) = delete;

    [[nodiscard]] bool Start(const AuthSearchPaths& paths, std::function<void()> changed);
    void Stop();

private:
    struct DirectoryWatcher;
    void WatchDirectory(DirectoryWatcher& watcher, std::stop_token stopToken);
    void DebounceMain(std::stop_token stopToken);
    void SignalChange();

    std::vector<std::unique_ptr<DirectoryWatcher>> watchers_;
    std::jthread debounceThread_;
    std::function<void()> changed_;
    std::mutex debounceMutex_;
    std::condition_variable_any debounceCondition_;
    unsigned long long changeVersion_ = 0;
    bool running_ = false;
};

} // namespace cqt
