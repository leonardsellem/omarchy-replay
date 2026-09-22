#pragma once

#include <QJsonObject>
#include <QString>

namespace replay {

struct StorageForecastOptions {
    qint64 nowMs = 0;
    qint64 sampleAfterMs = 0; // Ignore history recorded under earlier capture settings.
    double intervalSeconds = 5;
    qint64 diskBytes = 0;
    qint64 maxDiskBytes = 0;
    qint64 freeBytes = -1; // Unknown free space suppresses the capacity estimate.
    qint64 minFreeBytes = 0;
    int retentionDays = 30;
};

// Recalculate a selected rolling allowance from measured rates, without reading
// history. Used by Settings before changes are saved.
QJsonObject storageCapacityProjection(const QJsonObject &observed, const StorageForecastOptions &options);

// A bounded, read-only estimate from numeric history metadata. The caller supplies
// current usage and filesystem capacity, and calls on maintenance or on demand,
// never per capture. No retained telemetry, image reads, OCR or filesystem scan.
// Active hours are approximate sampled recording time, not calendar time. Long
// observation gaps contribute at most one expected capture interval; ordinary
// spacing contributes at most 1.25 intervals. Calendar estimates need at least
// seven retained days and describe observed use, without assuming hours per day.
QJsonObject storageForecast(const QString &historyDirectory, const StorageForecastOptions &options);

}
