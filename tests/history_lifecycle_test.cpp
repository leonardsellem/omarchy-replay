#include "recorder.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

struct Connection {
    sqlite3 *db = nullptr;
    explicit Connection(const QString &directory) {
        require(sqlite3_open(QDir(directory).filePath("index.sqlite").toUtf8().constData(), &db) == SQLITE_OK, "Open fixture DB");
        sqlite3_busy_timeout(db, 1000);
    }
    ~Connection() { sqlite3_close(db); }
    void exec(const QByteArray &query) {
        char *detail = nullptr;
        const auto status = sqlite3_exec(db, query.constData(), nullptr, nullptr, &detail);
        const QString error = QString::fromUtf8(detail ? detail : "Fixture SQL failed");
        sqlite3_free(detail);
        if (status != SQLITE_OK) throw std::runtime_error(error.toStdString());
    }
    qint64 number(const char *query) {
        sqlite3_stmt *statement = nullptr;
        require(sqlite3_prepare_v2(db, query, -1, &statement, nullptr) == SQLITE_OK, "Prepare fixture query");
        const int result = sqlite3_step(statement);
        const qint64 value = sqlite3_column_int64(statement, 0);
        sqlite3_finalize(statement);
        require(result == SQLITE_ROW, "Read fixture query"); return value;
    }
};

replay::RecorderOptions options(const QString &path) {
    replay::RecorderOptions result;
    result.directory = path; result.resume = true; result.archiveFirst = true;
    result.deferredOcr = true; result.minFreeBytes = 0; result.maxDiskBytes = 128ULL * 1024 * 1024;
    return result;
}

QImage image(int variant = 1) {
    QImage result(960, 540, QImage::Format_RGBA8888); result.fill(Qt::white);
    QPainter painter(&result); QFont font("DejaVu Sans"); font.setPixelSize(36); painter.setFont(font);
    painter.drawText(40, 120, "SYNTHETIC HISTORY FIXTURE");
    painter.drawText(40, 230, QString("Invoice REPLAY %1").arg(variant));
    painter.drawText(40, 350, "Retained evidence remains searchable.");
    return result;
}

void indexed(const QString &path, bool reuse = false) {
    replay::IndexerOptions config; config.directory = path; config.ocrMode = "full"; config.ocrReuse = reuse;
    replay::Indexer worker(config);
    for (int i = 0; i < 20; ++i) {
        const auto result = worker.processNext();
        if (result.state == "idle") return;
        require(result.processed && result.state == "ready", "History could not finish indexing");
    }
    throw std::runtime_error("Synthetic history did not drain");
}

void fullyExpire(const QString &path, qint64 cutoff, int observations = 1000, int files = 128) {
    for (int i = 0; i < 30; ++i) if (!replay::maintainHistory(path, cutoff, observations, files).more) return;
    throw std::runtime_error("Bounded expiration did not converge");
}

void sessions(const QString &root) {
    const auto config = options(root + "/sessions");
    qint64 first = 0, second = 0;
    {
        replay::Recorder capture(config);
        bool exclusive = false;
        try { replay::Recorder competing(config); } catch (const std::exception &) { exclusive = true; }
        require(exclusive, "Two capture writers acquired one history");
        first = capture.addFrame(image(), 1000).frameId;
        require(capture.addFrame(image(), 2000).duplicate, "Adjacent identical captures did not share an original");
        capture.breakContinuity();
        second = capture.addFrame(image(), 5000).frameId;
        require(second > first, "Explicit gap extended an old capture");
        capture.finish();
        // finish releases capture ownership even while its statistics remain inspectable.
        replay::Recorder next(config);
        require(next.addFrame(image(), 10000).frameId > second, "Session restart extended old duplicate history");
        next.finish();
    }
    indexed(config.directory, true);
    require(replay::searchFrames(config.directory, "SYNTHETIC HISTORY").size() == 3, "Search missed an earlier recording session");
    const auto usage = replay::historyUsage(config.directory);
    require(usage["observations"].toInteger() == 4 && usage["frames"].toInteger() == 3 &&
            usage["media_bytes"].toDouble() > 0 && usage["disk_bytes"].toDouble() >= usage["media_bytes"].toDouble(),
            "Shared-history counters do not include all sessions");
    auto finite = config; finite.resume = false;
    bool refused = false;
    try { replay::Recorder overwrite(finite); } catch (const std::exception &) { refused = true; }
    require(refused, "Ordinary finite trials began overwriting existing history");
    std::cout << "PASS shared history, session/gap continuity, capture lease and finite-trial refusal\n";
}

void retention(const QString &root) {
    const auto config = options(root + "/retention");
    qint64 shared = 0;
    {
        replay::Recorder capture(config);
        shared = capture.addFrame(image(), 1000).frameId;
        capture.addFrame(image(), 2000); capture.addFrame(image(), 3000);
        capture.addFrame(image(2), 4000); capture.finish();
    }
    indexed(config.directory, true);
    replay::recordGap(config.directory, 100, 500, "sleep");
    replay::recordGap(config.directory, 1500, 3500, "locked");
    const auto bytes = replay::historyUsage(config.directory)["media_bytes"].toDouble();
    const auto first = replay::maintainHistory(config.directory, 2500);
    const auto frame = replay::frameById(config.directory, shared);
    require(first.observationsRemoved == 2 && first.framesRemoved == 0 && frame && frame->timestampMs == 3000 &&
            frame->lastTimestampMs == 3000 && frame->observationCount == 1 &&
            replay::historyUsage(config.directory)["media_bytes"].toDouble() == bytes,
            "Rolling retention removed an original shared by a surviving observation");
    Connection db(config.directory);
    require(db.number("SELECT COUNT(*) FROM history_gaps") == 1 &&
            db.number("SELECT start_ms FROM history_gaps") == 2500, "Old gaps were not expired or clipped");
    require(db.number("SELECT COUNT(*) FROM frame_text") == 2 &&
            db.number("SELECT COUNT(*) FROM frame_ocr_geometry") == 2, "Partial expiry lost searchable text or geometry");
    const auto second = replay::maintainHistory(config.directory, 3500, 1, 1);
    require(second.observationsRemoved == 1 && second.framesRemoved == 1 && second.filesRemoved == 1 &&
            !replay::frameById(config.directory, shared), "Unused original was not reclaimed after its last observation expired");
    require(db.number("SELECT COUNT(*) FROM frame_text") == 1 &&
            db.number("SELECT COUNT(*) FROM frame_ocr_geometry") == 1 &&
            db.number("SELECT COUNT(*) FROM ocr_reuse WHERE frame_id=1") == 0, "Expiry left searchable or cached remnants");
    require(!replay::loadFrame(config.directory, 2).isNull(), "Expiry damaged a surviving original");
    std::cout << "PASS moving-window expiry respects duplicate observations and removes text/geometry/reuse/gaps\n";
}

void stableIdsAndLiveDeletion(const QString &root) {
    const auto config = options(root + "/stable");
    replay::Recorder capture(config);
    const auto old = capture.addFrame(image(), 1000).frameId;
    Connection db(config.directory);
    const auto oldObservation = db.number("SELECT MAX(id) FROM observations");
    fullyExpire(config.directory, 2000, 1, 1);
    const auto next = capture.addFrame(image(), 3000);
    require(next.stored && !next.duplicate && next.frameId > old &&
            db.number("SELECT MAX(id) FROM observations") > oldObservation,
            "Deleting the last frame caused a dangling duplicate or reused evidence ID");
    capture.finish();
    fullyExpire(config.directory, 4000);
    replay::Recorder resumed(config);
    require(resumed.addFrame(image(), 5000).frameId > next.frameId, "Restart reused a deleted frame ID");
    resumed.finish();
    std::cout << "PASS deletion cannot reuse frame/observation IDs or attach to a deleted duplicate\n";
}

void inFlight(const QString &root) {
    const auto config = options(root + "/in-flight");
    qint64 removed = 0, replacement = 0;
    { replay::Recorder capture(config); removed = capture.addFrame(image(), 1000).frameId; capture.finish(); }
    replay::requestIndexing(config.directory, removed, 0);
    bool deleted = false;
    replay::IndexerOptions workerConfig; workerConfig.directory = config.directory; workerConfig.ocrMode = "full";
    workerConfig.cpuPercentProvider = [&](bool) {
        if (!deleted) {
            deleted = true;
            const auto expired = replay::deleteHistoryRange(config.directory, 0, 2000);
            require(expired.framesRemoved == 1 && !expired.more, "In-flight deletion did not finish");
            replay::Recorder capture(config); replacement = capture.addFrame(image(2), 3000).frameId; capture.finish();
        }
        return 100.0;
    };
    replay::Indexer worker(workerConfig);
    const auto obsolete = worker.processNext();
    Connection db(config.directory);
    require(deleted && replacement > removed && obsolete.state == "obsolete" &&
            db.number("SELECT COUNT(*) FROM frame_text") == 0 &&
            db.number("SELECT COUNT(*) FROM frame_ocr_geometry") == 0 &&
            db.number("SELECT COUNT(*) FROM index_requests") == 0,
            "In-flight OCR resurrected deleted text or published onto replacement evidence");
    require(worker.processNext().state == "ready", "Replacement evidence could not be indexed");
    std::cout << "PASS in-flight OCR cannot resurrect deleted evidence\n";
}

void cleanupRecovery(const QString &root) {
    const auto config = options(root + "/cleanup");
    { replay::Recorder capture(config); capture.addFrame(image(), 1000); capture.addFrame(image(2), 2000); capture.finish(); }
    Connection db(config.directory);
    const auto deletion = replay::deleteHistoryRange(config.directory, 0, 3000, 1000, 1);
    require(deletion.framesRemoved == 2 && deletion.filesRemoved == 1 && deletion.more, "Cleanup did not honor its file batch limit");
    // Simulate death after unlink, before the durable cleanup receipt.
    require(QFile::remove(config.directory + "/media/frame-00000002.webp"), "Cannot simulate interrupted unlink");
    const auto recovered = replay::maintainHistory(config.directory, 0);
    require(!recovered.more && replay::historyUsage(config.directory)["media_bytes"].toDouble() == 0 &&
            db.number("SELECT COUNT(*) FROM history_media") == 0, "Interrupted cleanup leaked byte reservations");
    // Simulate death after reserving/partially writing the next original.
    db.exec("BEGIN IMMEDIATE; INSERT INTO history_media VALUES('media/frame-00000003.webp',123,'writing');"
            "UPDATE metadata SET value='123' WHERE key='history_media_bytes';"
            "UPDATE metadata SET value='4' WHERE key='history_next_frame'; COMMIT");
    QFile partial(config.directory + "/media/frame-00000003.webp");
    require(partial.open(QIODevice::WriteOnly) && partial.write("partial") == 7, "Cannot create synthetic partial original"); partial.close();
    replay::Recorder reopened(config);
    require(!QFileInfo::exists(partial.fileName()) && replay::historyUsage(config.directory)["media_bytes"].toDouble() == 0,
            "Restart did not recover the unpublished original and reservation");
    require(reopened.addFrame(image(), 5000).frameId == 4, "Crash recovery reused a reserved filename");
    reopened.finish();
    std::cout << "PASS bounded media GC recovers interrupted unlink and partial publication\n";
}

void legacyMigrationAndSafety(const QString &root) {
    auto config = options(root + "/migration"); config.resume = false;
    { replay::Recorder capture(config); capture.addFrame(image(), 1000); capture.finish(); }
    QFile orphan(config.directory + "/media/frame-00000999.webp");
    require(orphan.open(QIODevice::WriteOnly) && orphan.write("abandoned") == 9, "Create old synthetic orphan"); orphan.close();
    config.resume = true;
    {
        replay::Recorder capture(config);
        require(!QFileInfo::exists(orphan.fileName()), "Old archive orphan was not inventoried and reclaimed");
        require(capture.addFrame(image(2), 2000).frameId == 1000, "Migration collided with an old archive filename");
        capture.finish();
    }
    indexed(config.directory);
    require(replay::searchFrames(config.directory, "Invoice").size() == 2, "Migration lost earlier history");
    QFile outside(root + "/outside.txt");
    require(outside.open(QIODevice::WriteOnly) && outside.write("keep") == 4, "Create synthetic safety sentinel"); outside.close();
    Connection db(config.directory);
    db.exec("INSERT INTO history_media VALUES('media/frame-00001001.webp',0,'retired')");
    const auto link = config.directory + "/media/frame-00001001.webp";
    require(QFile::link(outside.fileName(), link), "Create synthetic symlink guard");
    bool refused = false;
    try { replay::maintainHistory(config.directory, 0); } catch (const std::exception &) { refused = true; }
    require(refused && QFileInfo(link).isSymLink() && QFileInfo(outside.fileName()).size() == 4,
            "Cleanup followed or removed an unsafe symlink");
    require(QDir().mkdir(root + "/outside-directory") && QDir(config.directory).rmdir("staging"), "Prepare staging guard fixture");
    const auto permissions = QFileInfo(root + "/outside-directory").permissions();
    require(QFile::link(root + "/outside-directory", config.directory + "/staging"), "Create unsafe staging symlink");
    refused = false;
    try { replay::Recorder capture(config); } catch (const std::exception &) { refused = true; }
    require(refused && QFileInfo(root + "/outside-directory").permissions() == permissions,
            "Resume changed permissions through an unsafe staging path");
    std::cout << "PASS archive migration and private-media symlink protection\n";
}

QImage noise(quint32 seed, QSize size = QSize(960, 540)) {
    QImage result(size, QImage::Format_RGBA8888);
    for (int row = 0; row < result.height(); ++row) {
        auto *pixels = result.scanLine(row);
        for (int column = 0; column < result.width(); ++column) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            pixels[4 * column] = seed; pixels[4 * column + 1] = seed >> 8;
            pixels[4 * column + 2] = seed >> 16; pixels[4 * column + 3] = 255;
        }
    }
    return result;
}

void diskAccounting(const QString &root) {
    auto config = options(root + "/disk"); config.maxDiskBytes = 16ULL * 1024 * 1024;
    { replay::Recorder first(config); first.addFrame(noise(1), 1000); first.addFrame(noise(2), 2000); first.finish(); }
    const auto prior = replay::historyUsage(config.directory)["media_bytes"].toDouble();
    require(prior > 2 * 1024 * 1024, "Synthetic disk fixture was unexpectedly compressible");
    bool stopped = false;
    {
        replay::Recorder resumed(config);
        for (int i = 3; i < 12; ++i) {
            try { resumed.addFrame(noise(i), i * 1000); }
            catch (const std::exception &) { stopped = true; break; }
        }
    }
    const auto full = replay::historyUsage(config.directory);
    require(stopped && full["disk_bytes"].toDouble() <= config.maxDiskBytes &&
            full["media_bytes"].toDouble() >= prior, "Append ignored existing media in its disk budget");
    fullyExpire(config.directory, 100000);
    require(replay::historyUsage(config.directory)["media_bytes"].toDouble() == 0, "Retention did not release the archive budget");
    replay::Recorder resumed(config);
    require(resumed.addFrame(image(), 200000).stored, "Reclaimed disk budget could not accept new history"); resumed.finish();
    std::cout << "PASS append enforces total disk limits and retention makes the released budget reusable\n";
}

void rollingStorage(const QString &root) {
    constexpr quint64 MiB = 1024 * 1024;
    auto config = options(root + "/rolling"); config.maxDiskBytes = 16 * MiB; config.rollingStorage = true;
    qint64 first = 0, latest = 0;
    {
        replay::Recorder capture(config);
        first = capture.addFrame(noise(1), 1000).frameId;
        require(capture.addFrame(noise(1), 1000).duplicate, "Same-time duplicate fixture did not share its original");
        for (int i = 2; i <= 14; ++i) {
            const auto accepted = capture.addFrame(noise(i), i * 1000);
            require(accepted.stored, "Rolling archive stopped accepting new moments"); latest = accepted.frameId;
            require(replay::historyUsage(config.directory)["disk_bytes"].toDouble() <= config.maxDiskBytes,
                    "Rolling admission exceeded its storage allowance");
        }
        require(!replay::frameById(config.directory, first) && replay::frameById(config.directory, latest),
                "Rolling archive did not evict the oldest evidence while preserving new evidence");
        require(capture.addFrame(noise(14), 15000).duplicate, "Rolling cleanup broke a surviving duplicate reference");
        const auto beforeOversized = replay::historyUsage(config.directory);
        bool rejected = false;
        try { capture.addFrame(noise(15, QSize(2048, 2048)), 16000); }
        catch (const replay::RollingStorageUnavailable &failure) { rejected = !failure.retryable; }
        const auto afterOversized = replay::historyUsage(config.directory);
        require(rejected && beforeOversized["observations"] == afterOversized["observations"] &&
                beforeOversized["frames"] == afterOversized["frames"] && beforeOversized["media_bytes"] == afterOversized["media_bytes"],
                "Oversized incoming image reclaimed history before its actual size was known");
        require(capture.addFrame(image(), 17000).stored, "Rejected oversized image permanently stopped rolling admission");
        capture.finish();
    }
    Connection db(config.directory);
    require(db.number("SELECT MIN(timestamp_ms) FROM observations") > 1000 &&
            db.number("SELECT COUNT(*) FROM frames WHERE ocr_state='pending'") > 0 &&
            db.number("SELECT COUNT(*) FROM history_media WHERE state<>'live'") == 0 &&
            db.number("SELECT COUNT(*) FROM observations o LEFT JOIN frames f ON f.id=o.frame_id WHERE f.id IS NULL") == 0,
            "Rolling cleanup left stale queue, media, or observation records");
    const auto count = db.number("SELECT COUNT(*) FROM observations");
    const auto oversized = replay::makeHistorySpace(config.directory, config.maxDiskBytes, 0, config.maxDiskBytes);
    require(!oversized.ready && !oversized.more && db.number("SELECT COUNT(*) FROM observations") == count,
            "An impossible image erased history before failing");
    QStorageInfo disk(config.directory); disk.refresh();
    const auto impossibleReserve = quint64(disk.bytesAvailable()) + config.maxDiskBytes * 4;
    const auto impossible = replay::makeHistorySpace(config.directory, config.maxDiskBytes, impossibleReserve, MiB);
    require(!impossible.ready && !impossible.more && db.number("SELECT COUNT(*) FROM observations") == count,
            "An impossible physical disk reserve erased history");
    // A bounded batch can remove an observation without deleting a shared
    // original; subsequent calls continue in timestamp/ID order.
    const auto sharedConfig = options(root + "/rolling-shared");
    qint64 shared = 0;
    { replay::Recorder capture(sharedConfig);
      shared = capture.addFrame(noise(1), 1000).frameId;
      capture.addFrame(noise(1), 1000); capture.addFrame(noise(2), 2000); capture.finish(); }
    const auto before = replay::historyUsage(sharedConfig.directory);
    const quint64 required = 16 * MiB - quint64(before["disk_bytes"].toDouble()) + MiB;
    auto step = replay::makeHistorySpace(sharedConfig.directory, 16 * MiB, 0, required, 1, 1);
    require(!step.ready && step.more && step.maintenance.observationsRemoved == 1 && step.maintenance.filesRemoved == 0 &&
            replay::frameById(sharedConfig.directory, shared)->observationCount == 1,
            "Bounded capacity cleanup removed a still-shared original");
    step = replay::makeHistorySpace(sharedConfig.directory, 16 * MiB, 0, required, 1, 1);
    require(step.ready && !replay::frameById(sharedConfig.directory, shared) &&
            replay::historyUsage(sharedConfig.directory)["observations"].toInteger() == 1,
            "Bounded cleanup did not finish the oldest shared original before newer history");
    std::cout << "PASS rolling capacity keeps recording, preserves shared references and rejects futile deletion\n";
}

void rollingIndexShrink(const QString &root) {
    constexpr quint64 MiB = 1024 * 1024;
    for (bool legacy : {false, true}) {
        auto config = options(root + (legacy ? "/legacy-index-limit" : "/rolling-index-limit"));
        { replay::Recorder capture(config);
          for (int i = 1; i <= 12; ++i) capture.addFrame(image(i), i * 1000);
          capture.finish(); }
        Connection db(config.directory);
        if (legacy) db.exec("PRAGMA auto_vacuum=NONE; VACUUM");
        // Simulate a large but supported one-MiB OCR text value for each frame.
        // Both canonical text and FTS content consume real database pages.
        db.exec("UPDATE frames SET text=CAST(zeroblob(1048576) AS TEXT),ocr_state='ready';"
                "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames; PRAGMA wal_checkpoint(TRUNCATE)");
        require(QFileInfo(config.directory + "/index.sqlite").size() > 16 * qint64(MiB),
                "Index shrink fixture did not exceed the smaller allowance");
        require(db.number("PRAGMA auto_vacuum") == (legacy ? 0 : 2), "Index vacuum fixture has wrong format");
        replay::HistorySpaceResult result;
        int attempts = 0;
        qint64 previousFirst = 0;
        for (; attempts < 512; ++attempts) {
            result = replay::makeHistorySpace(config.directory, 16 * MiB, 0, 8 * MiB, 1, 1);
            require(result.maintenance.observationsRemoved <= 1 && result.maintenance.filesRemoved <= 1,
                    "Index reclamation exceeded its bounded row/file batches");
            const auto first = db.number("SELECT COALESCE(MIN(id),0) FROM frames");
            require(first >= previousFirst, "Index reclamation stopped deleting oldest frames first"); previousFirst = first;
            if (result.ready || !result.more) break;
        }
        if (legacy) {
            require(!result.ready && !result.more && db.number("SELECT COUNT(*) FROM observations") == 12 &&
                    result.reason.contains("older history index"),
                    "Legacy non-shrinkable index was erased or retried indefinitely");
        } else {
            require(result.ready && attempts > 1 && db.number("SELECT COUNT(*) FROM observations") > 0 &&
                    db.number("SELECT MIN(id) FROM frames") > 1 && db.number("SELECT MAX(id) FROM frames") == 12,
                    "Incremental index pages did not reclaim space while preserving newer history");
            config.maxDiskBytes = 16 * MiB; config.rollingStorage = true;
            replay::Recorder capture(config);
            require(capture.addFrame(image(99), 20000).stored, "Index shrink did not restore rolling recording"); capture.finish();
            require(replay::historyUsage(config.directory)["disk_bytes"].toDouble() <= config.maxDiskBytes,
                    "Index shrink resumed above its allowance");
        }
    }
    std::cout << "PASS capacity reduction reclaims index pages incrementally and preserves incompatible legacy indexes\n";
}
}

int main(int argc, char **argv) {
    qputenv("OMP_THREAD_LIMIT", "1"); QApplication application(argc, argv); QTemporaryDir root;
    try {
        require(root.isValid(), "Create temporary history test directory");
        sessions(root.path()); retention(root.path()); stableIdsAndLiveDeletion(root.path()); inFlight(root.path());
        cleanupRecovery(root.path()); legacyMigrationAndSafety(root.path()); diskAccounting(root.path()); rollingStorage(root.path());
        rollingIndexShrink(root.path());
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
