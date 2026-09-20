#pragma once

#include "activity_signals.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <functional>

namespace replay {

struct SchedulerOptions {
    double activeCpuPercent = 10;
    double idleCpuPercent = 40;
    double requestCpuPercent = 30;
    double pressureThresholdPercent = 5;
    int pressureRecoveryMs = 5000;
    int sampleIntervalMs = 100;
    double pressureCpuPercent = 10;
    double cpuBusyThresholdPercent = 85;
    double cpuBusyRecoveryPercent = 70;
    int pressureEntryMs = 3000;
};

// Scheduling changes when work is done, not its total CPU cost. This supplies a
// cooperative allowance; Tesseract can only react at its next checkpoint.
class IndexScheduler {
public:
    using Signals = std::function<ActivitySnapshot()>;
    using Clock = std::function<qint64()>;
    IndexScheduler(SchedulerOptions options, Signals signalSource, Clock clock = {});
    double cpuPercent(bool requested);
    QJsonObject statsJSON() const;

private:
    SchedulerOptions options_;
    Signals signals_;
    Clock clock_;
    QElapsedTimer elapsed_;
    ActivitySnapshot snapshot_;
    qint64 lastSampleMs_ = -1, lastTransitionMs_ = 0, recoverySinceMs_ = -1, pressureSinceMs_ = -1;
    bool pressure_ = false;
    QString mode_ = "unknown";
    QJsonObject modeMs_;
    qint64 samples_ = 0, transitions_ = 0;
    qint64 pressureEntries_ = 0, pressureRecoveries_ = 0, saturationSamples_ = 0, headroomSamples_ = 0, unknownSamples_ = 0;
    QString pressureReason_ = "signals-unavailable";
    double effectiveCpuPercent_ = 10;
};

} // namespace replay
