#include "storage_forecast.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>

namespace {
constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 GiB = 1024 * MiB;
constexpr qint64 StartMs = 20LL * 24 * 60 * 60 * 1000;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message) {
    require(std::abs(actual - expected) <= std::max(1e-8, std::abs(expected) * 1e-8), message);
}

class Fixture {
public:
    QString directory;
    sqlite3 *db = nullptr;

    explicit Fixture(const QString &path) : directory(path) {
        require(QDir().mkpath(path), "Create synthetic history directory");
        require(sqlite3_open(QDir(path).filePath("index.sqlite").toUtf8().constData(), &db) == SQLITE_OK,
                "Open synthetic history database");
        execute("CREATE TABLE observations(id INTEGER PRIMARY KEY,timestamp_ms INTEGER NOT NULL,frame_id INTEGER NOT NULL);"
                "CREATE INDEX observation_time ON observations(timestamp_ms);"
                "CREATE TABLE frames(id INTEGER PRIMARY KEY,timestamp_ms INTEGER NOT NULL,path TEXT NOT NULL);"
                "CREATE TABLE history_media(path TEXT PRIMARY KEY,bytes INTEGER NOT NULL,state TEXT NOT NULL); BEGIN");
    }
    ~Fixture() { sqlite3_close(db); }
    void execute(const QByteArray &sql) {
        char *detail = nullptr;
        const int status = sqlite3_exec(db, sql.constData(), nullptr, nullptr, &detail);
        const QString message = QString::fromUtf8(detail ? detail : "Synthetic query failed");
        sqlite3_free(detail);
        if (status != SQLITE_OK) throw std::runtime_error(message.toStdString());
    }
    void frame(qint64 id, qint64 timestamp, qint64 bytes = MiB) {
        execute(QString("INSERT INTO frames VALUES(%1,%2,'media/%1.webp');"
                        "INSERT INTO history_media VALUES('media/%1.webp',%3,'live')")
                    .arg(id).arg(timestamp).arg(bytes).toUtf8());
    }
    void observation(qint64 id, qint64 timestamp, qint64 frameId) {
        execute(QString("INSERT INTO observations VALUES(%1,%2,%3)").arg(id).arg(timestamp).arg(frameId).toUtf8());
    }
    void finish() { execute("COMMIT"); }
    void sequence(int count = 121, int sameImageCount = 1, qint64 gapMs = 0, qint64 spacingMs = 5000) {
        for (int i = 0; i < count; ++i) {
            const qint64 timestamp = StartMs + i * spacingMs + (i >= count / 2 ? gapMs : 0);
            const qint64 frameId = i / sameImageCount + 1;
            if (i % sameImageCount == 0) frame(frameId, timestamp);
            observation(i + 1, timestamp, frameId);
        }
        finish();
    }
    QByteArray hash() const {
        QFile file(QDir(directory).filePath("index.sqlite"));
        require(file.open(QIODevice::ReadOnly), "Read synthetic database checksum");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        require(hash.addData(&file), "Checksum synthetic database");
        return hash.result();
    }
};

replay::StorageForecastOptions options(qint64 now = StartMs + 600000) {
    replay::StorageForecastOptions value;
    value.nowMs = now;
    value.intervalSeconds = 5;
    value.diskBytes = GiB;
    value.maxDiskBytes = 10 * GiB;
    value.freeBytes = 100 * GiB;
    value.minFreeBytes = GiB;
    return value;
}

void normalRateAndReadOnly(const QString &root) {
    Fixture fixture(root + "/normal"); fixture.sequence();
    const auto before = fixture.hash();
    const auto result = replay::storageForecast(fixture.directory, options());
    require(result["state"] == "ready" && result["warning"] == "none", "Sufficient changing history should have a ready forecast");
    require(result["sample_observations"].toInt() == 121 && !result["limited_sample"].toBool(), "Normal sample observation count");
    near(result["sample_active_seconds"].toDouble(), 600, "Rate includes active recording time");
    near(result["sampled_bytes"].toDouble(), 120 * MiB, "Boundary image must not be charged without its preceding interval");
    near(result["bytes_per_active_hour"].toDouble(), 720 * MiB, "Hourly rate is numeric image growth");
    near(result["capacity_active_hours"].toDouble(), 10.0 * GiB / (720 * MiB), "Capacity uses the whole rolling allowance");
    require(result["limiting_factor"] == "allowance" && result["effective_capacity_bytes"].toInteger() == 10 * GiB,
            "Disk allowance limits capacity");
    require(fixture.hash() == before, "Forecast modified the existing history");
}

void duplicatesAndSuspendedTime(const QString &root) {
    Fixture duplicate(root + "/duplicates"); duplicate.sequence(121, 10);
    const auto result = replay::storageForecast(duplicate.directory, options());
    near(result["sampled_bytes"].toDouble(), 12 * MiB, "Repeated observations must not repeatedly charge their original");
    near(result["bytes_per_active_hour"].toDouble(), 72 * MiB, "Repeated observations still contribute recording time");

    Fixture sleeping(root + "/sleep"); sleeping.sequence(121, 1, 2 * 60 * 60 * 1000);
    const auto afterSleep = replay::storageForecast(sleeping.directory, options(StartMs + 600000 + 2 * 60 * 60 * 1000));
    near(afterSleep["sample_active_seconds"].toDouble(), 600, "Unobserved lock/sleep periods inflated active time");
    near(afterSleep["bytes_per_active_hour"].toDouble(), 720 * MiB, "Sleep must not make estimated image growth appear cheap");

    Fixture jitter(root + "/jitter"); jitter.sequence(121, 1, 0, 5240);
    const auto delayed = replay::storageForecast(jitter.directory, options(StartMs + 120 * 5240));
    near(delayed["sample_active_seconds"].toDouble(), 628.8, "Ordinary capture latency should contribute measured spacing");
}

void boundsAndWindow(const QString &root) {
    Fixture large(root + "/bounded"); large.sequence(20000);
    const auto result = replay::storageForecast(large.directory, options(StartMs + 19999LL * 5000));
    require(result["state"] == "ready" && result["sample_observations"].toInt() == 2000 && result["limited_sample"].toBool(),
            "Forecast must read a bounded recent sample even as history grows");
    near(result["sample_active_seconds"].toDouble(), 1999 * 5, "Bounded sample interval count");
    near(result["sampled_bytes"].toDouble(), 1999 * MiB, "Sample boundary cannot charge an older original");

    // A single old original remains referenced by fresh observations. It is not
    // new archive growth just because the sample or retention boundary moved.
    Fixture oldOriginal(root + "/old-original"); oldOriginal.frame(1, 1000);
    for (int i = 0; i < 121; ++i) oldOriginal.observation(i + 1, StartMs + i * 5000, 1);
    oldOriginal.finish();
    const auto stable = replay::storageForecast(oldOriginal.directory, options());
    require(stable["state"] == "no-growth" && stable["capacity_active_hours"].isNull(), "Stable screens must not promise infinite remaining time");
    near(stable["bytes_per_active_hour"].toDouble(), 0, "Old original was charged as recent storage growth");

    const auto stale = replay::storageForecast(oldOriginal.directory, options(StartMs + 600000 + 25 * 60 * 60 * 1000));
    require(stale["state"] == "insufficient-data" && stale["sample_observations"].toInt() == 0,
            "Old samples outside the 24-hour window must not produce a current forecast");
}

void limitsAndChanges(const QString &root) {
    Fixture fixture(root + "/capacity"); fixture.sequence();
    auto capacity = options();
    capacity.freeBytes = capacity.minFreeBytes + 512 * MiB;
    auto result = replay::storageForecast(fixture.directory, capacity);
    require(result["effective_capacity_bytes"].toInteger() == 1536 * MiB && result["limiting_factor"] == "free-space" && result["warning"] == "free-space",
            "Reusable Replay storage must be included alongside filesystem headroom");
    near(result["capacity_active_hours"].toDouble(), 1536.0 / 720, "Free-space limited rolling capacity");
    capacity.freeBytes = capacity.minFreeBytes;
    result = replay::storageForecast(fixture.directory, capacity);
    require(result["effective_capacity_bytes"].toInteger() == GiB,
            "Reaching the free-space reserve must retain the capacity available through rolling replacement");
    capacity.freeBytes = 0;
    result = replay::storageForecast(fixture.directory, capacity);
    require(result["effective_capacity_bytes"].toInteger() == 0, "Capacity must first repay an existing free-space deficit");
    capacity = options(); capacity.diskBytes = capacity.maxDiskBytes + MiB;
    result = replay::storageForecast(fixture.directory, capacity);
    require(result["warning"] == "none" && result["effective_capacity_bytes"].toInteger() == 10 * GiB,
            "A full allowance is normal rolling storage, not a warning or a stop prediction");
    capacity = options(); capacity.freeBytes = -1;
    result = replay::storageForecast(fixture.directory, capacity);
    require(result["state"] == "unavailable" && result["reason"] == "free-space-unknown" &&
            result["effective_capacity_bytes"].isNull() && result["capacity_active_hours"].isNull(), "Unknown free space cannot promise usable capacity");

    capacity = options(); capacity.maxDiskBytes = 20 * GiB;
    result = replay::storageForecast(fixture.directory, capacity);
    near(result["capacity_active_hours"].toDouble(), 20.0 * GiB / (720 * MiB), "Changed capacity must apply immediately");
    Fixture other(root + "/different-archive"); other.sequence(121, 10);
    const auto switched = replay::storageForecast(other.directory, capacity);
    near(switched["bytes_per_active_hour"].toDouble(), 72 * MiB, "Archive change must not inherit another archive's growth rate");
    const auto before = fixture.hash();
    capacity.maxDiskBytes = 30 * GiB;
    const auto preview = replay::storageCapacityProjection(result, capacity);
    near(preview["capacity_active_hours"].toDouble(), 30.0 * GiB / (720 * MiB), "Settings must preview selected capacity using measured rate");
    require(fixture.hash() == before, "Capacity preview modified history");
}

void calendarUse(const QString &root) {
    Fixture fixture(root + "/calendar");
    const qint64 last = StartMs + 600000;
    fixture.frame(1, last - 8LL * 24 * 60 * 60 * 1000);
    fixture.observation(1, last - 8LL * 24 * 60 * 60 * 1000, 1);
    for (int i = 0; i < 121; ++i) {
        fixture.frame(i + 2, StartMs + i * 5000);
        fixture.observation(i + 2, StartMs + i * 5000, i + 2);
    }
    fixture.finish();
    auto capacity = options(last);
    const auto result = replay::storageForecast(fixture.directory, capacity);
    near(result["calendar_sample_days"].toDouble(), 8, "Use actual retained calendar span");
    near(result["bytes_per_calendar_day"].toDouble(), double(GiB) / 8, "Calendar rate must use actual footprint and span without assumed work hours");
    near(result["capacity_calendar_days"].toDouble(), 80, "Calendar capacity uses whole rolling allowance");
    near(result["estimated_retention_bytes"].toDouble(), 30.0 * GiB / 8, "Estimate bytes needed for configured retention");
    capacity.retentionDays = 14;
    const auto preview = replay::storageCapacityProjection(result, capacity);
    near(preview["estimated_retention_bytes"].toDouble(), 14.0 * GiB / 8, "Changing retention previews measured calendar usage");
    Fixture shortSpan(root + "/short-calendar"); shortSpan.sequence();
    const auto shortResult = replay::storageForecast(shortSpan.directory, options());
    require(shortResult["bytes_per_calendar_day"].isNull() && shortResult["estimated_retention_bytes"].isNull(),
            "A brief sample must never be extrapolated into an assumed work day");
    const auto stale = replay::storageForecast(fixture.directory, options(last + 25 * 60 * 60 * 1000));
    require(stale["bytes_per_calendar_day"].isNull(), "Stale archive cannot supply current calendar-use projection");

    // Eight days under current settings are not enough to attribute the whole
    // archive footprint if even older settings are still retained.
    fixture.frame(5000, last - 16LL * 24 * 60 * 60 * 1000);
    fixture.observation(5000, last - 16LL * 24 * 60 * 60 * 1000, 5000);
    capacity.sampleAfterMs = last - 8LL * 24 * 60 * 60 * 1000;
    const auto mixedSettings = replay::storageForecast(fixture.directory, capacity);
    require(mixedSettings["state"] == "ready" && mixedSettings["capacity_active_hours"].isDouble() &&
            mixedSettings["bytes_per_calendar_day"].isNull() &&
            mixedSettings["calendar_estimate_reason"] == "older-capture-settings-retained",
            "Old capture settings cannot be attributed to the new calendar span");
}

void captureSettingBoundary(const QString &root) {
    Fixture fixture(root + "/capture-settings"); fixture.sequence();
    auto capacity = options();
    capacity.sampleAfterMs = StartMs + 500000;
    auto result = replay::storageForecast(fixture.directory, capacity);
    require(result["state"] == "insufficient-data" && result["sample_observations"].toInt() == 21 &&
            result["capacity_active_hours"].isNull(), "Older capture settings must not fill the new sample");
    near(result["sample_active_seconds"].toDouble(), 100, "Only post-change recording time contributes");
    require(result["sample_after_ms"].toInteger() == capacity.sampleAfterMs, "Boundary is visible for diagnostics");
    capacity.sampleAfterMs = StartMs + 300000;
    result = replay::storageForecast(fixture.directory, capacity);
    require(result["state"] == "ready" && result["sample_observations"].toInt() == 61,
            "Five fresh recording minutes permit an active estimate");
    near(result["bytes_per_active_hour"].toDouble(), 720 * MiB, "Fresh sample has its own measured rate");
    capacity.sampleAfterMs = capacity.nowMs + 1;
    result = replay::storageForecast(fixture.directory, capacity);
    require(result["state"] == "insufficient-data" && result["sample_observations"].toInt() == 0 &&
            result["capacity_active_hours"].isNull() && result["bytes_per_calendar_day"].isNull(),
            "A new capture configuration starts without a forecast");
}

void insufficientAndUnavailable(const QString &root) {
    Fixture fixture(root + "/short"); fixture.sequence(10);
    auto capacity = options(); capacity.diskBytes = capacity.maxDiskBytes - MiB;
    const auto result = replay::storageForecast(fixture.directory, capacity);
    require(result["state"] == "insufficient-data" && result["capacity_active_hours"].isNull() && result["warning"] == "none",
            "Short sample must suppress time prediction without treating a full rolling allowance as a warning");
    capacity = options(); capacity.intervalSeconds = 0;
    require(replay::storageForecast(fixture.directory, capacity)["state"] == "unavailable", "Invalid interval was accepted");
    const QString missing = root + "/missing";
    require(replay::storageForecast(missing, options())["state"] == "unavailable" && !QFileInfo::exists(missing),
            "Reading a missing history must not create files");

    Fixture damaged(root + "/incomplete"); damaged.observation(1, StartMs, 42); damaged.finish();
    require(replay::storageForecast(damaged.directory, options())["reason"] == "history-metadata-incomplete",
            "Missing image metadata cannot be silently omitted from the estimate");

    fixture.execute("BEGIN EXCLUSIVE");
    require(replay::storageForecast(fixture.directory, options())["state"] == "unavailable", "Busy history should yield without a prediction");
    fixture.execute("ROLLBACK");
}

}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Create synthetic test directory");
        normalRateAndReadOnly(temporary.path());
        duplicatesAndSuspendedTime(temporary.path());
        boundsAndWindow(temporary.path());
        limitsAndChanges(temporary.path());
        insufficientAndUnavailable(temporary.path());
        calendarUse(temporary.path());
        captureSettingBoundary(temporary.path());
        std::cout << "PASS bounded read-only storage forecasts, duplicate reuse, suspended time and capacity limits\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
