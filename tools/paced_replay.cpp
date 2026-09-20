// Offline timing experiment: retained originals only, never connects to a display.
#include "recorder.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <sys/resource.h>
#include <sys/stat.h>
#include <webp/decode.h>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
double cpu(const rusage &usage) {
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
           usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}
double option(const QCommandLineParser &parser, const char *key, double lo, double hi) {
    bool ok = false;
    const double value = parser.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(value) || value < lo || value > hi)
        throw std::runtime_error("Invalid experiment option");
    return value;
}
QImage originalImage(const QString &directory, const replay::FrameRecord &frame) {
    const auto base = QFileInfo(directory).canonicalFilePath();
    const auto path = QFileInfo(QDir(directory).filePath(frame.originalPath)).canonicalFilePath();
    if (base.isEmpty() || path.isEmpty() || !path.startsWith(base + '/') ||
        frame.width <= 0 || frame.height <= 0 || qint64(frame.width) * frame.height > 32LL * 1024 * 1024)
        throw std::runtime_error("Retained original path or dimensions are invalid");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 128LL * 1024 * 1024)
        throw std::runtime_error("Retained original cannot be read within its size bound");
    const auto bytes = file.read(128LL * 1024 * 1024 + 1);
    if (bytes.size() > 128LL * 1024 * 1024 || file.error() != QFile::NoError)
        throw std::runtime_error("Retained original exceeded its read bound");
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(), &features) != VP8_STATUS_OK ||
        features.format != 2 || features.width != frame.width || features.height != frame.height)
        throw std::runtime_error("Retained original is not the indexed lossless WebP image");
    QImage image(frame.width, frame.height, QImage::Format_RGBA8888);
    if (image.isNull() || !WebPDecodeRGBAInto(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(),
                                            image.bits(), image.sizeInBytes(), image.bytesPerLine()))
        throw std::runtime_error("Retained original decoding failed");
    return image;
}
struct Worker {
    QProcess process;
    QByteArray output;
    bool forced = false;
    ~Worker() { shutdown(); }
    void collect() {
        output += process.readAllStandardOutput();
        if (output.size() > 1024 * 1024) output = output.right(1024 * 1024);
        process.readAllStandardError(); // Private source text never enters this numeric report.
    }
    bool running() {
        process.waitForFinished(0);
        collect();
        return process.state() != QProcess::NotRunning;
    }
    void shutdown() {
        if (!running()) return;
        process.terminate();
        if (!process.waitForFinished(3000)) { forced = true; process.kill(); process.waitForFinished(1000); }
        collect();
    }
};
}

int main(int argc, char **argv) {
    umask(0077);
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {"source-dir", "Read-only source dataset with retained lossless originals.", "path"},
        {"dir", "New private result dataset.", "path"},
        {"binary", "Replay binary used for the independent index worker.", "path", "build/replay"},
        {"frames", "Finite replay observations.", "count", "24"},
        {"interval", "Seconds between observations.", "seconds", "5"},
        {"drain", "Bounded indexing catch-up after capture.", "seconds", "20"},
        {"ocr-cpu-percent", "Cooperative recognition allowance.", "percent", "10"},
        {"ocr-data-path", "Optional isolated model directory.", "path"},
        {"ocr-max-height", "Optional maximum OCR input height.", "pixels", "0"},
        {"codec", "Archive codec.", "name", "h264-vaapi"},
        {"pending-frames", "Bounded source count.", "count", "8"},
        {"pending-mib", "Bounded source bytes in MiB.", "mib", "64"}
    });
    parser.process(app);
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::signal(SIGPIPE, SIG_IGN);
    try {
        const auto source = QDir(parser.value("source-dir")).absolutePath();
        const auto directory = QDir(parser.value("dir")).absolutePath();
        if (!parser.isSet("source-dir") || !parser.isSet("dir") || QFileInfo::exists(directory))
            throw std::runtime_error("Provide existing source and new result directory");
        const double requested = option(parser, "frames", 1, 60);
        if (requested != std::floor(requested)) throw std::runtime_error("Frames must be integral");
        const int frameCount = int(requested);
        const double interval = option(parser, "interval", .25, 60);
        if (frameCount * interval > 600) throw std::runtime_error("Experiment exceeds ten minutes");
        const double drainSeconds = option(parser, "drain", 0, 60);
        const auto sourceFrames = replay::listFrames(source, 1000);
        QVector<replay::FrameRecord> originals;
        QJsonArray sourceIds;
        for (const auto &frame : sourceFrames)
            if (!frame.originalPath.isEmpty() && QFileInfo::exists(QDir(source).filePath(frame.originalPath))) {
                originals.append(frame);
                sourceIds.append(frame.id);
            }
        if (originals.isEmpty()) throw std::runtime_error("No retained lossless sources");
        const auto binary = QFileInfo(parser.value("binary")).absoluteFilePath();
        if (!QFileInfo(binary).isExecutable()) throw std::runtime_error("Replay binary unavailable");

        replay::RecorderOptions options;
        options.directory = directory;
        options.codec = parser.value("codec");
        options.deferredOcr = true;
        options.ocrMode = "incremental";
        options.ocrCpuPercent = option(parser, "ocr-cpu-percent", 1, 100);
        options.ocrMaxWallMs = 60000;
        options.ocrDataPath = parser.value("ocr-data-path");
        if (!options.ocrDataPath.isEmpty()) options.ocrDataPath = QDir(options.ocrDataPath).absolutePath();
        const auto height = option(parser, "ocr-max-height", 0, 8192);
        const auto capacity = option(parser, "pending-frames", 1, 256);
        if (height != std::floor(height) || capacity != std::floor(capacity))
            throw std::runtime_error("Height and capacity must be integral");
        options.ocrMaxHeight = int(height);
        options.maxPendingFrames = int(capacity);
        options.maxPendingBytes = quint64(option(parser, "pending-mib", 1, 256) * 1024 * 1024);
        options.maxDiskBytes = 512ULL * 1024 * 1024;
        options.intervalSeconds = interval;
        options.stopRequested = [] { return bool(stopped); };
        if (getpriority(PRIO_PROCESS, 0) < 10 && setpriority(PRIO_PROCESS, 0, 10) != 0)
            throw std::runtime_error("Cannot set experiment scheduling priority");
        replay::Recorder recorder(options);
        Worker worker;
        QStringList arguments{"index", "--dir", directory, "--follow", "--parent-pid",
            QString::number(QCoreApplication::applicationPid()), "--ocr-mode", options.ocrMode,
            "--ocr-cpu-percent", QString::number(options.ocrCpuPercent), "--ocr-max-wall-ms", "60000",
            "--ocr-max-height", QString::number(options.ocrMaxHeight)};
        if (!options.ocrDataPath.isEmpty()) arguments << "--ocr-data-path" << options.ocrDataPath;
        worker.process.start(binary, arguments);
        if (!worker.process.waitForStarted(3000)) throw std::runtime_error("Indexer did not start");
        rusage initial{}, initialChildren{};
        getrusage(RUSAGE_SELF, &initial); getrusage(RUSAGE_CHILDREN, &initialChildren);
        QElapsedTimer elapsed; elapsed.start();
        QJsonArray observations;
        int admitted = 0, rejected = 0, missed = 0;
        bool workerStoppedEarly = false;
        const qint64 epoch = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < frameCount && !stopped; ++i) {
            const qint64 target = qint64(i * interval * 1000);
            while (!stopped && elapsed.elapsed() < target) {
                worker.collect();
                QThread::msleep(10);
            }
            if (stopped) break;
            if (elapsed.elapsed() >= target + qint64(interval * 1000)) { ++missed; continue; }
            if (!worker.running()) { workerStoppedEarly = true; break; }
            const auto &sourceFrame = originals[i % originals.size()];
            const auto image = originalImage(source, sourceFrame);
            const auto timestamp = epoch + elapsed.elapsed();
            const auto result = recorder.addFrame(image, timestamp);
            admitted += result.stored || result.duplicate;
            rejected += result.backlogFull;
            auto sample = replay::indexingStatus(directory);
            sample["elapsed_seconds"] = elapsed.elapsed() / 1000.0;
            sample["scheduled_elapsed_seconds"] = target / 1000.0;
            sample["observation_timestamp_ms"] = timestamp;
            sample["source_frame_id"] = sourceFrame.id;
            sample["arrival_index"] = i;
            sample["admitted"] = result.stored || result.duplicate;
            sample["backlog_full"] = result.backlogFull;
            observations.append(sample);
        }
        while (!stopped && !workerStoppedEarly && elapsed.elapsed() < qint64(frameCount * interval * 1000)) {
            if (!worker.running()) { workerStoppedEarly = true; break; }
            QThread::msleep(10);
        }
        const auto atEnd = replay::indexingStatus(directory);
        const double captureSeconds = elapsed.elapsed() / 1000.0;
        QElapsedTimer finalization; finalization.start();
        recorder.finish();
        const double finalizationSeconds = finalization.elapsed() / 1000.0;
        QElapsedTimer drain; drain.start();
        while (!stopped && drain.elapsed() < drainSeconds * 1000 &&
               replay::indexingStatus(directory)["pending"].toInteger() > 0) {
            if (!worker.running()) break;
            QThread::msleep(50);
        }
        const double drainElapsedSeconds = drain.elapsed() / 1000.0;
        worker.shutdown();
        const auto workerDocument = QJsonDocument::fromJson(worker.output);
        const auto workerReport = workerDocument.object();
        rusage self{}, children{};
        getrusage(RUSAGE_SELF, &self); getrusage(RUSAGE_CHILDREN, &children);
        auto report = recorder.statsJSON();
        report["source_kind"] = "offline-retained-lossless-images";
        report["source_image_count"] = originals.size();
        report["source_sequence_frame_ids"] = sourceIds;
        report["requested_observations"] = frameCount;
        report["accepted_observations"] = admitted;
        report["backlog_skipped_observations"] = rejected;
        report["missed_schedule_slots"] = missed;
        report["unattempted_observations"] = frameCount - admitted - rejected - missed;
        report["capture_seconds"] = captureSeconds;
        report["archive_finalization_seconds"] = finalizationSeconds;
        report["drain_seconds"] = drainElapsedSeconds;
        report["elapsed_seconds"] = elapsed.elapsed() / 1000.0;
        report["self_cpu_seconds"] = cpu(self) - cpu(initial);
        report["children_cpu_seconds"] = cpu(children) - cpu(initialChildren);
        report["indexing_at_capture_end"] = atEnd;
        report["indexing"] = replay::indexingStatus(directory);
        report["worker"] = workerReport;
        report["worker_exit_code"] = worker.process.exitCode();
        report["worker_normal_exit"] = worker.process.exitStatus() == QProcess::NormalExit;
        report["worker_forced_stop"] = worker.forced;
        report["worker_result_available"] = workerDocument.isObject();
        report["worker_stopped_early"] = workerStoppedEarly;
        report["observations_over_time"] = observations;
        report["interrupted"] = bool(stopped);
        report["limits"] = "Offline replay omits compositor/capture buffers and foreground measurement. Retained originals cycle oldest-first, including a last-to-first scene boundary; this is not a new work session. Producer wall/CPU counters start after startup; child CPU covers complete reaped worker/encoder lifetimes. Capture includes the final scheduled interval; archive finalization and bounded drain follow separately. Queue lag is oldest-pending age, not per-job completion latency.";
        const auto data = QJsonDocument(report).toJson(QJsonDocument::Indented);
        QSaveFile output(QDir(directory).filePath("paced-report.json"));
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
            throw std::runtime_error("Cannot save numeric paced report");
        std::fwrite(data.constData(), 1, data.size(), stdout);
        if (stopped) return 130;
        if (workerStoppedEarly || worker.forced || worker.process.exitStatus() != QProcess::NormalExit ||
            !workerDocument.isObject()) return 1;
        return worker.process.exitCode();
    } catch (const std::exception &) {
        std::fprintf(stderr, "Offline paced experiment failed; no desktop capture was started.\n");
        return 1;
    }
}
