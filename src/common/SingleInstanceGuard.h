#pragma once

#include <windows.h>

namespace cqt
{

class SingleInstanceGuard
{
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
    [[nodiscard]] bool Acquire();
    [[nodiscard]] bool AlreadyRunning() const noexcept { return alreadyRunning_; }

private:
    HANDLE mutex_ = nullptr;
    bool alreadyRunning_ = false;
};

} // namespace cqt
