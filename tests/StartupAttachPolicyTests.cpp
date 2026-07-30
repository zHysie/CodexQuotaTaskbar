#include "taskbar/StartupAttachPolicy.h"
#include "taskbar/TaskbarHost.h"

#include <cstdio>
#include <vector>

namespace
{

int failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

} // namespace

int main()
{
    using cqt::TaskbarProbeFailure;

    Check(cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::TaskbarUnavailable),
          "missing taskbar is transient during logon");
    Check(cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::NotificationAreaUnavailable),
          "missing notification area is transient during logon");
    Check(cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::AutomationTreeUnavailable),
          "incomplete automation tree is transient during logon");
    Check(cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::TaskButtonBoundaryUnavailable),
          "incomplete task buttons are transient during logon");
    Check(!cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::UnsupportedTaskbarLayout),
          "unsupported taskbar layout fails immediately");
    Check(!cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::InsufficientSafeSpace),
          "insufficient taskbar space fails immediately");
    Check(!cqt::IsTransientTaskbarProbeFailure(TaskbarProbeFailure::InsufficientExternalSafeSpace),
          "external obstacle exhaustion fails immediately");

    Check(cqt::kStartupAttachPolicy.ShouldRetry(true, 1),
          "first transient startup failure retries");
    Check(!cqt::kStartupAttachPolicy.ShouldRetry(false, 1),
          "permanent startup failure does not retry");
    Check(!cqt::kStartupAttachPolicy.ShouldRetry(
              true, cqt::kStartupAttachPolicy.maximumAttempts),
          "startup retries are bounded");
    Check(cqt::kStartupAttachPolicy.DelayBeforeNextAttempt(1) == 1500,
          "first startup retry delay");
    Check(cqt::kStartupAttachPolicy.DelayBeforeNextAttempt(2) == 3000,
          "later startup retry delay");

    int attempts = 0;
    std::vector<ULONGLONG> delays;
    const bool recovered = cqt::RunStartupAttachSequence(
        cqt::kStartupAttachPolicy,
        [&attempts](bool& retryable) {
            ++attempts;
            retryable = true;
            return attempts == 3;
        },
        [&delays](ULONGLONG delay) {
            delays.push_back(delay);
            return true;
        });
    Check(recovered && attempts == 3, "transient startup failures recover");
    Check(delays == std::vector<ULONGLONG>{1500, 3000},
          "startup recovery uses bounded staged delays");

    attempts = 0;
    delays.clear();
    const bool permanentFailure = cqt::RunStartupAttachSequence(
        cqt::kStartupAttachPolicy,
        [&attempts](bool& retryable) {
            ++attempts;
            retryable = false;
            return false;
        },
        [&delays](ULONGLONG delay) {
            delays.push_back(delay);
            return true;
        });
    Check(!permanentFailure && attempts == 1 && delays.empty(),
          "permanent startup failure is reported without delay");

    attempts = 0;
    delays.clear();
    const bool exhausted = cqt::RunStartupAttachSequence(
        cqt::kStartupAttachPolicy,
        [&attempts](bool& retryable) {
            ++attempts;
            retryable = true;
            return false;
        },
        [&delays](ULONGLONG delay) {
            delays.push_back(delay);
            return true;
        });
    Check(!exhausted && attempts == cqt::kStartupAttachPolicy.maximumAttempts,
          "transient startup retries stop at the configured bound");
    Check(delays.size() == static_cast<std::size_t>(
              cqt::kStartupAttachPolicy.maximumAttempts - 1),
          "bounded startup attempts do not wait after the final failure");

    std::printf("startup attach policy summary: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
