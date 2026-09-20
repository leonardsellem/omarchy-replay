#include "recorder.h"

#include <QApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <limits>
#include <sqlite3.h>
#include <stdexcept>

namespace {
constexpr quint64 MiB = 1024 * 1024;
constexpr int Frames = 12;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Callback>
void requireError(Callback callback, const char *message) {
    bool rejected = false;
    try { callback(); } catch (const std::exception &) { rejected = true; }
    require(rejected, message);
}

quint64 bytesBelow(const QString &directory) {
    quint64 bytes = 0;
    QDirIterator files(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) { files.next(); bytes += files.fileInfo().size(); }
    return bytes;
}

int mediaCount(const QString &directory) {
    return QDir(QDir(directory).filePath("media")).entryList(QDir::Files | QDir::Hidden).size();
}

QImage sourceFrame(int invoice) {
    QImage image(960, 540, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font("DejaVu Sans"); font.setPixelSize(32); painter.setFont(font);
    painter.drawText(48, 90, "SYNTHETIC ARCHIVE TEST");
    painter.drawText(48, 185, "Patrick / workshop invoice");
    painter.drawText(48, 280, QString("Invoice XYZ-%1").arg(invoice));
    painter.drawText(48, 375, "Original pixels remain available before OCR.");
    return image;
}

replay::RecorderOptions archiveOptions(const QString &directory) {
    replay::RecorderOptions options;
    options.directory = directory;
    options.deferredOcr = true;
    options.archiveFirst = true;
    options.minFreeBytes = 0;
    options.maxPendingFrames = 8;
    options.maxPendingBytes = 1024;
    return options;
}

int drain(const QString &directory) {
    replay::IndexerOptions options; options.directory = directory;
    replay::Indexer worker(options);
    for (int count = 0; count <= Frames; ++count) {
        const auto result = worker.processNext();
        if (!result.processed && result.state == "idle") return count;
        require(result.processed && result.state == "ready", "Lossless archive job did not become ready");
    }
    throw std::runtime_error("Archive worker did not finish its finite pending set");
}

void validatePolicy(const QString &root) {
    for (int variant = 0; variant < 3; ++variant) {
        auto options = archiveOptions(root + QString("/invalid-%1").arg(variant));
        if (variant == 0) options.codec = "h264";
        if (variant == 1) options.deferredOcr = false;
        if (variant == 2) options.ocr = false;
        requireError([&] { replay::Recorder recorder(options); }, "Incompatible archive-first policy was accepted");
        require(!QFileInfo::exists(options.directory), "Invalid policy created a partial dataset");
    }
    auto options = archiveOptions(root + "/minimum-free-space");
    options.minFreeBytes = std::numeric_limits<quint64>::max();
    requireError([&] { replay::Recorder recorder(options); }, "Archive-first bypassed the free-space reserve");
    std::cout << "PASS archive-first is explicitly lossless/deferred and retains free-space checks\n";
}

void retainWithoutWorkerAndResume(const QString &root) {
    const auto options = archiveOptions(root + "/archive");
    {
        replay::Recorder recorder(options);
        for (int i = 0; i < Frames; ++i) {
            const QImage image = sourceFrame(1042 + i);
            const auto result = recorder.addFrame(image, 1000 + i * 2000);
            require(result.stored && !result.backlogFull, "OCR backlog rejected canonical lossless history");
            require(replay::loadFrame(options.directory, result.frameId).convertToFormat(QImage::Format_RGBA8888) == image,
                    "Pending archive was unavailable or changed original pixels");
        }
        const auto duplicate = recorder.addFrame(sourceFrame(1042 + Frames - 1), 1000 + Frames * 2000);
        require(duplicate.duplicate, "Archive-first changed exact duplicate handling");
        const auto stats = recorder.statsJSON();
        const auto index = stats["indexing"].toObject();
        require(stats["archive_first"].toBool() && index["archive_first"].toBool(), "Archive policy was not reported");
        require(index["pending"].toInteger() == Frames && index["ready"].toInteger() == 0 &&
                index["source_bytes"].toDouble() > options.maxPendingBytes &&
                index["source_frames"].toInteger() > options.maxPendingFrames &&
                index["staged_bytes"].toInteger() == 0,
                "Paused-OCR proof did not exceed both source limits using only canonical media");
        require(stats["backlog_full"].toInteger() == 0 && !stats["ocr_initialized"].toBool(),
                "Archive capture performed OCR or reported false backlog rejections");
        require(replay::searchFrames(options.directory, "Patrick").isEmpty(), "Pending archive advertised unrecognized text");
        recorder.finish();
    }
    require(replay::listObservations(options.directory).size() == Frames + 1, "Archive-first dropped observation metadata");
    require(replay::indexingStatus(options.directory)["archive_first"].toBool(), "Archive policy did not survive reopen");
    {
        replay::IndexerOptions workerOptions; workerOptions.directory = options.directory;
        replay::Indexer worker(workerOptions);
        for (int i = 0; i < 3; ++i) require(worker.processNext().state == "ready", "First worker did not index its jobs");
    }
    require(replay::indexingStatus(options.directory)["pending"].toInteger() == Frames - 3, "Worker restart lost pending IDs");
    require(drain(options.directory) == Frames - 3, "Fresh worker did not finish every retained archive job");
    require(replay::searchFrames(options.directory, "Patrick").size() == Frames, "Resumed archive text did not become searchable");
    const auto ready = replay::listFrames(options.directory);
    for (int i = 0; i < ready.size(); ++i) {
        require(ready[i].ocrState == "ready" && ready[i].originalPath.isEmpty() && ready[i].archiveAvailable,
                "Indexed archive did not release only its OCR source reference");
        require(replay::loadFrame(options.directory, ready[i].id).convertToFormat(QImage::Format_RGBA8888) == sourceFrame(1042 + i),
                "Index cleanup removed or changed canonical lossless media");
    }
    require(mediaCount(options.directory) == Frames &&
            replay::indexingStatus(options.directory)["source_bytes"].toInteger() == 0,
            "Indexing duplicated archive files or retained source accounting");

    // Compare final media bytes for identical finite inputs. This is a codec
    // storage observation, not an OCR accuracy, CPU, or daily-retention claim.
    replay::RecorderOptions video;
    video.directory = root + "/video-comparison"; video.codec = "h264";
    video.ocr = false; video.minFreeBytes = 0;
    {
        replay::Recorder recorder(video);
        for (int i = 0; i < Frames; ++i) recorder.addFrame(sourceFrame(1042 + i), 1000 + i * 2000);
        recorder.finish();
    }
    const QJsonObject comparison{{"synthetic_frames", Frames}, {"width", 960}, {"height", 540},
        {"lossless_webp_media_bytes", double(bytesBelow(QDir(options.directory).filePath("media")))},
        {"lossy_h264_media_bytes", double(bytesBelow(QDir(video.directory).filePath("media")))}};
    std::cout << "STORAGE " << QJsonDocument(comparison).toJson(QJsonDocument::Compact).constData() << '\n';
    std::cout << "PASS paused-OCR archive beyond source caps, exact browsing, worker restart and preserved originals\n";
}

void randomize(QImage &image, quint32 &state) {
    for (int row = 0; row < image.height(); ++row) {
        auto *pixels = image.scanLine(row);
        for (int column = 0; column < image.width() * 4; ++column) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            pixels[column] = column % 4 == 3 ? 255 : state & 255;
        }
    }
}

void boundedStorage(const QString &root) {
    auto options = archiveOptions(root + "/full-storage");
    options.maxDiskBytes = 16 * MiB;
    options.maxPendingBytes = 64 * MiB; // Ignored OCR cap may exceed this archive's total budget.
    replay::Recorder recorder(options);
    QImage image(1024, 1024, QImage::Format_RGBA8888);
    quint32 random = 31987231;
    int accepted = 0;
    bool full = false;
    for (int i = 0; i < 16; ++i) {
        randomize(image, random);
        try {
            require(recorder.addFrame(image, i * 1000).stored, "Archive unexpectedly rejected high-entropy input");
            ++accepted;
        } catch (const std::exception &error) {
            require(QString::fromUtf8(error.what()).contains("dataset disk budget reached"), "Archive stopped for a reason other than its disk budget");
            full = true;
            break;
        }
    }
    require(full && accepted > 0 && bytesBelow(options.directory) <= options.maxDiskBytes, "Archive did not respect its finite total disk ceiling");
    require(recorder.statsJSON()["failed"].toBool() && recorder.statsJSON()["backlog_full"].toInteger() == 0,
            "Total storage exhaustion was confused with OCR backlog pressure");
    const auto frames = replay::listFrames(options.directory);
    require(frames.size() == accepted && mediaCount(options.directory) == accepted, "Storage exhaustion left extra media or lost accepted rows");
    random = 31987231;
    for (const auto &frame : frames) {
        randomize(image, random);
        require(replay::loadFrame(options.directory, frame.id).convertToFormat(QImage::Format_RGBA8888) == image,
                "Storage exhaustion damaged an accepted original");
    }
    const quint64 before = bytesBelow(options.directory);
    requireError([&] { recorder.addFrame(image, 999999); }, "Failed archive writer accepted another attempt");
    require(bytesBelow(options.directory) == before, "Retry after storage exhaustion accumulated orphan bytes");
    std::cout << "PASS finite archive storage exhaustion preserves accepted originals and stops further writes\n";
}

void failedPublication(const QString &root) {
    const auto options = archiveOptions(root + "/publication-error");
    replay::Recorder recorder(options);
    const auto accepted = recorder.addFrame(sourceFrame(1042), 1000);
    sqlite3 *database = nullptr;
    require(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database) == SQLITE_OK,
            "Cannot open synthetic archive for publication failure injection");
    const int result = sqlite3_exec(database,
        "CREATE TRIGGER reject_frame BEFORE INSERT ON frames BEGIN SELECT RAISE(ABORT,'synthetic publication failure'); END",
        nullptr, nullptr, nullptr);
    sqlite3_close(database);
    require(result == SQLITE_OK, "Could not install synthetic publication failure");
    requireError([&] { recorder.addFrame(sourceFrame(1043), 3000); }, "Synthetic row publication unexpectedly succeeded");
    require(mediaCount(options.directory) == 1 && replay::listFrames(options.directory).size() == 1 &&
            replay::listObservations(options.directory).size() == 1, "Failed publication left unreferenced media or an observation");
    require(replay::loadFrame(options.directory, accepted.frameId).convertToFormat(QImage::Format_RGBA8888) == sourceFrame(1042),
            "Publication cleanup removed a committed original");
    requireError([&] { recorder.addFrame(sourceFrame(1044), 5000); }, "Failed writer allowed accumulating repeated orphan files");
    require(mediaCount(options.directory) == 1, "Repeated failed publication accumulated media");
    std::cout << "PASS failed row publication removes only the uncommitted archive image\n";
}

int crashWriter(const QString &directory) {
    replay::Recorder recorder(archiveOptions(directory));
    for (int i = 0; i < 3; ++i) recorder.addFrame(sourceFrame(1042 + i), 1000 + i * 2000);
    std::cout << "ARCHIVE_COMMITTED\n" << std::flush;
    QThread::sleep(30); // Parent kills this process without destructors or finish().
    return 1;
}

void abruptExit(const QString &root) {
    const QString directory = root + "/crashed-writer";
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QCoreApplication::applicationFilePath(), {"--archive-crash-writer", directory});
    require(process.waitForStarted(5000), "Could not start isolated archive writer");
    QElapsedTimer deadline; deadline.start();
    QByteArray output;
    while (!output.contains("ARCHIVE_COMMITTED") && deadline.elapsed() < 10000 && process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        output += process.readAll();
    }
    require(output.contains("ARCHIVE_COMMITTED"), "Archive writer did not acknowledge durable commits");
    process.kill();
    require(process.waitForFinished(3000), "Synthetic archive writer did not stop on SIGKILL");
    const auto frames = replay::listFrames(directory);
    require(frames.size() == 3 && mediaCount(directory) == 3 && replay::listObservations(directory).size() == 3,
            "Abrupt writer exit lost committed history or left extra media");
    for (int i = 0; i < frames.size(); ++i)
        require(frames[i].ocrState == "pending" && replay::loadFrame(directory, frames[i].id).convertToFormat(QImage::Format_RGBA8888) == sourceFrame(1042 + i),
                "Committed pending originals did not survive abrupt writer exit");
    require(drain(directory) == 3 && replay::searchFrames(directory, "Patrick").size() == 3,
            "Fresh index worker could not resume abruptly stopped archive history");
    std::cout << "PASS SIGKILL preserves committed images and resumable OCR jobs\n";
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    try {
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--archive-crash-writer")
            return crashWriter(QString::fromLocal8Bit(argv[2]));
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create isolated archive test directory");
        validatePolicy(temporary.path());
        retainWithoutWorkerAndResume(temporary.path());
        boundedStorage(temporary.path());
        failedPublication(temporary.path());
        abruptExit(temporary.path());
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
