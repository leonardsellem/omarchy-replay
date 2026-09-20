#include "work_budget.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <time.h>
#include <utility>

namespace replay {
namespace {
constexpr std::int64_t nsPerMs = 1000000;

std::int64_t clockNs(clockid_t clock) {
    timespec value{};
    if (clock_gettime(clock, &value) != 0)
        throw std::runtime_error("Cannot read CPU budget clock");
    return std::int64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}

WorkBudget::Sample linuxSample() {
    const auto cpu = clockNs(CLOCK_PROCESS_CPUTIME_ID);
    return {clockNs(CLOCK_MONOTONIC), cpu};
}

void linuxSleep(std::int64_t ns) {
    const timespec requested{ns / 1000000000, ns % 1000000000};
    // Do not retry EINTR here: the caller checks its stop request immediately.
    if (nanosleep(&requested, nullptr) != 0 && errno != EINTR)
        throw std::runtime_error("CPU budget sleep failed");
}
} // namespace

WorkBudget::WorkBudget(WorkBudgetOptions options, CancelCheck cancel,
                       SampleClock clock, Sleeper sleep)
    : options_(options), cancel_(std::move(cancel)),
      clock_(clock ? std::move(clock) : linuxSample),
      sleep_(sleep ? std::move(sleep) : linuxSleep) {
    if (!std::isfinite(options_.cpuPercent) || options_.cpuPercent < 0 || options_.cpuPercent > 100 ||
        options_.maxWallMs < 1 || options_.maxWallMs > 60000 ||
        options_.maxSleepMs < 1 || options_.maxSleepMs > 20 ||
        !std::isfinite(options_.burstCpuMs) || options_.burstCpuMs < 0 || options_.burstCpuMs > 100)
        throw std::invalid_argument("Invalid cooperative CPU budget limits");
    cpuPercent_ = options_.cpuPercent;
    refreshCpuPercent();
    start_ = accounted_ = lastExit_ = clock_();
    if (start_.wallNs < 0 || start_.cpuNs < 0)
        throw std::runtime_error("Invalid CPU budget clock sample");
}

WorkBudget::Sample WorkBudget::readSample() {
    const Sample current = clock_();
    if (current.wallNs < accounted_.wallNs || current.cpuNs < accounted_.cpuNs)
        throw std::runtime_error("CPU budget clock moved backwards");
    return current;
}

void WorkBudget::account(Sample current) {
    const auto wallDelta = current.wallNs - accounted_.wallNs;
    const auto cpuDelta = current.cpuNs - accounted_.cpuNs;
    if (cpuPercent_ > 0)
        debtNs_ = std::max(0.0, debtNs_ + double(cpuDelta) - double(wallDelta) * cpuPercent_ / 100.0);
    accounted_ = current;
    stats_.elapsedWallMs = double(current.wallNs - start_.wallNs) / nsPerMs;
    stats_.processCpuMs = double(current.cpuNs - start_.cpuNs) / nsPerMs;
}

void WorkBudget::refreshCpuPercent() {
    if (options_.dynamicCpuPercent) {
        const double next = options_.dynamicCpuPercent();
        if (!std::isfinite(next) || next < 1 || next > 100)
            throw std::runtime_error("Dynamic OCR CPU target must be between 1 and 100 percent");
        if (next != cpuPercent_) ++stats_.cpuPercentChanges;
        cpuPercent_ = next;
    }
    stats_.currentCpuPercent = cpuPercent_;
}

bool WorkBudget::stopped() const noexcept {
    return stats_.cancellationRequested || stats_.deadlineExceeded || stats_.monitorFailed;
}

bool WorkBudget::pollStop(Sample current) {
    if (stopped()) return true;
    if (cancel_ && cancel_()) stats_.cancellationRequested = true;
    if (current.wallNs - start_.wallNs >= std::int64_t(options_.maxWallMs) * nsPerMs)
        stats_.deadlineExceeded = true;
    return stopped();
}

bool WorkBudget::shouldCancel() noexcept {
    if (stopped()) return true;
    try {
        const auto current = readSample();
        account(current);
        refreshCpuPercent();
        if (!pollStop(current)) return false;
        stats_.maxCallbackWallGapMs = std::max(stats_.maxCallbackWallGapMs,
            double(current.wallNs - lastExit_.wallNs) / nsPerMs);
        stats_.maxCallbackCpuGapMs = std::max(stats_.maxCallbackCpuGapMs,
            double(current.cpuNs - lastExit_.cpuNs) / nsPerMs);
        return true;
    } catch (...) {
        stats_.monitorFailed = true;
        return true;
    }
}

bool WorkBudget::checkpoint() noexcept {
    if (stopped()) return false;
    try {
        ++stats_.checkpoints;
        auto current = readSample();
        stats_.maxCallbackWallGapMs = std::max(stats_.maxCallbackWallGapMs,
            double(current.wallNs - lastExit_.wallNs) / nsPerMs);
        stats_.maxCallbackCpuGapMs = std::max(stats_.maxCallbackCpuGapMs,
            double(current.cpuNs - lastExit_.cpuNs) / nsPerMs);
        account(current);
        refreshCpuPercent();
        if (pollStop(current)) return false;
        if (cpuPercent_ > 0 && debtNs_ > options_.burstCpuMs * nsPerMs) {
            // Pay debt completely after crossing the small burst allowance.
            // Sleep and preemption both repay it; idle time never banks credit.
            while (debtNs_ > 0) {
                const auto remaining = std::int64_t(options_.maxWallMs) * nsPerMs - (current.wallNs - start_.wallNs);
                const double needed = debtNs_ / (cpuPercent_ / 100.0);
                const auto sleepNs = std::int64_t(std::ceil(std::min({needed,
                    double(options_.maxSleepMs) * nsPerMs, double(remaining)})));
                if (sleepNs <= 0) {
                    stats_.deadlineExceeded = true;
                    return false;
                }
                const auto before = current.wallNs;
                sleep_(sleepNs);
                ++stats_.sleepCount;
                current = readSample();
                stats_.sleepMs += double(current.wallNs - before) / nsPerMs;
                account(current);
                refreshCpuPercent();
                if (pollStop(current)) return false;
            }
        }
        lastExit_ = current;
        return true;
    } catch (...) {
        stats_.monitorFailed = true;
        return false;
    }
}

const char* WorkBudget::stopReason() const noexcept {
    if (stats_.monitorFailed) return "monitor-failed";
    if (stats_.cancellationRequested) return "cancel-requested";
    if (stats_.deadlineExceeded) return "deadline-exceeded";
    return "none";
}

} // namespace replay
