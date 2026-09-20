#pragma once

#include <cstdint>
#include <functional>

namespace replay {

struct WorkBudgetOptions {
    double cpuPercent = 0; // Zero disables pacing, but not cancellation/deadline.
    int maxWallMs = 10000;
    int maxSleepMs = 5;    // Check cancellation between these short sleeps.
    double burstCpuMs = 2;
    std::function<double()> dynamicCpuPercent; // Optional live target, always 1..100.
};

struct WorkBudgetStats {
    std::uint64_t checkpoints = 0;
    std::uint64_t sleepCount = 0;
    double sleepMs = 0;
    double maxCallbackWallGapMs = 0;
    double maxCallbackCpuGapMs = 0;
    double elapsedWallMs = 0;
    double processCpuMs = 0;
    bool cancellationRequested = false;
    bool deadlineExceeded = false;
    bool monitorFailed = false;
    std::uint64_t cpuPercentChanges = 0;
    double currentCpuPercent = 0;
};

// Cooperative pacing for one bounded work unit on the calling thread. Create a
// fresh budget before each OCR unit; idle time cannot accumulate future credit.
// CLOCK_PROCESS_CPUTIME_ID counts this process, not encoder children/compositor.
// It is a target at checkpoints, not a scheduler-enforced CPU or latency limit.
// Work between callbacks (including Tesseract layout analysis) cannot be paced
// or interrupted here. Its largest wall/CPU gap is reported, including the gap
// before the first callback. Deliberate sleeps are excluded from gap statistics.
class WorkBudget {
public:
    struct Sample {
        std::int64_t wallNs;
        std::int64_t cpuNs;
    };
    using CancelCheck = std::function<bool()>;
    // Optional clock/sleeper injection makes budget/cancellation tests exact;
    // production callers leave these empty for Linux clocks and nanosleep.
    using SampleClock = std::function<Sample()>;
    using Sleeper = std::function<void(std::int64_t)>;

    explicit WorkBudget(WorkBudgetOptions options = {}, CancelCheck cancel = {},
                        SampleClock clock = {}, Sleeper sleep = {});
    bool checkpoint() noexcept;   // True to continue; may yield this thread.
    bool shouldCancel() noexcept; // Never sleeps; stop decisions remain latched.
    const WorkBudgetStats& stats() const noexcept { return stats_; }
    const char* stopReason() const noexcept;

private:
    Sample readSample();
    void account(Sample current);
    void refreshCpuPercent();
    bool pollStop(Sample current);
    bool stopped() const noexcept;

    WorkBudgetOptions options_;
    CancelCheck cancel_;
    SampleClock clock_;
    Sleeper sleep_;
    Sample start_{}, accounted_{}, lastExit_{};
    double debtNs_ = 0;
    double cpuPercent_ = 0;
    WorkBudgetStats stats_;
};

} // namespace replay
