#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <optional>

namespace replay {

struct ActivitySnapshot {
    bool idleKnown = false;
    bool idle = false;
    bool pressureKnown = false;
    double cpuPressurePercent = 0;
    bool cpuBusyKnown = false;
    double cpuBusyPercent = 0;
};

// Single-threaded sampler. Idle respects compositor inhibitors and starts its
// timeout at subscription, not at the user's last activity before subscription.
// CPU pressure is system-wide PSI "some" stall time, not CPU utilization.
// CPU utilization uses aggregate /proc/stat deltas, excluding idle and iowait.
class ActivitySignals {
public:
    explicit ActivitySignals(int idleSeconds = 60);
    ~ActivitySignals();
    ActivitySignals(const ActivitySignals&) = delete;
    ActivitySignals& operator=(const ActivitySignals&) = delete;

    // Deterministic sources bypass all native I/O. Missing/throwing sources are
    // unknown. The clock is monotonic microseconds; a null idle value means the
    // source is unavailable or disconnected, never that the user is idle.
    struct Sources {
        std::function<qint64()> monotonicMicroseconds;
        std::function<QByteArray()> cpuPressure;
        std::function<std::optional<bool>()> idle;
        std::function<QByteArray()> cpuStat;
    };
    ActivitySignals(int idleSeconds, Sources sources);

    ActivitySnapshot sample();
    QJsonObject statsJSON() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace replay
