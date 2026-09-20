#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>

namespace replay {

// Saved service settings take precedence over trial settings. Empty means that
// no known policy is available; callers must not invent a model or CPU budget.
QJsonObject savedIndexPolicy(const QString &directory);

// Read-only. configured/enabled/paused describe durable intent; running is the
// supervisor lease, worker_running includes other indexers, external_worker
// identifies that distinction. state is active/idle/requested/pressure/unknown/
// waiting/paused/stopping/stopped/error. policy is the saved configuration; current
// effective_cpu_percent may be null. heartbeat_age_ms and policy_age_ms are
// independent freshness signals (-1 when unavailable). progress contains only
// numeric pending/ready/failed/disabled counts, and oldest_pending_age_ms is
// measured now. error/recovery/log_tail contain bounded operational messages.
QJsonObject indexServiceStatus(const QString &directory);

// start preserves a saved pause; resume explicitly unpauses. pause and stop do
// not terminate another caller's indexer. Settings are written only on actions.
// Internal ensure initializes/starts only when no explicit Stop is saved; it
// preserves saved service settings and checks intent under the control lease.
// Invoke from a background thread, since starting the supervisor is bounded but
// may wait briefly for process launch. No recording or login service is started.
QJsonObject controlIndexService(const QString &directory, const QString &action,
                               const QJsonObject &policy = {});

// Internal CLI supervisor and bounded transient worker telemetry.
int runIndexService(const QString &directory, const std::function<bool()> &stopRequested);
void publishIndexWorkerPolicy(const QString &directory, const QJsonObject &policy);
// Current worker receipt bound to one controller PID/start time. Empty when the
// worker changed, exited, or has not published yet; never used for signaling.
QJsonObject ownedIndexWorkerPolicy(const QString &directory, qint64 controllerPid);
bool indexServiceEnabled(const QString &directory);

} // namespace replay
