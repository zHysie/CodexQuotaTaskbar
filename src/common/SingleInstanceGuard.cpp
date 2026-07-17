#include "common/SingleInstanceGuard.h"

namespace cqt
{

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (mutex_) CloseHandle(mutex_);
}

bool SingleInstanceGuard::Acquire()
{
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\CodexQuotaTaskbar.Singleton");
    if (!mutex_) return false;
    alreadyRunning_ = GetLastError() == ERROR_ALREADY_EXISTS;
    return true;
}

} // namespace cqt
