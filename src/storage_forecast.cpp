#include "storage_forecast.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonValue>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>
#include <sqlite3.h>

namespace replay {
namespace {
constexpr int MaxObservations = 2000;
constexpr qint64 SampleWindowMs = 24 * 60 * 60 * 1000;
constexpr double MinimumActiveSeconds = 5 * 60;
constexpr int MinimumObservations = 12;

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

struct Observation {
    qint64 timestampMs, frameId, frameTimestampMs, bytes;
};

QJsonObject unavailable(QJsonObject result, const char *reason) {
    result["state"] = "unavailable";
    result["reason"] = reason;
    return result;
}

}

QJsonObject storageCapacityProjection(const QJsonObject &observed, const StorageForecastOptions &options) {
    QJsonObject result = observed;
    result["warning"] = "none";
    result["effective_capacity_bytes"] = QJsonValue::Null;
    result["capacity_active_hours"] = QJsonValue::Null;
    result["capacity_calendar_days"] = QJsonValue::Null;
    result["estimated_retention_bytes"] = QJsonValue::Null;
    result["retention_days"] = options.retentionDays;
    result["disk_bytes"] = options.diskBytes;
    result["filesystem_free_bytes"] = options.freeBytes < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(options.freeBytes);
    result["limiting_factor"] = "unknown";
    if (options.diskBytes < 0 || options.maxDiskBytes <= 0 || options.minFreeBytes < 0 || options.retentionDays < 1)
        return unavailable(result, "invalid-capacity-input");
    if (options.freeBytes < 0) return unavailable(result, "free-space-unknown");
    const qint64 freeHeadroom = std::max<qint64>(0, options.freeBytes - options.minFreeBytes);
    // Current Replay storage is reusable as the window rolls forward. Do not
    // interpret a full allowance as zero remaining recording capacity.
    const qint64 physicalCapacity = options.freeBytes < options.minFreeBytes
        ? std::max<qint64>(0, options.diskBytes - (options.minFreeBytes - options.freeBytes))
        : freeHeadroom > std::numeric_limits<qint64>::max() - options.diskBytes
            ? std::numeric_limits<qint64>::max() : options.diskBytes + freeHeadroom;
    const qint64 capacity = std::min(options.maxDiskBytes, physicalCapacity);
    result["effective_capacity_bytes"] = capacity;
    result["free_space_remaining_bytes"] = freeHeadroom;
    result["limiting_factor"] = physicalCapacity < options.maxDiskBytes ? "free-space" : "allowance";
    if (physicalCapacity < options.maxDiskBytes) result["warning"] = "free-space";
    const double hourly = result.value("bytes_per_active_hour").toDouble();
    if (hourly > 0 && std::isfinite(hourly)) result["capacity_active_hours"] = double(capacity) / hourly;
    const double daily = result.value("bytes_per_calendar_day").toDouble();
    if (daily > 0 && std::isfinite(daily)) {
        result["capacity_calendar_days"] = double(capacity) / daily;
        result["estimated_retention_bytes"] = daily * options.retentionDays;
    }
    return result;
}

QJsonObject storageForecast(const QString &historyDirectory, const StorageForecastOptions &options) {
    QJsonObject result{
        {"state", "insufficient-data"}, {"reason", "more-recording-needed"}, {"warning", "none"},
        {"bytes_per_active_hour", QJsonValue::Null}, {"bytes_per_calendar_day", QJsonValue::Null},
        {"calendar_sample_days", 0}, {"minimum_calendar_sample_days", 7},
        {"sample_after_ms", options.sampleAfterMs}, {"calendar_estimate_reason", "more-retained-days-needed"},
        {"sample_observations", 0}, {"sample_active_seconds", 0}, {"sampled_bytes", 0},
        {"sample_start_ms", QJsonValue::Null}, {"sample_end_ms", QJsonValue::Null},
        {"limited_sample", false}, {"sample_window_seconds", SampleWindowMs / 1000},
        {"max_sample_observations", MaxObservations}, {"minimum_sample_active_seconds", MinimumActiveSeconds},
        {"estimate_note", "Approximate rolling history capacity. Active hours use recent retained image growth; "
                          "calendar days use the current archive size over at least seven retained days. "
                          "Workload changes can change either estimate."}
    };
    result = storageCapacityProjection(result, options);
    if (result["state"] == "unavailable") return result;
    if (options.nowMs < 0 || options.sampleAfterMs < 0 || !std::isfinite(options.intervalSeconds) || options.intervalSeconds <= 0)
        return unavailable(result, "invalid-capacity-input");

    const QString path = QDir(historyDirectory).filePath("index.sqlite");
    if (!QFileInfo(path).isFile()) return unavailable(result, "history-unavailable");
    sqlite3 *handle = nullptr;
    const int opened = sqlite3_open_v2(path.toUtf8().constData(), &handle, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    Database database(handle, sqlite3_close);
    if (opened != SQLITE_OK) return unavailable(result, "history-unavailable");
    sqlite3_busy_timeout(handle, 25);
    // A corrupt or incompatible query plan must not become a background scan.
    int progressCalls = 0;
    sqlite3_progress_handler(handle, 1000, [](void *count) -> int { return ++*static_cast<int *>(count) > 1000; }, &progressCalls);
    if (sqlite3_exec(handle, "PRAGMA query_only=ON; PRAGMA cache_size=-256; BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        return unavailable(result, "history-busy");

    // Materialize the indexed, bounded sample before joining any media metadata.
    // The extra row detects truncation, including an old repeated frame whose
    // original bytes must not be attributed to the sampled time window again.
    constexpr const char *sql =
        "WITH recent AS MATERIALIZED ("
        " SELECT timestamp_ms,frame_id,id FROM observations INDEXED BY observation_time"
        " WHERE timestamp_ms>=? AND timestamp_ms<=? ORDER BY timestamp_ms DESC,id DESC LIMIT ?)"
        " SELECT r.timestamp_ms,r.frame_id,f.timestamp_ms,m.bytes FROM recent r"
        " LEFT JOIN frames f ON f.id=r.frame_id"
        " LEFT JOIN history_media m ON m.path=f.path AND m.state='live'"
        " ORDER BY r.timestamp_ms,r.id";
    sqlite3_stmt *rawStatement = nullptr;
    const int prepared = sqlite3_prepare_v2(handle, sql, -1, &rawStatement, nullptr);
    Statement statement(rawStatement, sqlite3_finalize);
    if (prepared != SQLITE_OK) return unavailable(result, "history-metadata-unavailable");
    sqlite3_bind_int64(rawStatement, 1, std::max(options.sampleAfterMs, std::max<qint64>(0, options.nowMs - SampleWindowMs)));
    sqlite3_bind_int64(rawStatement, 2, options.nowMs);
    sqlite3_bind_int(rawStatement, 3, MaxObservations + 1);
    QVector<Observation> observations;
    observations.reserve(MaxObservations + 1);
    int step = SQLITE_OK;
    while ((step = sqlite3_step(rawStatement)) == SQLITE_ROW) {
        if (sqlite3_column_type(rawStatement, 2) == SQLITE_NULL || sqlite3_column_type(rawStatement, 3) == SQLITE_NULL)
            return unavailable(result, "history-metadata-incomplete");
        const Observation value{sqlite3_column_int64(rawStatement, 0), sqlite3_column_int64(rawStatement, 1),
                                sqlite3_column_int64(rawStatement, 2), sqlite3_column_int64(rawStatement, 3)};
        if (value.bytes < 0 || value.timestampMs < 0 || value.frameTimestampMs < 0)
            return unavailable(result, "history-metadata-invalid");
        observations.append(value);
    }
    if (step != SQLITE_DONE) return unavailable(result, "history-busy");
    statement.reset();
    // Indexed endpoint lookups describe retained calendar history without
    // scanning the archive or adding a persistent usage log.
    sqlite3_stmt *rawSpan = nullptr;
    const int spanPrepared = sqlite3_prepare_v2(handle,
        "SELECT (SELECT timestamp_ms FROM observations INDEXED BY observation_time WHERE timestamp_ms>=? AND timestamp_ms<=? ORDER BY timestamp_ms LIMIT 1),"
        "(SELECT timestamp_ms FROM observations INDEXED BY observation_time WHERE timestamp_ms>=? AND timestamp_ms<=? ORDER BY timestamp_ms DESC LIMIT 1),"
        "(SELECT timestamp_ms FROM observations INDEXED BY observation_time ORDER BY timestamp_ms LIMIT 1)",
        -1, &rawSpan, nullptr);
    Statement span(rawSpan, sqlite3_finalize);
    if (spanPrepared == SQLITE_OK) {
        sqlite3_bind_int64(rawSpan, 1, options.sampleAfterMs);
        sqlite3_bind_int64(rawSpan, 2, options.nowMs);
        sqlite3_bind_int64(rawSpan, 3, options.sampleAfterMs);
        sqlite3_bind_int64(rawSpan, 4, options.nowMs);
    }
    if (spanPrepared == SQLITE_OK && sqlite3_step(rawSpan) == SQLITE_ROW &&
        sqlite3_column_type(rawSpan, 0) != SQLITE_NULL && sqlite3_column_type(rawSpan, 1) != SQLITE_NULL) {
        const qint64 first = sqlite3_column_int64(rawSpan, 0), last = sqlite3_column_int64(rawSpan, 1);
        const qint64 oldestRetained = sqlite3_column_int64(rawSpan, 2);
        if (oldestRetained < options.sampleAfterMs)
            result["calendar_estimate_reason"] = "older-capture-settings-retained";
        const double days = first >= 0 && last >= first ? double(last - first) / (24 * 60 * 60 * 1000) : 0;
        // diskBytes describes the whole archive. While older capture settings
        // remain represented, assigning their bytes to the new span would bias
        // the forecast. Active-hour estimates can resume independently.
        if (oldestRetained >= options.sampleAfterMs && last <= options.nowMs && days >= 7 && options.nowMs - last <= SampleWindowMs && options.diskBytes > 0) {
            result["calendar_sample_days"] = days;
            result["bytes_per_calendar_day"] = double(options.diskBytes) / days;
            result["calendar_estimate_reason"] = "retained-calendar-use";
        }
    }
    span.reset();
    sqlite3_exec(handle, "COMMIT", nullptr, nullptr, nullptr);
    result = storageCapacityProjection(result, options);

    if (observations.size() > MaxObservations) {
        observations.removeFirst();
        result["limited_sample"] = true;
    }
    result["sample_observations"] = observations.size();
    if (observations.isEmpty()) return result;
    result["sample_start_ms"] = observations.first().timestampMs;
    result["sample_end_ms"] = observations.last().timestampMs;
    QSet<qint64> seenFrames{observations.first().frameId};
    double activeSeconds = 0, bytes = 0;
    for (qsizetype i = 1; i < observations.size(); ++i) {
        const auto &current = observations[i];
        const double elapsedSeconds = double(current.timestampMs - observations[i - 1].timestampMs) / 1000;
        // Long unobserved intervals include sleep, exclusions and manual pauses;
        // credit only the next observation's expected interval in those cases.
        activeSeconds += elapsedSeconds > options.intervalSeconds * 1.25
            ? options.intervalSeconds : elapsedSeconds;
        if (!seenFrames.contains(current.frameId)) {
            seenFrames.insert(current.frameId);
            if (current.frameTimestampMs >= observations.first().timestampMs) bytes += double(current.bytes);
        }
    }
    result["sample_active_seconds"] = activeSeconds;
    result["sampled_bytes"] = bytes;
    if (observations.size() < MinimumObservations || activeSeconds < MinimumActiveSeconds) return result;
    if (bytes == 0) {
        result["state"] = "no-growth";
        result["reason"] = "no-new-images-in-sample";
        result["bytes_per_active_hour"] = 0;
        // No finite prediction is justified: the next screen may change.
        return result;
    }
    const double hourlyBytes = bytes * 3600 / activeSeconds;
    result["state"] = "ready";
    result["reason"] = "recent-image-growth";
    result["bytes_per_active_hour"] = hourlyBytes;
    return storageCapacityProjection(result, options);
}

}
