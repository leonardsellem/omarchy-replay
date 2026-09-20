#include "recorder.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <csignal>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

struct Connection {
    sqlite3 *db = nullptr;
    explicit Connection(const QString &directory) {
        require(sqlite3_open(QDir(directory).filePath("index.sqlite").toUtf8().constData(), &db) == SQLITE_OK,
                "Cannot open synthetic database");
        sqlite3_busy_timeout(db, 1000);
    }
    ~Connection() { sqlite3_close(db); }
    void exec(const char *sql) { require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "Fixture SQL failed"); }
    qint64 count(const char *sql) {
        sqlite3_stmt *statement = nullptr;
        require(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK, "Fixture query failed");
        const int status = sqlite3_step(statement);
        const auto value = sqlite3_column_int64(statement, 0);
        sqlite3_finalize(statement);
        require(status == SQLITE_ROW, "Fixture query returned no value");
        return value;
    }
};

QString seed(const QString &root, const QString &name) {
    replay::RecorderOptions options;
    options.directory = root + '/' + name;
    options.deferredOcr = true;
    options.archiveFirst = true;
    options.minFreeBytes = 0;
    replay::Recorder recorder(options);
    for (int i = 0; i < 2; ++i) {
        QImage image(960, 540, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        QPainter painter(&image);
        QFont font("DejaVu Sans"); font.setPixelSize(36); painter.setFont(font);
        painter.drawText(40, 140, QString("SYNTHETIC INVOICE %1").arg(i));
        painter.drawText(40, 220, "Original evidence stays searchable.");
        painter.end();
        require(recorder.addFrame(image, 1000 + i * 1000).stored, "Fixture did not retain image");
    }
    recorder.finish();
    replay::requestIndexing(options.directory, 1, 0);
    return options.directory;
}

replay::IndexerOptions options(const QString &directory) {
    replay::IndexerOptions result;
    result.directory = directory; result.ocrMode = "full";
    return result;
}

QByteArray originals(const QString &directory) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QDir media(directory + "/media");
    for (const auto &name : media.entryList(QDir::Files, QDir::Name)) {
        QFile file(media.filePath(name)); require(file.open(QIODevice::ReadOnly), "Original disappeared");
        hash.addData(file.readAll());
    }
    return hash.result();
}

void pendingIntact(Connection &db) {
    require(db.count("SELECT COUNT(*) FROM frames WHERE ocr_state='pending' AND source_path<>'' AND ocr_error=''") == 2,
            "Contention changed a pending image or discarded its source");
    require(db.count("SELECT COUNT(*) FROM index_requests") == 1, "Contention consumed the priority request");
    require(db.count("SELECT COUNT(*) FROM frame_text") == 0 &&
            db.count("SELECT COUNT(*) FROM frame_ocr_geometry") == 0, "Contended publication partially committed");
}

void readyIntact(const QString &directory, const QByteArray &before) {
    Connection db(directory);
    require(db.count("SELECT COUNT(*) FROM frames WHERE ocr_state='ready' AND text<>'' AND ocr_error=''") == 2,
            "Worker did not finish both retained images");
    require(db.count("SELECT COUNT(*) FROM frame_text") == 2 &&
            db.count("SELECT COUNT(*) FROM frame_ocr_geometry WHERE lines_json<>'[]'") == 2,
            "Recovery duplicated FTS or lost OCR geometry");
    require(db.count("SELECT COUNT(*) FROM index_requests") == 0, "Completed request was not consumed");
    require(originals(directory) == before, "Recovery changed original images");
}

void drain(replay::Indexer &worker) {
    for (int i = 0; i < 4; ++i) {
        const auto result = worker.processNext();
        if (result.state == "idle") return;
        require(result.processed && result.state == "ready", "Recovery did not publish pending OCR");
    }
    throw std::runtime_error("Worker did not finish the synthetic queue");
}

void selection(const QString &root) {
    const auto directory = seed(root, "selection");
    const auto before = originals(directory);
    replay::Indexer worker(options(directory));
    require(worker.retryFailed() == 0, "Fixture unexpectedly had failed jobs");
    Connection writer(directory);
    writer.exec("BEGIN IMMEDIATE");
    QElapsedTimer elapsed; elapsed.start();
    const auto blocked = worker.processNext();
    require(!blocked.processed && !blocked.canceled && blocked.state == "busy", "Busy selection looked like an empty queue");
    require(elapsed.elapsed() < 750, "Selection contention did not yield promptly");
    pendingIntact(writer);
    // The foreign writer outlives the old one-second SQLite timeout.
    QThread::msleep(1200);
    writer.exec("ROLLBACK");
    drain(worker); readyIntact(directory, before);
    require(worker.statsJSON().value("database_contentions").toInteger() > 0, "Contention was not diagnosed");
    std::cout << "PASS selection contention defers and recovers without losing pending work\n";
}

void publication(const QString &root) {
    const auto directory = seed(root, "publication");
    const auto before = originals(directory);
    Connection writer(directory);
    bool locked = false;
    auto config = options(directory);
    config.cpuPercentProvider = [&](bool) {
        if (!locked) { writer.exec("BEGIN IMMEDIATE"); locked = true; }
        return 100.0;
    };
    replay::Indexer worker(config);
    const auto blocked = worker.processNext();
    require(locked && !blocked.processed && blocked.state == "busy", "Publication contention did not defer");
    pendingIntact(writer);
    QThread::msleep(1200);
    writer.exec("ROLLBACK");
    drain(worker); readyIntact(directory, before);
    require(worker.statsJSON().value("failed_jobs").toInteger() == 0, "Database lock became a failed OCR image");
    std::cout << "PASS publication contention preserves requests, originals, FTS and geometry\n";
}

void cancellation(const QString &root) {
    const auto directory = seed(root, "cancel");
    Connection writer(directory);
    QElapsedTimer elapsed;
    auto config = options(directory);
    config.stopRequested = [&] { return elapsed.isValid() && elapsed.elapsed() >= 150; };
    replay::Indexer worker(config);
    require(worker.retryFailed() == 0, "Fixture unexpectedly had failed jobs");
    writer.exec("BEGIN IMMEDIATE"); elapsed.start();
    const auto result = worker.processNext();
    require(result.canceled && !result.processed && elapsed.elapsed() < 750, "Cancellation did not interrupt lock backoff");
    pendingIntact(writer); writer.exec("ROLLBACK");
    std::cout << "PASS cancellation while waiting for a writer preserves pending evidence\n";
}

void schedulerRead(const QString &root) {
    const auto directory = seed(root, "scheduler-read");
    const auto before = originals(directory);
    Connection writer(directory);
    // WAL normally allows the scheduler read. A rollback-journal database
    // makes the monitor's read collision deterministic without a test hook.
    writer.exec("PRAGMA journal_mode=DELETE");
    bool locked = false;
    auto config = options(directory);
    config.cpuPercentProvider = [&](bool) {
        if (!locked) {
            writer.exec("BEGIN EXCLUSIVE"); locked = true;
            QThread::msleep(1100); // The next budget callback polls the schedule.
        }
        return 100.0;
    };
    replay::Indexer worker(config);
    const auto blocked = worker.processNext();
    require(locked && !blocked.processed && blocked.state == "busy", "Scheduler read collision became an OCR failure");
    pendingIntact(writer); writer.exec("ROLLBACK");
    drain(worker); readyIntact(directory, before);
    require(worker.statsJSON().value("failed_jobs").toInteger() == 0, "Read collision failed an image");
    std::cout << "PASS typed scheduler read failures survive the noexcept OCR callback boundary\n";
}

void permanentError(const QString &root) {
    const auto directory = seed(root, "permanent");
    Connection writer(directory);
    writer.exec("CREATE TRIGGER reject_publication BEFORE UPDATE OF text ON frames BEGIN SELECT RAISE(ABORT,'synthetic failure'); END");
    replay::Indexer worker(options(directory));
    bool threw = false;
    try { worker.processNext(); } catch (const std::exception &) { threw = true; }
    require(threw, "Permanent SQLite failure was silently retried");
    pendingIntact(writer);
    writer.exec("DROP TRIGGER reject_publication");
    drain(worker);
    std::cout << "PASS permanent database errors remain errors, not transient waits\n";
}

void commandLoop(const QString &root, const QString &binary, bool follow) {
    const auto directory = seed(root, follow ? "follow" : "one-shot");
    const auto before = originals(directory);
    Connection writer(directory); writer.exec("BEGIN IMMEDIATE");
    QProcess process;
    QStringList arguments{"index", "--dir", directory, "--ocr-mode", "full", "--ocr-cpu-ceiling-percent", "0"};
    if (follow) arguments << "--follow";
    process.start(binary, arguments);
    require(process.waitForStarted(3000), "CLI worker did not start");
    QThread::msleep(1400);
    require(!process.waitForFinished(1), "CLI worker exited during startup contention");
    pendingIntact(writer); writer.exec("ROLLBACK");
    if (follow) {
        QElapsedTimer elapsed; elapsed.start();
        while (writer.count("SELECT COUNT(*) FROM frames WHERE ocr_state='ready'") != 2 && elapsed.elapsed() < 10000)
            QThread::msleep(50);
        readyIntact(directory, before);
        process.terminate();
    }
    require(process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            "CLI worker failed after contention cleared");
    readyIntact(directory, before);
    const auto receipt = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    require(receipt.value("database_contentions").toInteger() > 0, "CLI receipt omitted database waits");
    std::cout << "PASS " << (follow ? "follow" : "one-shot") << " CLI waits through startup contention and completes\n";
}
}

int main(int argc, char **argv) {
    qputenv("OMP_THREAD_LIMIT", "1");
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    try {
        require(temporary.isValid() && argc >= 2, "Pass the Replay executable");
        const QString mode = argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString();
        if (mode.isEmpty() || mode == "selection") selection(temporary.path());
        if (mode.isEmpty() || mode == "publication") publication(temporary.path());
        if (mode.isEmpty()) {
            cancellation(temporary.path()); schedulerRead(temporary.path()); permanentError(temporary.path());
            commandLoop(temporary.path(), QString::fromLocal8Bit(argv[1]), false);
            commandLoop(temporary.path(), QString::fromLocal8Bit(argv[1]), true);
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
    return 0;
}
