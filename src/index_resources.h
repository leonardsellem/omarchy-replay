#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>

namespace replay {

// Startup verification of the independent Replay worker's transient service.
// This class never moves a process or changes resource settings.
class IndexResources {
public:
    // 0 disables; otherwise percent of one CPU in 1..100. Empty unit reports
    // unavailable with its reason, for the caller's cooperative fallback.
    IndexResources(double cpuCeilingPercent, const QString &serviceUnit,
                   const QString &unavailableReason);
    // Read-only, injectable verification inputs; missing required files or
    // properties must not be treated as evidence of enforcement.
    struct Sources {
        qint64 pid = 0;
        std::function<std::optional<QByteArray>(const QString &)> readFile;
        std::function<QVariant(const QString &)> serviceProperty;
    };
    IndexResources(double cpuCeilingPercent, const QString &serviceUnit,
                   const QString &unavailableReason, Sources sources);

    // Startup verification snapshot. effective_cpu_percent is a ceiling shared
    // with siblings at constrained ancestors, never a promised allocation.
    QJsonObject statsJSON() const;

private:
    QJsonObject stats_;
};

quint64 processStartTicks(qint64 pid);

struct ManagedIndexResult {
    bool launched = false;
    int exitCode = 1;
    QByteArray output, errors;
    QString unavailableReason;
};
// Blocks like the original index command, carrying private per-launch pipes.
// A failed ambiguous start is cleaned up before a caller may use fallback.
ManagedIndexResult runManagedIndex(const QStringList &arguments, double ceiling,
                                  const std::function<bool()> &stopRequested);

// Watches the controller pidfd independently of the synchronous OCR loop.
// Owner death asks for graceful interruption, then bounds a stalled shutdown.
class IndexOwnerGuard {
public:
    IndexOwnerGuard(qint64 ownerPid, quint64 ownerStartTicks);
    ~IndexOwnerGuard();
    IndexOwnerGuard(const IndexOwnerGuard &) = delete;
    IndexOwnerGuard &operator=(const IndexOwnerGuard &) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace replay
