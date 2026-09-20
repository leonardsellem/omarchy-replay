#include "index_scheduler.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace replay {

IndexScheduler::IndexScheduler(SchedulerOptions options, Signals signalSource, Clock clock)
    : options_(options), signals_(std::move(signalSource)), clock_(std::move(clock)) {
    for (const double allowance : {options.activeCpuPercent, options.idleCpuPercent, options.requestCpuPercent, options.pressureCpuPercent})
        if (!std::isfinite(allowance) || allowance < 1 || allowance > 100)
            throw std::invalid_argument("Adaptive CPU allowances must be between 1 and 100 percent");
    if (!signals_ || !std::isfinite(options.pressureThresholdPercent) ||
        options.pressureThresholdPercent <= 0 || options.pressureThresholdPercent > 100 ||
        !std::isfinite(options.cpuBusyThresholdPercent) || options.cpuBusyThresholdPercent <= 0 || options.cpuBusyThresholdPercent > 100 ||
        !std::isfinite(options.cpuBusyRecoveryPercent) || options.cpuBusyRecoveryPercent < 0 || options.cpuBusyRecoveryPercent >= options.cpuBusyThresholdPercent ||
        options.pressureEntryMs < 0 || options.pressureRecoveryMs < 0 || options.sampleIntervalMs < 10)
        throw std::invalid_argument("Invalid adaptive scheduling policy");
    elapsed_.start();
    if (!clock_) clock_ = [this] { return elapsed_.elapsed(); };
    lastTransitionMs_ = clock_();
    effectiveCpuPercent_ = options.activeCpuPercent;
}

double IndexScheduler::cpuPercent(bool requested) {
    const qint64 now = clock_();
    if (lastSampleMs_ < 0 || now - lastSampleMs_ >= options_.sampleIntervalMs) {
        try { snapshot_ = signals_(); }
        catch (...) { snapshot_ = {}; }
        if (!std::isfinite(snapshot_.cpuPressurePercent) || snapshot_.cpuPressurePercent < 0 ||
            snapshot_.cpuPressurePercent > 100) snapshot_.pressureKnown = false;
        if (!std::isfinite(snapshot_.cpuBusyPercent) || snapshot_.cpuBusyPercent < 0 ||
            snapshot_.cpuBusyPercent > 100) snapshot_.cpuBusyKnown = false;
        lastSampleMs_ = now;
        ++samples_;
        if (!snapshot_.pressureKnown || !snapshot_.cpuBusyKnown) {
            recoverySinceMs_ = -1;
            pressureSinceMs_ = -1;
            pressureReason_ = "signals-unavailable";
            ++unknownSamples_;
        // Quota-throttled background tasks can raise PSI despite spare CPUs.
        // Require sustained utilization as well; this is not a UI latency probe.
        } else if (snapshot_.cpuPressurePercent >= options_.pressureThresholdPercent &&
                   snapshot_.cpuBusyPercent >= options_.cpuBusyThresholdPercent) {
            ++saturationSamples_;
            recoverySinceMs_ = -1;
            if (pressureSinceMs_ < 0) pressureSinceMs_ = now;
            if (!pressure_ && now - pressureSinceMs_ >= options_.pressureEntryMs) {
                pressure_ = true;
                ++pressureEntries_;
            }
            pressureReason_ = pressure_ ? "cpu-saturated" : "saturation-pending";
        } else {
            pressureSinceMs_ = -1;
            const bool headroom = snapshot_.cpuBusyPercent <= options_.cpuBusyRecoveryPercent;
            const bool lowPressure = snapshot_.cpuPressurePercent <= options_.pressureThresholdPercent / 2;
            if (headroom) ++headroomSamples_;
            pressureReason_ = headroom ? "headroom" : "no-sustained-contention";
            // Headroom can clear the latch even while quota-related PSI remains.
            if (pressure_ && (headroom || lowPressure)) {
                if (recoverySinceMs_ < 0) recoverySinceMs_ = now;
                pressureReason_ = headroom ? "recovering-headroom" : "recovering-pressure";
                if (now - recoverySinceMs_ >= options_.pressureRecoveryMs) {
                    pressure_ = false;
                    ++pressureRecoveries_;
                    recoverySinceMs_ = -1;
                    pressureReason_ = headroom ? "headroom" : "no-sustained-contention";
                }
            } else {
                recoverySinceMs_ = -1;
                if (pressure_) pressureReason_ = "pressure-hysteresis";
            }
        }
    }
    QString mode;
    double allowance = options_.activeCpuPercent;
    if (!snapshot_.idleKnown || !snapshot_.pressureKnown || !snapshot_.cpuBusyKnown) mode = "unknown";
    else if (pressure_) { mode = "pressure"; allowance = std::min(options_.activeCpuPercent, options_.pressureCpuPercent); }
    else if (requested) {
        mode = "requested";
        allowance = std::max(options_.requestCpuPercent,
            snapshot_.idle ? options_.idleCpuPercent : options_.activeCpuPercent);
    }
    else if (snapshot_.idle) { mode = "idle"; allowance = options_.idleCpuPercent; }
    else mode = "active";
    if (mode != mode_) {
        modeMs_[mode_] = modeMs_[mode_].toInteger() + std::max<qint64>(0, now - lastTransitionMs_);
        lastTransitionMs_ = now;
        mode_ = mode;
        ++transitions_;
    }
    effectiveCpuPercent_ = allowance;
    return allowance;
}

QJsonObject IndexScheduler::statsJSON() const {
    QJsonObject durations = modeMs_;
    durations[mode_] = durations[mode_].toInteger() + std::max<qint64>(0, clock_() - lastTransitionMs_);
    return {{"mode", mode_}, {"effective_cpu_percent", effectiveCpuPercent_},
        {"active_cpu_percent", options_.activeCpuPercent}, {"idle_cpu_percent", options_.idleCpuPercent},
        {"request_cpu_percent", options_.requestCpuPercent},
        {"pressure_cpu_percent", options_.pressureCpuPercent},
        {"pressure_threshold_percent", options_.pressureThresholdPercent},
        {"cpu_busy_threshold_percent", options_.cpuBusyThresholdPercent},
        {"cpu_busy_recovery_percent", options_.cpuBusyRecoveryPercent},
        {"pressure_entry_ms", options_.pressureEntryMs}, {"pressure_recovery_ms", options_.pressureRecoveryMs},
        {"idle_known", snapshot_.idleKnown}, {"idle", snapshot_.idle},
        {"pressure_known", snapshot_.pressureKnown}, {"cpu_pressure_percent", snapshot_.cpuPressurePercent},
        {"cpu_busy_known", snapshot_.cpuBusyKnown}, {"cpu_busy_percent", snapshot_.cpuBusyPercent},
        {"cpu_headroom_percent", snapshot_.cpuBusyKnown ? QJsonValue(100 - snapshot_.cpuBusyPercent) : QJsonValue()},
        {"pressure_reason", pressureReason_}, {"pressure_entries", pressureEntries_}, {"pressure_recoveries", pressureRecoveries_},
        {"saturation_samples", saturationSamples_}, {"headroom_samples", headroomSamples_}, {"unknown_samples", unknownSamples_},
        {"samples", samples_}, {"transitions", transitions_}, {"policy_time_ms", durations},
        {"scope", "Cooperative OCR allowance, not whole-process CPU or foreground responsiveness"}};
}

} // namespace replay
