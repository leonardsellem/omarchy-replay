// Finite offline experiment. No native activity sampler or display connection.
#include "fixture.h"
#include "index_scheduler.h"
#include "recorder.h"

#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <memory>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <webp/decode.h>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
qint64 monotonicMs() {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) throw std::runtime_error("Monotonic clock unavailable");
    return value.tv_sec * 1000LL + value.tv_nsec / 1000000;
}
double cpuSeconds(int who) {
    rusage usage{};
    if (getrusage(who, &usage) != 0) throw std::runtime_error("CPU accounting unavailable");
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}
int integer(const QCommandLineParser& parser, const char* name, int low, int high) {
    bool ok = false;
    const int value = parser.value(name).toInt(&ok);
    if (!ok || value < low || value > high) throw std::runtime_error("Invalid integral experiment option");
    return value;
}
void saveJson(const QString& path, const QJsonObject& object) {
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw std::runtime_error("Could not save experiment receipt");
}
QJsonObject readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024) return {};
    return QJsonDocument::fromJson(bytes).object();
}
void printJson(const QJsonObject& object) {
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stdout);
    std::fflush(stdout);
}
// Core diagnostics include paths and descriptive strings. Only numeric values
// and their names enter this report; never text, errors or source fingerprints.
QJsonObject numeric(const QJsonObject& object) {
    QJsonObject result;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it->isDouble()) result[it.key()] = *it;
        else if (it->isBool()) result[it.key()] = int(it->toBool());
        else if (it->isObject()) result[it.key()] = numeric(it->toObject());
    }
    return result;
}

struct Profile {
    qint64 duration = 120000, interval = 5000, idleStart = 30000, idleEnd = 90000;
    qint64 priorityAt = 20000, restartAt = 45000, drain = 60000;
    bool alwaysActive = false;
    explicit Profile(bool quick, bool active) : alwaysActive(active) {
        if (quick) {
            duration = 12000; interval = 500; idleStart = 3000; idleEnd = 9000;
            priorityAt = 2000; restartAt = 4500; drain = 10000;
        }
    }
    bool idle(qint64 elapsed) const {
        return !alwaysActive && ((elapsed >= idleStart && elapsed < idleEnd) || elapsed >= duration);
    }
    int phase(qint64 elapsed) const {
        if (alwaysActive) return elapsed < duration ? 0 : 3;
        return elapsed < idleStart ? 0 : elapsed < idleEnd ? 1 : elapsed < duration ? 2 : 3;
    }
};

QByteArray originalBytes(const QString& directory, const replay::FrameRecord& frame) {
    const auto relative = frame.originalPath.isEmpty() && frame.codec == "webp" ? frame.path : frame.originalPath;
    const auto base = QFileInfo(directory).canonicalFilePath();
    const auto path = QFileInfo(QDir(directory).filePath(relative)).canonicalFilePath();
    if (relative.isEmpty() || base.isEmpty() || path.isEmpty() || !path.startsWith(base + '/') ||
        frame.width <= 0 || frame.height <= 0 || qint64(frame.width) * frame.height > 32LL * 1024 * 1024)
        throw std::runtime_error("Retained original path or dimensions invalid");
    QFile file(path);
    constexpr qint64 limit = 128LL * 1024 * 1024;
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > limit)
        throw std::runtime_error("Retained original unavailable within size bound");
    const auto data = file.read(limit + 1);
    if (data.size() > limit || file.error() != QFileDevice::NoError)
        throw std::runtime_error("Retained original read failed");
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(reinterpret_cast<const uint8_t*>(data.constData()), data.size(), &features) != VP8_STATUS_OK ||
        features.format != 2 || features.width != frame.width || features.height != frame.height)
        throw std::runtime_error("Retained original is not indexed lossless WebP");
    return data;
}
QImage originalImage(const QString& directory, const replay::FrameRecord& frame) {
    const auto data = originalBytes(directory, frame);
    QImage image(frame.width, frame.height, QImage::Format_RGBA8888);
    if (image.isNull() || !WebPDecodeRGBAInto(reinterpret_cast<const uint8_t*>(data.constData()), data.size(),
                                            image.bits(), image.sizeInBytes(), image.bytesPerLine()))
        throw std::runtime_error("Retained original decoding failed");
    return image;
}

// Only used while the old worker is stopped, and once by the replacement before
// it starts indexing. Hashes stay in private temporary receipts, not the report.
QJsonObject pendingFingerprints(const QString& directory) {
    QJsonObject hashes;
    for (const auto& frame : replay::listFrames(directory, 1000)) {
        if (frame.ocrState != "pending") continue;
        hashes[QString::number(frame.id)] = QString::fromLatin1(
            QCryptographicHash::hash(originalBytes(directory, frame), QCryptographicHash::Sha256).toHex());
    }
    return hashes;
}

struct Worker {
    QProcess process;
    QByteArray output;
    int generation = 0;
    qint64 epoch = 0, pid = 0;
    bool forced = false;
    ~Worker() { shutdown(); }
    void collect() {
        output += process.readAllStandardOutput();
        if (output.size() > 1024 * 1024) output = output.right(1024 * 1024);
        process.readAllStandardError();
    }
    bool running() {
        process.waitForFinished(0);
        collect();
        return process.state() != QProcess::NotRunning;
    }
    void shutdown() {
        if (!running()) return;
        process.terminate();
        if (!process.waitForFinished(5000)) { forced = true; process.kill(); process.waitForFinished(1000); }
        collect();
    }
    QJsonObject receipt() {
        collect();
        const auto document = QJsonDocument::fromJson(output);
        return {{"generation", generation}, {"pid", pid}, {"epoch_monotonic_ms", epoch},
                {"exit_code", process.exitCode()}, {"normal_exit", int(process.exitStatus() == QProcess::NormalExit)},
                {"forced_stop", int(forced)}, {"result_available", int(document.isObject())},
                {"result", document.object()}};
    }
};

int runWorker(const QCommandLineParser& parser, const Profile& profile, const QString& directory) {
    const int parent = integer(parser, "parent-pid", 1, 1 << 30);
    const int generation = integer(parser, "generation", 0, 1);
    bool epochOk = false;
    const qint64 epoch = parser.value("epoch-ms").toLongLong(&epochOk);
    if (!epochOk || epoch <= 0 || !parser.isSet("receipt-dir")) throw std::runtime_error("Missing shared worker epoch");
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent)
        throw std::runtime_error("Worker parent is unavailable");
    const qint64 startedAt = monotonicMs();
    const double cpuStart = cpuSeconds(RUSAGE_SELF);
    auto elapsed = [&] { return monotonicMs() - epoch; };
    replay::SchedulerOptions policy;
    replay::IndexScheduler scheduler(policy, [&] {
        return replay::ActivitySnapshot{true, profile.idle(elapsed()), true, 0, true, 10};
    }, elapsed);
    QJsonArray transitions, jobs;
    qint64 providerCalls = 0, attempt = 0;
    bool inJob = false;
    double lastAllowance = -1;
    int lastPhase = -1;
    auto provider = [&](bool requested) {
        const double allowance = scheduler.cpuPercent(requested);
        ++providerCalls;
        const int phase = profile.phase(elapsed());
        if ((allowance != lastAllowance || phase != lastPhase) && transitions.size() < 128) {
            transitions.append(QJsonObject{{"elapsed_seconds", elapsed() / 1000.0}, {"phase", phase},
                {"cpu_percent", allowance}, {"requested", int(requested)}, {"inside_job", int(inJob)},
                {"attempt", attempt}, {"worker_cpu_seconds", cpuSeconds(RUSAGE_SELF) - cpuStart}});
            lastAllowance = allowance; lastPhase = phase;
        }
        return allowance;
    };
    replay::IndexerOptions options;
    options.directory = directory;
    options.ocrMode = parser.value("ocr-mode");
    options.ocrDataPath = parser.value("ocr-data-path");
    options.ocrMaxHeight = integer(parser, "ocr-max-height", 0, 8192);
    options.ocrCpuPercent = 10;
    options.ocrMaxWallMs = 60000;
    options.cpuPercentProvider = provider;
    options.stopRequested = [&] {
        return bool(stopped) || getppid() != parent || elapsed() > profile.duration + profile.drain + 90000;
    };
    replay::Indexer indexer(options);
    const auto fingerprints = pendingFingerprints(directory);
    const auto startStatus = numeric(replay::indexingStatus(directory));
    const double initialAllowance = provider(false);
    saveJson(QDir(parser.value("receipt-dir")).filePath(QString("worker-%1-start.json").arg(generation)),
        {{"generation", generation}, {"epoch_monotonic_ms", epoch}, {"elapsed_seconds", elapsed() / 1000.0},
         {"phase", profile.phase(elapsed())}, {"idle", int(profile.idle(elapsed()))},
         {"initial_cpu_percent", initialAllowance}, {"indexing", startStatus}, {"source_fingerprints", fingerprints}});
    bool canceled = false;
    while (!options.stopRequested()) {
        ++attempt;
        const qint64 jobStart = elapsed();
        inJob = true;
        const auto result = indexer.processNext();
        inJob = false;
        if ((result.processed || result.canceled) && jobs.size() < 128)
            jobs.append(QJsonObject{{"attempt", attempt}, {"frame_id", result.frameId},
                {"start_seconds", jobStart / 1000.0}, {"end_seconds", elapsed() / 1000.0},
                {"processed", int(result.processed)}, {"canceled", int(result.canceled)},
                {"ready", int(result.state == "ready")}, {"failed", int(result.state == "failed")},
                {"ocr_ms", result.ocrMs}});
        if (result.canceled) { canceled = true; break; }
        if (!result.processed) { provider(false); QThread::msleep(50); }
    }
    auto report = numeric(indexer.statsJSON());
    report["scheduler"] = numeric(scheduler.statsJSON());
    report["generation"] = generation;
    report["epoch_monotonic_ms"] = epoch;
    report["start_elapsed_seconds"] = (startedAt - epoch) / 1000.0;
    report["end_elapsed_seconds"] = elapsed() / 1000.0;
    report["provider_calls"] = providerCalls;
    report["policy_transitions"] = transitions;
    report["jobs"] = jobs;
    report["interrupted"] = int(bool(stopped));
    report["canceled"] = int(canceled);
    report["self_cpu_seconds"] = cpuSeconds(RUSAGE_SELF) - cpuStart;
    printJson(report);
    return canceled && !stopped ? 1 : 0;
}

int runProducer(const QCommandLineParser& parser, const Profile& profile, const QString& directory) {
    const bool quick = parser.isSet("quick");
    if (QFileInfo::exists(directory) || (!quick && !parser.isSet("source-dir")) || (quick && parser.isSet("source-dir")))
        throw std::runtime_error("Use a new directory and either quick synthetic mode or a source dataset");
    if (!QDir().mkpath(QFileInfo(directory).absolutePath())) throw std::runtime_error("Cannot create experiment parent");
    QTemporaryDir copies(QDir(QFileInfo(directory).absolutePath()).filePath("source-copy-XXXXXX"));
    if (!copies.isValid()) throw std::runtime_error("Cannot create private source copies");
    QVector<replay::FrameRecord> sources;
    QJsonArray sourceIds;
    const qint64 startupAt = monotonicMs();
    if (!quick) {
        const auto source = QDir(parser.value("source-dir")).absolutePath();
        qint64 copiedBytes = 0;
        for (const auto& frame : replay::listFrames(source, 1000)) {
            if (frame.originalPath.isEmpty() && frame.codec != "webp") continue;
            const auto data = originalBytes(source, frame);
            copiedBytes += data.size();
            if (copiedBytes > 128LL * 1024 * 1024) throw std::runtime_error("Private source copies exceed 128 MiB");
            auto copy = frame;
            copy.originalPath = QString("source-%1.webp").arg(sources.size());
            QFile file(QDir(copies.path()).filePath(copy.originalPath));
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(data) != data.size())
                throw std::runtime_error("Cannot copy retained original");
            file.close();
            originalImage(copies.path(), copy); // Validate decoding before the timed profile.
            sources.append(copy); sourceIds.append(frame.id);
            if (sources.size() == 8) break;
        }
        if (sources.size() != 8) throw std::runtime_error("Experiment requires eight retained lossless originals");
    }
    replay::RecorderOptions options;
    options.directory = directory;
    options.codec = parser.isSet("codec") ? parser.value("codec") : quick ? "webp" : "h264-vaapi";
    options.deferredOcr = true;
    options.archiveFirst = parser.isSet("archive-first");
    options.ocrMode = parser.value("ocr-mode");
    options.ocrDataPath = parser.value("ocr-data-path");
    options.ocrMaxHeight = integer(parser, "ocr-max-height", 0, 8192);
    options.ocrMaxWallMs = 60000;
    options.intervalSeconds = profile.interval / 1000.0;
    options.maxPendingFrames = 0;
    options.maxPendingBytes = 64ULL * 1024 * 1024;
    options.maxDiskBytes = 512ULL * 1024 * 1024;
    options.stopRequested = [] { return bool(stopped); };
    replay::Recorder recorder(options);
    const qint64 epoch = monotonicMs();
    const qint64 wallEpoch = QDateTime::currentMSecsSinceEpoch();
    const double selfStart = cpuSeconds(RUSAGE_SELF), childrenStart = cpuSeconds(RUSAGE_CHILDREN);
    auto elapsed = [&] { return monotonicMs() - epoch; };
    QJsonArray workers, observations, snapshots;
    const auto receiptDirectory = copies.path();
    auto startWorker = [&](int generation) {
        auto worker = std::make_unique<Worker>();
        worker->generation = generation; worker->epoch = epoch;
        QStringList args{"--worker", "--dir", directory, "--epoch-ms", QString::number(epoch),
            "--parent-pid", QString::number(QCoreApplication::applicationPid()), "--generation", QString::number(generation),
            "--receipt-dir", receiptDirectory, "--ocr-mode", options.ocrMode, "--profile", parser.value("profile"),
            "--ocr-max-height", QString::number(options.ocrMaxHeight)};
        if (quick) args << "--quick";
        if (!options.ocrDataPath.isEmpty()) args << "--ocr-data-path" << options.ocrDataPath;
        worker->process.start(QCoreApplication::applicationFilePath(), args);
        if (!worker->process.waitForStarted(3000)) throw std::runtime_error("Cannot start experiment worker");
        worker->pid = worker->process.processId();
        const auto receipt = QDir(receiptDirectory).filePath(QString("worker-%1-start.json").arg(generation));
        const auto deadline = monotonicMs() + 3000;
        while (!stopped && !QFileInfo::exists(receipt) && monotonicMs() < deadline && worker->running()) QThread::msleep(10);
        if (!QFileInfo::exists(receipt)) throw std::runtime_error("Worker did not acknowledge initialization");
        return worker;
    };
    auto worker = startWorker(0);
    int saved = 0, duplicates = 0, skipped = 0, missed = 0, offered = 0;
    int activeOrdinal = 0, lastSource = 0;
    bool prioritized = false, restarted = false, workerStoppedEarly = false;
    QJsonObject priorityReceipt, restartReceipt;
    qint64 nextSnapshot = 0;
    auto snapshot = [&](int event) {
        auto item = numeric(replay::indexingStatus(directory));
        item["elapsed_seconds"] = elapsed() / 1000.0;
        item["phase"] = profile.phase(elapsed()); item["event"] = event;
        item["worker_generation"] = worker->generation;
        item["saved"] = saved; item["duplicates"] = duplicates; item["skipped"] = skipped;
        snapshots.append(item);
    };
    while (!stopped && elapsed() < profile.duration) {
        if (!worker->running()) { workerStoppedEarly = true; break; }
        const qint64 now = elapsed();
        while (offered < 24 && now >= offered * profile.interval) {
            const int slot = offered++;
            const qint64 scheduled = slot * profile.interval;
            if (now >= scheduled + profile.interval) { ++missed; continue; }
            const bool frozen = !profile.alwaysActive && scheduled >= profile.idleStart && scheduled < profile.idleEnd;
            if (!frozen) lastSource = activeOrdinal++ % 8;
            static const int fixtureIndices[]{0, 2, 4, 5, 6, 7, 8, 9};
            const QImage image = quick ? replay::fixtureFrame(fixtureIndices[lastSource], QSize(960, 540))
                                       : originalImage(copies.path(), sources[lastSource]);
            const qint64 timestamp = wallEpoch + elapsed();
            const auto result = recorder.addFrame(image, timestamp);
            saved += result.stored; duplicates += result.duplicate; skipped += result.backlogFull;
            observations.append(QJsonObject{{"slot", slot}, {"scheduled_seconds", scheduled / 1000.0},
                {"elapsed_seconds", elapsed() / 1000.0}, {"timestamp_ms", timestamp},
                {"source_index", lastSource}, {"source_frame_id", quick ? 0 : sources[lastSource].id},
                {"phase", profile.phase(scheduled)}, {"frozen_input", int(frozen)},
                {"stored", int(result.stored)}, {"duplicate", int(result.duplicate)},
                {"backlog_full", int(result.backlogFull)}, {"frame_id", result.frameId}});
        }
        if (!prioritized && elapsed() >= profile.priorityAt) {
            prioritized = true;
            const auto frames = replay::listFrames(directory, 1000);
            qint64 selected = 0;
            for (auto it = frames.crbegin(); it != frames.crend(); ++it)
                if (it->ocrState == "pending") { selected = it->id; break; }
            const int count = selected ? replay::requestIndexing(directory, selected, 0) : 0;
            priorityReceipt = {{"elapsed_seconds", elapsed() / 1000.0}, {"frame_id", selected},
                {"requested_frames", count}, {"context_seconds", 0}, {"pending_candidate_available", int(selected != 0)}};
            snapshot(1);
        }
        if (!restarted && elapsed() >= profile.restartAt) {
            restarted = true;
            const double restartStart = elapsed() / 1000.0;
            snapshot(2);
            worker->shutdown();
            workers.append(worker->receipt());
            const auto before = pendingFingerprints(directory);
            const auto stoppedStatus = numeric(replay::indexingStatus(directory));
            if (worker->forced || worker->process.exitStatus() != QProcess::NormalExit || worker->process.exitCode() != 0)
                throw std::runtime_error("Planned graceful worker stop failed");
            worker = startWorker(1);
            const auto started = readJson(QDir(receiptDirectory).filePath("worker-1-start.json"));
            const auto after = started["source_fingerprints"].toObject();
            int matched = 0;
            for (auto it = before.begin(); it != before.end(); ++it)
                matched += after.contains(it.key()) && after[it.key()] == it.value();
            restartReceipt = {{"start_seconds", restartStart}, {"end_seconds", elapsed() / 1000.0},
                {"pending_before_restart", before.size()}, {"pending_at_worker_start", after.size()},
                {"unchanged_originals", matched}, {"all_originals_preserved", int(matched == before.size() && after.size() == before.size())},
                {"retention_check_nonempty", int(!before.isEmpty())}, {"worker_start", numeric(started)},
                {"indexing_while_stopped", stoppedStatus},
                {"same_epoch", int(started["epoch_monotonic_ms"].toInteger() == epoch)},
                {"restart_used_expected_allowance", int(started["initial_cpu_percent"].toDouble() == (profile.alwaysActive ? 10 : 40))},
                {"restart_remained_idle", int(started["idle"].toInt() == 1 && started["initial_cpu_percent"].toDouble() == 40)}};
            snapshot(3);
        }
        if (elapsed() >= nextSnapshot) { snapshot(0); nextSnapshot = elapsed() + 1000; }
        QThread::msleep(10);
    }
    snapshot(4);
    const auto captureEnd = numeric(replay::indexingStatus(directory));
    const double captureSeconds = elapsed() / 1000.0;
    const auto finalizeStart = monotonicMs();
    recorder.finish();
    const double finalizeSeconds = (monotonicMs() - finalizeStart) / 1000.0;
    const auto drainStart = monotonicMs();
    while (!stopped && !workerStoppedEarly && monotonicMs() - drainStart < profile.drain &&
           replay::indexingStatus(directory)["pending"].toInteger() > 0) {
        if (!worker->running()) { workerStoppedEarly = true; break; }
        if (elapsed() >= nextSnapshot) { snapshot(0); nextSnapshot = elapsed() + 1000; }
        QThread::msleep(50);
    }
    const double drainSeconds = (monotonicMs() - drainStart) / 1000.0;
    worker->shutdown(); workers.append(worker->receipt()); snapshot(5);
    const auto finalStatus = numeric(replay::indexingStatus(directory));
    bool workersHealthy = !workerStoppedEarly;
    QJsonObject totals;
    for (const auto& entry : workers) {
        const auto receipt = entry.toObject();
        workersHealthy = workersHealthy && receipt["normal_exit"].toInt() && receipt["result_available"].toInt()
            && !receipt["forced_stop"].toInt() && receipt["exit_code"].toInt() == 0;
        const auto result = receipt["result"].toObject();
        for (const auto* key : {"processed", "priority_jobs", "oldest_jobs", "obsolete_jobs", "ocr_discontinuity_resets",
                              "failed_jobs", "canceled_jobs", "ocr_full_frames", "ocr_partial_frames", "ocr_cpu_ms",
                              "ocr_wall_ms", "ocr_budget_sleep_ms", "self_cpu_seconds"})
            totals[key] = totals[key].toDouble() + result[key].toDouble();
    }
    const bool healthy = workersHealthy && restarted && restartReceipt["same_epoch"].toInt() &&
        restartReceipt["all_originals_preserved"].toInt() && restartReceipt["restart_used_expected_allowance"].toInt() &&
        finalStatus["failed"].toInteger() == 0;
    QJsonObject report{{"report_version", 1}, {"completed", int(!stopped && !workerStoppedEarly && offered == 24)},
        {"healthy", int(healthy)}, {"quick", int(quick)}, {"source_image_count", 8}, {"source_sequence_frame_ids", sourceIds},
        {"requested_observations", 24}, {"offered_slots", offered}, {"saved_observations", saved},
        {"duplicate_observations", duplicates}, {"accepted_observations", saved + duplicates},
        {"backlog_skipped_observations", skipped}, {"missed_schedule_slots", missed}, {"unattempted_observations", 24 - offered},
        {"epoch_monotonic_ms", epoch}, {"epoch_timestamp_ms", wallEpoch},
        {"source_preparation_seconds", (epoch - startupAt) / 1000.0}, {"capture_seconds", captureSeconds},
        {"archive_finalization_seconds", finalizeSeconds}, {"drain_seconds", drainSeconds},
        {"elapsed_seconds", elapsed() / 1000.0}, {"self_cpu_seconds", cpuSeconds(RUSAGE_SELF) - selfStart},
        {"children_cpu_seconds", cpuSeconds(RUSAGE_CHILDREN) - childrenStart},
        {"interrupted", int(bool(stopped))}, {"worker_stopped_early", int(workerStoppedEarly)},
        {"recorder", numeric(recorder.statsJSON())}, {"workers", workers}, {"worker_totals", totals},
        {"priority_request", priorityReceipt}, {"restart", restartReceipt}, {"observations", observations},
        {"queue_snapshots", snapshots}, {"indexing_at_capture_end", captureEnd}, {"indexing", finalStatus},
        {"profile", QJsonObject{{"always_active", int(profile.alwaysActive)}, {"duration_ms", profile.duration}, {"interval_ms", profile.interval},
            {"idle_start_ms", profile.idleStart}, {"idle_end_ms", profile.idleEnd}, {"priority_at_ms", profile.priorityAt},
            {"restart_at_ms", profile.restartAt}, {"maximum_drain_ms", profile.drain},
            {"active_cpu_percent", 10}, {"idle_cpu_percent", 40}, {"requested_cpu_percent", 30}}}};
    saveJson(QDir(directory).filePath("scheduling-report.json"), report);
    printJson(report);
    return stopped ? 130 : healthy ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    umask(0077);
    // Fixture rendering needs fonts but never a desktop connection, in either role.
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QPA_PLATFORMTHEME", ""); qputenv("QT_STYLE_OVERRIDE", "Fusion");
    qunsetenv("DISPLAY"); qunsetenv("WAYLAND_DISPLAY"); qunsetenv("WAYLAND_SOCKET");
    QGuiApplication app(argc, argv);
    std::signal(SIGINT, stop); std::signal(SIGTERM, stop); std::signal(SIGPIPE, SIG_IGN);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"source-dir", "Read-only dataset with eight retained lossless originals.", "path"},
        {"dir", "New private destination dataset.", "path"}, {"quick", "12-second synthetic smoke profile."},
        {"codec", "Archive codec; defaults to h264-vaapi (webp for quick).", "name"},
        {"archive-first", "Retain WebP independently of OCR backlog for this bounded experiment."},
        {"profile", "Activity profile: adaptive or always-active (including drain).", "name", "adaptive"},
        {"ocr-mode", "Pass-through recognition mode: full, incremental or regions.", "name", "incremental"},
        {"ocr-data-path", "Optional isolated model directory.", "path"},
        {"ocr-max-height", "Optional OCR-only resize height.", "pixels", "0"},
        {"worker", "Internal worker role."}, {"parent-pid", "Internal parent PID.", "pid"},
        {"epoch-ms", "Internal shared monotonic epoch.", "milliseconds"},
        {"generation", "Internal worker generation.", "number"}, {"receipt-dir", "Internal receipt directory.", "path"}});
    parser.process(app);
    try {
        if (!parser.isSet("dir")) throw std::runtime_error("A private destination is required");
        if (!QStringList{"full", "incremental", "regions"}.contains(parser.value("ocr-mode")))
            throw std::runtime_error("Unknown OCR experiment mode");
        if (!QStringList{"adaptive", "always-active"}.contains(parser.value("profile")))
            throw std::runtime_error("Unknown activity profile");
        const int height = integer(parser, "ocr-max-height", 0, 8192);
        if (height && height < 256) throw std::runtime_error("OCR maximum height must be zero or at least 256");
        if (getpriority(PRIO_PROCESS, 0) < 10 && setpriority(PRIO_PROCESS, 0, 10) != 0)
            throw std::runtime_error("Cannot set experiment scheduling priority");
        const Profile profile(parser.isSet("quick"), parser.value("profile") == "always-active");
        const auto directory = QDir(parser.value("dir")).absolutePath();
        return parser.isSet("worker") ? runWorker(parser, profile, directory) : runProducer(parser, profile, directory);
    } catch (const std::exception&) {
        printJson({{"completed", 0}, {"healthy", 0}, {"interrupted", int(bool(stopped))}, {"exception", 1}});
        std::fprintf(stderr, "Offline scheduling experiment failed; no desktop connection was made.\n");
        return stopped ? 130 : 1;
    }
}
