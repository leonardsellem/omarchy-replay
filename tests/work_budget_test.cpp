#include "work_budget.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::int64_t ms = 1000000;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void close(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < .00001, message);
}

struct Clock {
    replay::WorkBudget::Sample sample{100 * ms, 20 * ms};
    std::vector<std::int64_t> sleeps;
    bool cancel = false;
    bool cancelOnSleep = false;
    auto now() { return [this] { return sample; }; }
    auto sleeper() {
        return [this](std::int64_t ns) {
            sleeps.push_back(ns);
            sample.wallNs += ns;
            if (cancelOnSleep) cancel = true;
        };
    }
    void advance(std::int64_t wallMs, std::int64_t cpuMs) {
        sample.wallNs += wallMs * ms;
        sample.cpuNs += cpuMs * ms;
    }
};

void dutyAndGapAccounting() {
    Clock clock;
    replay::WorkBudget budget({25, 1000, 5, 0}, {}, clock.now(), clock.sleeper());
    clock.advance(20, 20);
    require(budget.checkpoint(), "normal work was cancelled");
    close(budget.stats().elapsedWallMs, 80, "25% duty did not yield 60ms for 20ms CPU");
    close(budget.stats().sleepMs, 60, "sleep accounting is wrong");
    close(budget.stats().maxCallbackCpuGapMs, 20, "CPU callback gap was hidden");
    close(budget.stats().maxCallbackWallGapMs, 20, "wall callback gap included budget sleeps");
    require(clock.sleeps.size() == 12, "sleep slices were not bounded to 5ms");
    for (auto value : clock.sleeps) require(value <= 5 * ms && value > 0, "invalid sleep slice");
    clock.advance(2, 2);
    require(budget.checkpoint(), "second checkpoint failed");
    close(budget.stats().sleepMs, 66, "second CPU unit was not independently paced");
    close(budget.stats().maxCallbackWallGapMs, 20, "deliberate sleep increased callback gap");
}

void noBankedIdleCreditAndPreemption() {
    Clock clock;
    replay::WorkBudget budget({25, 1000, 5, 0}, {}, clock.now(), clock.sleeper());
    clock.advance(500, 0);
    require(budget.checkpoint(), "idle checkpoint failed");
    clock.advance(10, 10);
    require(budget.checkpoint(), "work after idle failed");
    close(budget.stats().sleepMs, 30, "idle time banked an unlimited CPU burst");
    clock.advance(100, 10); // Descheduled enough already; additional sleep is unnecessary.
    require(budget.checkpoint(), "preempted work failed");
    close(budget.stats().sleepMs, 30, "preemption was not counted toward wall time");
}

void burstAndDisabledPacing() {
    Clock clock;
    replay::WorkBudget budget({50, 1000, 5, 2}, {}, clock.now(), clock.sleeper());
    clock.advance(4, 4);
    require(budget.checkpoint() && clock.sleeps.empty(), "permitted small burst slept");
    clock.advance(2, 2);
    require(budget.checkpoint(), "burst debt repayment failed");
    close(budget.stats().sleepMs, 6, "burst debt was not fully repaid");

    Clock off;
    replay::WorkBudget disabled({0, 1000, 5, 0}, {}, off.now(), off.sleeper());
    off.advance(50, 50);
    require(disabled.checkpoint() && off.sleeps.empty(), "disabled budget slept");
    close(disabled.stats().maxCallbackCpuGapMs, 50, "disabled baseline lost callback metrics");
}

void cancellationAndDeadline() {
    Clock clock;
    clock.cancelOnSleep = true;
    replay::WorkBudget budget({10, 1000, 5, 0}, [&] { return clock.cancel; }, clock.now(), clock.sleeper());
    clock.advance(20, 20);
    require(!budget.checkpoint(), "cancel request during yield was ignored");
    require(clock.sleeps.size() == 1 && budget.stats().cancellationRequested, "cancel did not stop after one slice");
    clock.cancel = false;
    require(budget.shouldCancel() && !budget.checkpoint(), "cancellation was not latched");

    Clock deadlineClock;
    replay::WorkBudget deadline({10, 25, 5, 0}, {}, deadlineClock.now(), deadlineClock.sleeper());
    deadlineClock.advance(20, 20);
    require(!deadline.checkpoint() && deadline.stats().deadlineExceeded, "wall deadline did not include sleeps");
    close(deadline.stats().elapsedWallMs, 25, "yield slept past its own deadline");

    Clock noCallbacks;
    replay::WorkBudget delayed({20, 25, 5, 0}, {}, noCallbacks.now(), noCallbacks.sleeper());
    noCallbacks.advance(40, 40);
    require(!delayed.checkpoint() && delayed.stats().deadlineExceeded, "late callback falsely completed");
    close(delayed.stats().maxCallbackCpuGapMs, 40, "uninterruptible callback gap was hidden");
    require(noCallbacks.sleeps.empty(), "expired work slept further");

    Clock cancelOnly;
    replay::WorkBudget cancelMonitor({0, 25, 5, 0}, {}, cancelOnly.now(), cancelOnly.sleeper());
    cancelOnly.advance(35, 30);
    require(cancelMonitor.shouldCancel() && cancelMonitor.stats().deadlineExceeded,
            "cancel-only monitor missed the deadline");
    close(cancelMonitor.stats().maxCallbackCpuGapMs, 30, "cancel-only callback hid its CPU gap");
    close(cancelMonitor.stats().elapsedWallMs, 35, "cancel-only stop lost elapsed time");
}

void failuresStayInsideCallback() {
    Clock clock;
    replay::WorkBudget failing({20, 1000, 5, 0}, []() -> bool { throw std::runtime_error("stop hook failed"); }, clock.now(), clock.sleeper());
    require(!failing.checkpoint() && failing.stats().monitorFailed, "hook exception escaped callback");
    Clock backwards;
    replay::WorkBudget invalidClock({}, {}, backwards.now(), backwards.sleeper());
    backwards.sample.cpuNs -= 1;
    require(!invalidClock.checkpoint() && invalidClock.stats().monitorFailed, "backwards CPU clock was accepted");
    bool rejected = false;
    try { replay::WorkBudget invalid({std::numeric_limits<double>::quiet_NaN()}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "NaN duty was accepted");
}

void dynamicTargetsPreserveDebt() {
    Clock clock;
    double target = 10;
    replay::WorkBudgetOptions options{10, 1000, 5, 0};
    options.dynamicCpuPercent = [&] { return target; };
    replay::WorkBudget budget(options, {}, clock.now(), clock.sleeper());
    clock.advance(20, 20);
    target = 100;
    require(budget.checkpoint(), "live target increase failed");
    // The first 20ms accrued 18ms of debt under 10%, before the new target.
    close(budget.stats().sleepMs, 18, "a target increase erased past CPU debt");
    target = 25;
    clock.advance(20, 20); // This interval still belongs to the old 100% target.
    require(budget.checkpoint(), "live target decrease failed");
    close(budget.stats().sleepMs, 18, "a target decrease retroactively charged old work");
    clock.advance(4, 4);
    require(budget.checkpoint(), "work at decreased target failed");
    close(budget.stats().sleepMs, 30, "decreased target did not pace subsequent work");
    require(budget.stats().cpuPercentChanges == 2 && budget.stats().currentCpuPercent == 25,
            "live target changes were not observable");

    Clock sleeping;
    target = 10;
    replay::WorkBudget duringSleep(options, {}, sleeping.now(), [&](std::int64_t ns) {
        sleeping.sample.wallNs += ns;
        sleeping.sleeps.push_back(ns);
        target = 100;
    });
    sleeping.advance(20, 20);
    require(duringSleep.checkpoint(), "target update during yield failed");
    close(duringSleep.stats().sleepMs, 22.5, "yield did not apply a live target after its first sleep slice");
    target = std::numeric_limits<double>::quiet_NaN();
    require(!duringSleep.checkpoint() && duringSleep.stats().monitorFailed,
            "invalid provider value escaped or disabled pacing");
}
} // namespace

int main() {
    try {
        dutyAndGapAccounting();
        noBankedIdleCreditAndPreemption();
        burstAndDisabledPacing();
        cancellationAndDeadline();
        failuresStayInsideCallback();
        dynamicTargetsPreserveDebt();
        std::cout << "PASS cooperative CPU duty, bounded yield, no idle credit, cancellation, deadlines and callback-gap accounting\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
