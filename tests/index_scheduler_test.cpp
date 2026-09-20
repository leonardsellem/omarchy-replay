#include "index_scheduler.h"
#include <functional>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Scenario {
    qint64 now = 0;
    int reads = 0;
    bool fail = false;
    replay::ActivitySnapshot state;
    replay::IndexScheduler policy;

    explicit Scenario(replay::SchedulerOptions options = {})
        : policy(options, [this] {
            ++reads;
            if (fail) throw std::runtime_error("signals unavailable");
            return state;
        }, [this] { return now; }) {}

    void known() {
        state.idleKnown = state.pressureKnown = state.cpuBusyKnown = true;
        state.cpuBusyPercent = 20;
    }
    double at(qint64 time, bool requested = false) { now = time; return policy.cpuPercent(requested); }
};

void interactiveAndUnknownSignals() {
    Scenario s;
    require(s.at(0) == 10 && s.at(0, true) == 10 && s.reads == 1, "unknown signals allowed a boost or bypassed caching");
    s.known();
    require(s.at(100) == 10 && s.at(100, true) == 30, "active/request allowances are wrong");
    s.state.idle = true;
    require(s.at(200) == 40 && s.at(200, true) == 40, "request slowed idle work");
    s.state.idle = false;
    require(s.at(300) == 10, "resumed activity did not stop idle boost");
    s.state.idle = true;
    s.state.cpuBusyKnown = false;
    require(s.at(400, true) == 10, "unknown CPU utilization allowed a boost");
    s.known();
    s.state.idleKnown = false;
    require(s.at(500) == 10, "lost idle source retained idle boost");
    s.known();
    s.fail = true;
    require(s.at(600, true) == 10, "signal exception escaped or allowed a boost");
}

void quotaPressureWithHeadroomDoesNotBackOff() {
    Scenario s;
    s.known();
    s.state.idle = true;
    s.state.cpuPressurePercent = 99;
    s.state.cpuBusyPercent = 30;
    require(s.at(0) == 40 && s.at(10000, true) == 40, "high PSI with spare CPU blocked idle/request work");
    require(s.policy.statsJSON()["pressure_entries"].toInteger() == 0
            && s.policy.statsJSON()["pressure_reason"].toString() == "headroom", "headroom decision was not reported");
    s.state.cpuBusyPercent = 99;
    s.state.cpuPressurePercent = 0;
    require(s.at(11000, true) == 40 && s.at(20000) == 40, "utilization alone triggered pressure mode");
}

void sustainedSaturationAndRecovery() {
    replay::SchedulerOptions options;
    options.activeCpuPercent = 30;
    options.pressureCpuPercent = 10;
    Scenario s(options);
    s.known();
    s.state.idle = true;
    s.state.cpuBusyPercent = 90;
    s.state.cpuPressurePercent = 10;
    require(s.at(0, true) == 40 && s.at(2900, true) == 40, "pressure entered before persistence threshold");
    s.state.cpuBusyPercent = 80;
    require(s.at(3000) == 40, "a brief spike latched pressure");
    s.state.cpuBusyPercent = 90;
    require(s.at(3100) == 40 && s.at(6000) == 40, "entry did not restart after saturation ended");
    require(s.at(6100, true) == 10, "sustained contention did not override explicit boost");
    s.state.cpuBusyPercent = 60;
    require(s.at(6200) == 10 && s.at(11100) == 10, "pressure recovered without sustained headroom");
    require(s.at(11200) == 40, "lingering PSI blocked recovery despite CPU headroom");
    const auto stats = s.policy.statsJSON();
    require(stats["pressure_entries"].toInteger() == 1 && stats["pressure_recoveries"].toInteger() == 1,
            "pressure transitions were not counted");
    require(stats["policy_time_ms"].toObject()["pressure"].toInteger() == 5100,
            "pressure mode duration was not recorded");
    require(stats["cpu_headroom_percent"].toDouble() == 40, "CPU headroom diagnostic is wrong");

    s.state.cpuBusyPercent = 90;
    require(s.at(12000) == 40 && s.at(15000) == 10, "second sustained pressure event failed");
    s.state.cpuPressurePercent = 1;
    require(s.at(15100) == 10 && s.at(20100) == 40, "low PSI could not recover without low utilization");
}

void malformedSignalsAndRecoveryPersistence() {
    Scenario s;
    s.known(); s.state.idle = true;
    const std::vector<double> invalid{std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -1, 101};
    qint64 now = 0;
    for (double value : invalid) {
        s.state.cpuBusyPercent = value;
        require(s.at(now += 100, true) == 10, "invalid CPU utilization allowed a boost");
    }
    s.state.cpuBusyPercent = 90;
    s.state.cpuPressurePercent = 10;
    require(s.at(1000) == 40 && s.at(4000) == 10, "setup pressure did not latch");
    s.state.cpuBusyPercent = 20;
    require(s.at(4100) == 10, "headroom skipped recovery wait");
    s.state.cpuBusyKnown = false;
    require(s.at(8000) == 10, "unknown utilization did not use active allowance");
    s.state.cpuBusyKnown = true;
    require(s.at(8100) == 10 && s.at(13000) == 10 && s.at(13100) == 40,
            "unknown samples did not restart recovery persistence");
    s.state.cpuPressurePercent = std::numeric_limits<double>::quiet_NaN();
    require(s.at(13200, true) == 10, "invalid PSI allowed a boost");
}

void configurablePolicyAndValidation() {
    replay::SchedulerOptions options;
    options.activeCpuPercent = 25;
    options.pressureCpuPercent = 7;
    options.cpuBusyThresholdPercent = 75;
    options.cpuBusyRecoveryPercent = 50;
    options.pressureThresholdPercent = 8;
    options.pressureEntryMs = 1000;
    options.pressureRecoveryMs = 2000;
    Scenario s(options);
    s.known(); s.state.cpuBusyPercent = 75; s.state.cpuPressurePercent = 8;
    require(s.at(0) == 25 && s.at(1000) == 7, "configured entry thresholds or pressure allowance ignored");
    s.state.cpuBusyPercent = 50;
    require(s.at(1100) == 7 && s.at(3100) == 25, "configured headroom recovery threshold ignored");
    options.activeCpuPercent = 5;
    options.pressureEntryMs = 0;
    Scenario smaller(options);
    smaller.known(); smaller.state.cpuBusyPercent = 90; smaller.state.cpuPressurePercent = 10;
    require(smaller.at(0) == 5, "pressure allowance increased a smaller active budget");

    const std::vector<std::function<void(replay::SchedulerOptions &)>> invalid{
        [](auto &o) { o.pressureCpuPercent = 0; },
        [](auto &o) { o.cpuBusyThresholdPercent = 101; },
        [](auto &o) { o.cpuBusyThresholdPercent = std::numeric_limits<double>::quiet_NaN(); },
        [](auto &o) { o.cpuBusyRecoveryPercent = -1; },
        [](auto &o) { o.cpuBusyRecoveryPercent = o.cpuBusyThresholdPercent; },
        [](auto &o) { o.cpuBusyRecoveryPercent = std::numeric_limits<double>::infinity(); },
        [](auto &o) { o.pressureEntryMs = -1; },
        [](auto &o) { o.pressureRecoveryMs = -1; }
    };
    for (const auto &alter : invalid) {
        replay::SchedulerOptions bad;
        alter(bad);
        bool rejected = false;
        try { Scenario rejectedPolicy(bad); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "invalid scheduling option accepted");
    }
}
} // namespace

int main() {
    try {
        interactiveAndUnknownSignals();
        quotaPressureWithHeadroomDoesNotBackOff();
        sustainedSaturationAndRecovery();
        malformedSignalsAndRecoveryPersistence();
        configurablePolicyAndValidation();
        std::cout << "Adaptive scheduling, CPU headroom, sustained pressure and conservative fallbacks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
