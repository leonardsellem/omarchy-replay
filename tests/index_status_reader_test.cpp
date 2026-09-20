#include "recorder.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <sqlite3.h>
#include <sys/stat.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Writer {
    sqlite3 *db = nullptr;
    explicit Writer(const QString &path) {
        require(sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK,
                "Cannot open synthetic writer");
        sqlite3_busy_timeout(db, 100);
    }
    ~Writer() { sqlite3_close(db); }
    void exec(const char *sql) {
        require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "Synthetic write failed");
    }
    void truncate() {
        int log = -1, checkpointed = -1;
        require(sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, &log, &checkpointed) == SQLITE_OK,
                "Persistent status reader blocked WAL checkpoint");
        require(log == 0 && checkpointed == 0, "WAL checkpoint did not truncate");
    }
};

struct SharedMemory {
    qint64 modifiedNs = 0;
    QByteArray bytes;
};

SharedMemory sharedMemory(const QString &path) {
    struct stat value {};
    require(::stat(path.toUtf8().constData(), &value) == 0, "Missing WAL shared-memory sidecar");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot inspect synthetic WAL shared memory");
    return {qint64(value.st_mtim.tv_sec) * 1000000000 + value.st_mtim.tv_nsec, file.readAll()};
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create synthetic directory");
        replay::RecorderOptions options;
        options.directory = temporary.filePath("history");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            QImage image(640, 360, QImage::Format_RGBA8888);
            image.fill(Qt::white);
            recorder.addFrame(image, 1000);
            recorder.finish();
        }
        const QString dbPath = options.directory + "/index.sqlite";
        const QString walPath = dbPath + "-wal", shmPath = dbPath + "-shm";
        {
            Writer writer(dbPath);
            writer.exec("BEGIN; UPDATE frames SET text='synthetic completed history',ocr_state='ready';"
                        "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames;"
                        "INSERT INTO frame_ocr_geometry(frame_id,lines_json) VALUES(1,'[[10,20,300,20,\"synthetic completed history\"]]');"
                        "COMMIT;");
            writer.truncate();
        }

        // Establish the real failure shape: all writers have closed; a fresh
        // read-only connection creates an empty WAL and 32 KiB shared index.
        require(replay::indexingStatus(options.directory).value("ready").toInt() == 1, "Seed is not fully indexed");
        require(QFileInfo(walPath).exists() && QFileInfo(walPath).size() == 0, "Expected empty WAL fixture");
        const auto first = sharedMemory(shmPath);
        require(first.bytes.size() == 32768, "Expected one WAL shared-memory page");
        QThread::msleep(20);
        replay::indexingStatus(options.directory);
        const auto reopened = sharedMemory(shmPath);
        require(reopened.modifiedNs != first.modifiedNs,
                "Fixture did not expose read-only reconnect shared-memory churn");

        replay::IndexStatusReader reader(options.directory);
        require(reader.status().value("ready").toInt() == 1, "Persistent reader lost ready history");
        const auto before = sharedMemory(shmPath);
        for (int poll = 0; poll < 3; ++poll) {
            QThread::msleep(20);
            const auto status = reader.status();
            require(status.value("pending").toInt() == 0 && status.value("ready").toInt() == 1,
                    "Idle status changed without a writer");
        }
        const auto after = sharedMemory(shmPath);
        require(after.modifiedNs == before.modifiedNs && after.bytes == before.bytes,
                "Persistent idle status polls rewrote WAL shared memory");

        // The connection must not pin a snapshot or retain a read transaction.
        // Commit two separate writers' changes, observe each, and truncate WAL
        // with the status reader still alive after each poll.
        for (int observations = 2; observations <= 3; ++observations) {
            Writer writer(dbPath);
            writer.exec("BEGIN IMMEDIATE; UPDATE frames SET observation_count=observation_count+1;"
                        "INSERT INTO observations(timestamp_ms,frame_id) VALUES(2000,1); COMMIT;");
            require(QFileInfo(walPath).size() > 0, "Writer did not populate WAL");
            require(reader.status().value("coverage_total_observations").toInt() == observations,
                    "Persistent reader returned a stale snapshot");
            writer.truncate();
            require(QFileInfo(walPath).size() == 0, "WAL remained populated after checkpoint");
        }
        require(reader.status().value("coverage_indexed_observations").toInt() == 3,
                "Checkpoint changed indexed observation coverage");
        const auto checkpointed = sharedMemory(shmPath);
        QThread::msleep(20);
        reader.status();
        const auto repolled = sharedMemory(shmPath);
        require(checkpointed.modifiedNs == repolled.modifiedNs && checkpointed.bytes == repolled.bytes,
                "Idle polling resumed shared-memory churn after writer checkpoint");
        std::cout << "PASS empty-WAL reconnect reproduction, stable persistent status polls, fresh writer commits and unblocked checkpoints\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
