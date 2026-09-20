#include "activity_signals.h"

#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr qint64 secondUs = 1000000;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void close(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < .000001, message);
}

QByteArray pressure(quint64 total, double avg10 = 7.5) {
    return "some avg10=" + QByteArray::number(avg10) + " avg60=2.00 avg300=1.00 total=" + QByteArray::number(total)
        + "\nfull avg10=99.00 avg60=99.00 avg300=99.00 total=999999999\n";
}

QByteArray cpuStat(quint64 user, quint64 idle, quint64 iowait = 0) {
    return "cpu " + QByteArray::number(user) + " 0 0 " + QByteArray::number(idle) + ' '
        + QByteArray::number(iowait) + " 0 0 0 0 0\n";
}

struct Scenario {
    qint64 now = 0;
    QByteArray text = pressure(0);
    QByteArray cpuText = cpuStat(0, 0);
    std::optional<bool> idle;
    int reads = 0, cpuReads = 0;
    bool failPressure = false, failIdle = false, failClock = false, failCpu = false;

    replay::ActivitySignals::Sources sources() {
        return {
            [this] {
                if (failClock) throw std::runtime_error("clock unavailable");
                return now;
            },
            [this] {
                ++reads;
                if (failPressure) throw std::runtime_error("PSI unavailable");
                return text;
            },
            [this] {
                if (failIdle) throw std::runtime_error("disconnected");
                return idle;
            },
            [this] {
                ++cpuReads;
                if (failCpu) throw std::runtime_error("CPU counters unavailable");
                return cpuText;
            }
        };
    }
};

void cpuBusyDeltasAndAccounting() {
    Scenario source;
    replay::ActivitySignals sampler(60, source.sources());
    require(!sampler.sample().cpuBusyKnown, "first CPU sample invented utilization without a delta");
    source.cpuText = "cpu 20 10 10 30 10 2 4 4 20 10\n";
    source.now = secondUs - 1;
    require(!sampler.sample().cpuBusyKnown && source.cpuReads == 1, "CPU read rate limit failed");
    source.now = secondUs;
    auto value = sampler.sample();
    require(value.cpuBusyKnown, "valid CPU delta was unknown");
    close(value.cpuBusyPercent, 100.0 * 50 / 90, "idle/iowait were busy or guest time was counted twice");
    source.now += secondUs;
    require(!sampler.sample().cpuBusyKnown, "unchanged counters invented CPU headroom");
    source.now += secondUs;
    source.cpuText = "cpu 40 20 20 60 20 4 8 8 40 20\n";
    source.failPressure = true;
    value = sampler.sample();
    require(value.cpuBusyKnown && !value.pressureKnown, "PSI failure hid valid independent CPU utilization");
    close(value.cpuBusyPercent, 100.0 * 50 / 90, "CPU recovery delta is wrong");
    const auto stats = sampler.statsJSON();
    require(stats["cpu_busy_reads"].toInteger() == 4 && stats["cpu_busy_baseline_samples"].toInteger() == 1
            && stats["cpu_busy_delta_samples"].toInteger() == 2, "CPU diagnostic accounting is wrong");
}

void invalidCpuCountersAreUnknownAndRecover() {
    Scenario source;
    replay::ActivitySignals sampler(60, source.sources());
    sampler.sample();
    const std::vector<QByteArray> invalid{
        {}, "cpu0 1 0 0 9 0 0 0 0\n", "cpu 1 2 3 4\n",
        "cpu -1 0 0 9 0 0 0 0\n", "cpu 1.5 0 0 9 0 0 0 0\n",
        "cpu 18446744073709551616 0 0 9 0 0 0 0\n",
        "cpu 18446744073709551615 1 0 0 0 0 0 0\n",
        "cpu 1 0 0 9 0 0 0 0 nan\n", QByteArray(4097, 'x')
    };
    for (const auto &text : invalid) {
        source.now += secondUs;
        source.cpuText = text;
        const auto value = sampler.sample();
        require(!value.cpuBusyKnown && value.cpuBusyPercent == 0 && value.pressureKnown,
                "invalid CPU counters became usable or hid independent PSI");
    }
    source.now += secondUs;
    source.cpuText = cpuStat(100, 100, 100);
    require(!sampler.sample().cpuBusyKnown, "CPU recovery used a stale baseline");
    source.now += secondUs;
    source.cpuText = cpuStat(200, 200, 99); // Iowait can decrease; do not unsigned-wrap.
    require(!sampler.sample().cpuBusyKnown, "decreasing CPU component became valid utilization");
    require(sampler.statsJSON()["cpu_busy_counter_resets"].toInteger() == 1, "CPU counter reset was not reported");
    source.now += secondUs;
    source.cpuText = cpuStat(300, 300, 99);
    close(sampler.sample().cpuBusyPercent, 50, "CPU reset did not establish a new delta baseline");
    source.now += secondUs;
    source.failCpu = true;
    require(!sampler.sample().cpuBusyKnown, "CPU source exception kept stale utilization");
    source.failCpu = false;
    source.now += secondUs;
    require(!sampler.sample().cpuBusyKnown, "CPU exception recovery reused stale counters");
    source.now += secondUs;
    source.cpuText = cpuStat(400, 300, 99);
    close(sampler.sample().cpuBusyPercent, 100, "fully busy CPU delta is wrong");
    source.now += secondUs;
    source.cpuText = cpuStat(400, 400, 199);
    const auto idle = sampler.sample();
    require(idle.cpuBusyKnown, "idle/iowait-only CPU delta was unavailable");
    close(idle.cpuBusyPercent, 0, "idle/iowait-only CPU delta is wrong");
}

void pressureRateAndRecentDeltas() {
    Scenario source;
    source.text = pressure(100, 2.5);
    replay::ActivitySignals sampler(60, source.sources());
    auto value = sampler.sample();
    require(value.pressureKnown && !value.idleKnown && !value.idle, "initial source availability is wrong");
    close(value.cpuPressurePercent, 2.5, "initial PSI sample did not use avg10 or used full pressure");

    source.now = secondUs - 1;
    source.text = pressure(250100, 99);
    value = sampler.sample();
    require(source.reads == 1, "PSI was read more often than once per second");
    close(value.cpuPressurePercent, 2.5, "cached PSI changed without a read");

    source.now = secondUs;
    value = sampler.sample();
    require(source.reads == 2, "PSI was not refreshed after one second");
    close(value.cpuPressurePercent, 25, "recent delta did not override avg10");
    source.now = 4 * secondUs;
    source.text = pressure(550100, 99);
    close(sampler.sample().cpuPressurePercent, 10, "delta assumed one second instead of actual elapsed time");
    source.now += secondUs;
    close(sampler.sample().cpuPressurePercent, 0, "unchanged total did not clear old pressure");

    const auto stats = sampler.statsJSON();
    require(stats["pressure_reads"].toInt() == 4 && stats["pressure_delta_samples"].toInt() == 3
            && stats["pressure_avg10_samples"].toInt() == 1, "pressure diagnostic accounting is wrong");
}

void malformedPressureIsUnknownAndRecovers() {
    Scenario source;
    replay::ActivitySignals sampler(60, source.sources());
    require(sampler.sample().pressureKnown, "valid PSI was rejected");
    const std::vector<QByteArray> invalid{
        {}, "full avg10=1 avg60=1 avg300=1 total=1\n",
        "some avg10=nan avg60=0 avg300=0 total=1\n",
        "some avg10=inf avg60=0 avg300=0 total=1\n",
        "some avg10=101 avg60=0 avg300=0 total=1\n",
        "some avg10=-1 avg60=0 avg300=0 total=1\n",
        "some avg10=0 avg60=nan avg300=0 total=1\n",
        "some avg10=0 avg60=0 avg300=0 total=-1\n",
        "some avg10=0 avg60=0 avg300=0 total=18446744073709551616\n",
        "some avg10=0 avg60=0 total=1\n",
        "some avg10=0 avg10=1 avg300=0 total=1\n",
        pressure(1) + pressure(2), QByteArray(4097, 'x')
    };
    for (const auto& input : invalid) {
        source.now += secondUs;
        source.text = input;
        const auto value = sampler.sample();
        require(!value.pressureKnown && value.cpuPressurePercent == 0, "malformed PSI became a usable pressure reading");
    }
    source.now += secondUs;
    source.text = pressure(9000000, 3.25);
    close(sampler.sample().cpuPressurePercent, 3.25, "recovery derived a delta across missing readings");

    source.now += secondUs;
    source.text = pressure(1, 4.5); // Reboot/counter reset must not unsigned-wrap.
    close(sampler.sample().cpuPressurePercent, 4.5, "counter reset did not fall back to avg10");
    source.now += secondUs;
    source.text = "some total=18446744073709551615 avg300=0 avg10=2 avg60=0\n";
    const auto maximum = sampler.sample();
    require(maximum.pressureKnown && maximum.cpuPressurePercent == 100, "valid uint64 total or bounded pressure failed");
}

void idleLossIsConservativeAndIndependentOfPressure() {
    Scenario source;
    source.idle = false;
    replay::ActivitySignals sampler(60, source.sources());
    auto value = sampler.sample();
    require(value.idleKnown && !value.idle, "active seat was not known active");
    source.idle = true;
    value = sampler.sample();
    require(value.idleKnown && value.idle && source.reads == 1, "idle event was delayed by PSI rate limiting");
    source.idle.reset(); // Protocol removal/disconnection.
    value = sampler.sample();
    require(!value.idleKnown && !value.idle && value.pressureKnown, "lost idle source retained idle state or hid PSI");
    source.idle = true;
    require(sampler.sample().idle, "re-established injected idle state was ignored");
    source.failIdle = true;
    value = sampler.sample();
    require(!value.idleKnown && !value.idle, "idle source exception escaped or retained idle state");

    source.failIdle = false;
    source.failPressure = true;
    source.now += secondUs;
    value = sampler.sample();
    require(value.idleKnown && value.idle && !value.pressureKnown, "PSI source exception hid valid idle state");
}

void invalidClockAndMissingSources() {
    Scenario source;
    source.now = 10 * secondUs;
    replay::ActivitySignals sampler(60, source.sources());
    require(sampler.sample().pressureKnown, "initial clock failed");
    source.now -= 1;
    auto value = sampler.sample();
    require(!value.pressureKnown && !value.cpuBusyKnown, "backwards clock kept stale usable resource values");
    source.failClock = true;
    require(!sampler.sample().pressureKnown, "clock exception escaped or made pressure known");
    source.failClock = false;
    source.now = 12 * secondUs;
    source.text = pressure(100000000, 1.25);
    close(sampler.sample().cpuPressurePercent, 1.25, "clock recovery reused a stale delta baseline");

    replay::ActivitySignals absent(60, {});
    const auto unknown = absent.sample();
    require(!unknown.idleKnown && !unknown.idle && !unknown.pressureKnown && !unknown.cpuBusyKnown, "missing sources were not conservative");
    bool rejected = false;
    try { replay::ActivitySignals invalid(0, {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "zero idle threshold was accepted");
}

void unavailableNativeDisplay() {
    QTemporaryDir directory;
    require(directory.isValid(), "could not create isolated missing-display test path");
    struct Restore {
        QByteArray previous = qgetenv("WAYLAND_DISPLAY");
        bool present = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
        ~Restore() { if (present) qputenv("WAYLAND_DISPLAY", previous); else qunsetenv("WAYLAND_DISPLAY"); }
    } restore;
    qputenv("WAYLAND_DISPLAY", (directory.path() + "/no-compositor").toUtf8());
    replay::ActivitySignals sampler;
    const auto value = sampler.sample();
    require(!value.idleKnown && !value.idle, "missing compositor was treated as idle");
    require(sampler.statsJSON()["idle_source"].toString() == "unavailable", "missing compositor diagnostic is wrong");
}
} // namespace

int main() {
    try {
        pressureRateAndRecentDeltas();
        cpuBusyDeltasAndAccounting();
        invalidCpuCountersAreUnknownAndRecover();
        malformedPressureIsUnknownAndRecovers();
        idleLossIsConservativeAndIndependentOfPressure();
        invalidClockAndMissingSources();
        unavailableNativeDisplay();
        std::cout << "PASS activity signal availability, idle transitions, PSI validation, recent deltas and read rate limit\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
