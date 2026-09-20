#include "recorder.h"
#include "ocr_fixtures.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <sqlite3.h>
#include <webp/decode.h>
#include <sys/resource.h>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition) { if (!condition) throw std::runtime_error("experiment validation failed"); }
void emitJson(const QJsonObject &object) {
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << '\n' << std::flush;
}
double cpuMs() {
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0 +
           usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
}

struct Job {
    QString suite;
    int index = 0;
    QSize size;
    QString sourcePath;
    QStringList truth;
};

// Physical font sizes are deliberate: unlike the older normalized fixtures,
// these stay 14/18/24 pixels on a 4K image. Truth is fixed before OCR is run.
QStringList denseTruth(int scene) {
    return {scene == 0 ? "KITE-1042" : "KITE-1043",
            scene < 2 ? "MESA-731" : "MESA-739",
            scene < 2 ? "POND-628" : "POND-629",
            "BOLT-482", "FERN-953", "LAKE-276"};
}
QImage denseFrame(int scene) {
    QImage image(QSize(3840, 2160), QImage::Format_RGBA8888);
    image.fill(QColor("#e8edf2"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const auto truth = denseTruth(scene);
    const QStringList prose{
        "delivery checklist review the quantities and attach the receipt",
        "workspace draft notes saved locally for the afternoon review",
        "sample build completed the next step is to inspect the result",
        "shipping schedule updated after the warehouse confirmation",
        "customer reference details remain available in the local record",
        "change request reviewed and prepared for the next release"};
    for (int column = 0; column < 3; ++column) {
        const int x = 32 + column * 1264;
        const bool dark = column == 1;
        painter.fillRect(QRect(x, 32, 1232, 2096), dark ? QColor("#17202a") : Qt::white);
        painter.setPen(dark ? QColor("#eef2f5") : QColor("#17202a"));
        const int pixels = std::array{14, 18, 24}[column];
        ocr_fixture::text(painter, x + 24, 72, "SYNTHETIC LOCAL OCR EXPERIMENT", 24);
        const int lineHeight = pixels + 12;
        const int rowCount = (2010 - 110) / lineHeight;
        for (int row = 0; row < rowCount; ++row) {
            const int documentRow = row + (scene == 2 ? 3 : 0);
            QString line = QString("%1  %2").arg(documentRow + 1, 3, 10, QChar('0'))
                               .arg(prose[(documentRow + column) % prose.size()]);
            if (row == 5) line = "Invoice reference " + truth[column];
            if (row == rowCount - 5) line = "Dispatch reference " + truth[column + 3];
            ocr_fixture::text(painter, x + 24, 110 + row * lineHeight, line, pixels, true);
        }
    }
    return image;
}

QVector<Job> makeJobs(const QString &source, const QString &sourceDirectory) {
    QVector<Job> jobs;
    if (source == "private") {
        sqlite3 *database = nullptr;
        require(sqlite3_open_v2(QDir(sourceDirectory).filePath("index.sqlite").toUtf8().constData(),
                                &database, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
        struct Close { sqlite3 *db; ~Close() { sqlite3_close(db); } } close{database};
        sqlite3_stmt *statement = nullptr;
        require(sqlite3_prepare_v2(database,
            "SELECT source_path,width,height FROM frames WHERE source_path<>'' ORDER BY timestamp_ms,id",
            -1, &statement, nullptr) == SQLITE_OK);
        struct Finalize { sqlite3_stmt *stmt; ~Finalize() { sqlite3_finalize(stmt); } } finalize{statement};
        const QString root = QFileInfo(sourceDirectory).canonicalFilePath() + '/';
        int status;
        while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
            const QString relative = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0)));
            const QString path = QFileInfo(QDir(sourceDirectory).filePath(relative)).canonicalFilePath();
            require(path.startsWith(root) && QFileInfo(path).isFile());
            const QSize size(sqlite3_column_int(statement, 1), sqlite3_column_int(statement, 2));
            require(size.width() > 0 && size.height() > 0 && qint64(size.width()) * size.height() <= 32LL * 1024 * 1024);
            jobs.append({"private", int(jobs.size()), size, path, {}});
        }
        require(status == SQLITE_DONE && jobs.size() == 8);
        return jobs;
    }
    for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160)}) {
        const QString suite = size.height() == 1080 ? "synthetic1080" : "synthetic4k";
        if (source != "synthetic" && source != suite) continue;
        for (int i = 0; i < ocr_fixture::count; ++i)
            jobs.append({suite, i, size, {}, ocr_fixture::visibleTerms(i)});
    }
    if (source == "synthetic" || source == "dense4k")
        for (int i = 0; i < 3; ++i) jobs.append({"dense4k", i, QSize(3840, 2160), {}, denseTruth(i)});
    require(!jobs.empty());
    return jobs;
}

QImage jobImage(const Job &job) {
    if (job.suite == "dense4k") return denseFrame(job.index);
    if (job.suite != "private") return ocr_fixture::frame(job.index, job.size);
    QFile file(job.sourcePath); require(file.open(QIODevice::ReadOnly));
    require(file.size() > 0 && file.size() <= 128LL * 1024 * 1024);
    const auto bytes = file.readAll();
    WebPBitstreamFeatures features{};
    require(WebPGetFeatures(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(), &features) == VP8_STATUS_OK);
    require(features.format == 2 && QSize(features.width, features.height) == job.size);
    QImage image(job.size, QImage::Format_RGBA8888);
    require(WebPDecodeRGBAInto(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(), image.bits(),
                              image.sizeInBytes(), image.bytesPerLine()) != nullptr);
    return image;
}

bool exactTerm(const QString &text, const QString &term) {
    const QRegularExpression expression("(?<![\\p{L}\\p{N}_])" + QRegularExpression::escape(term) + "(?![\\p{L}\\p{N}_])",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return expression.match(text).hasMatch();
}
QMap<QString, int> tokens(const QString &text) {
    QMap<QString, int> result;
    auto matches = QRegularExpression("[\\p{L}\\p{N}_]+", QRegularExpression::UseUnicodePropertiesOption)
                       .globalMatch(text.toCaseFolded());
    while (matches.hasNext()) ++result[matches.next().captured()];
    return result;
}
QJsonObject agreement(const QString &baseline, const QString &candidate) {
    const auto first = tokens(baseline), second = tokens(candidate);
    qint64 a = 0, b = 0, common = 0;
    for (auto it = first.begin(); it != first.end(); ++it) { a += it.value(); common += std::min(it.value(), second.value(it.key())); }
    for (int count : second) b += count;
    return {{"baseline_tokens", a}, {"candidate_tokens", b}, {"common_tokens", common},
            {"baseline_token_recall", a ? QJsonValue(double(common) / a) : QJsonValue()},
            {"candidate_token_precision", b ? QJsonValue(double(common) / b) : QJsonValue()}};
}
QJsonObject safeStats(const QJsonObject &input) {
    QJsonObject result;
    // Whitelist numbers and flags. Recorder paths and error strings never leave
    // this process, including when the corpus is private.
    for (auto it = input.begin(); it != input.end(); ++it)
        if (it.value().isDouble() || it.value().isBool()) result[it.key()] = it.value();
    return result;
}

} // namespace

int main(int argc, char **argv) {
    qputenv("OMP_THREAD_LIMIT", "1");
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"source", "synthetic, synthetic1080, synthetic4k, dense4k, or private", "source", "synthetic"},
                       {"source-dir", "Read-only private input dataset", "path"},
                       {"dataset-dir", "New temporary output dataset", "path"},
                       {"baseline-dir", "Temporary system/full-resolution baseline dataset", "path"},
                       {"mode", "OCR mode", "mode", "full"},
                       {"tessdata", "Model directory (empty uses system model)", "path"},
                       {"ocr-height", "Maximum OCR height; zero preserves resolution", "pixels", "0"}});
    parser.process(app);
    try {
        require(parser.positionalArguments().empty());
        bool validHeight = false;
        const int height = parser.value("ocr-height").toInt(&validHeight);
        require(validHeight && (height == 0 || (height >= 256 && height <= 8192)));
        const QString source = parser.value("source");
        const auto jobs = makeJobs(source, parser.value("source-dir"));
        replay::RecorderOptions options;
        options.directory = parser.value("dataset-dir");
        require(!options.directory.isEmpty() && !QFileInfo::exists(options.directory));
        options.ocrMode = parser.value("mode");
        require(options.ocrMode == "full" || options.ocrMode == "incremental" || options.ocrMode == "regions");
        options.ocrDataPath = parser.value("tessdata");
        options.ocrMaxHeight = height;
        options.ocrMaxWallMs = 60000;
        options.maxDiskBytes = 2ULL * 1024 * 1024 * 1024;
        replay::Recorder recorder(options);
        QJsonArray measurements;
        QVector<qint64> ids;
        for (int i = 0; i < jobs.size(); ++i) {
            emitJson({{"event", "job_begin"}, {"job", i}});
            const double decodeCpuStart = cpuMs();
            QElapsedTimer decodeTimer; decodeTimer.start();
            const QImage image = jobImage(jobs[i]);
            const double decodeWall = decodeTimer.nsecsElapsed() / 1e6, decodeCpu = cpuMs() - decodeCpuStart;
            const auto before = recorder.statsJSON();
            const double startCpu = cpuMs();
            QElapsedTimer timer; timer.start();
            const auto stored = recorder.addFrame(image, 1000 + i * 5000);
            const double wall = timer.nsecsElapsed() / 1e6, cpu = cpuMs() - startCpu;
            require(stored.stored && !stored.duplicate);
            const auto after = recorder.statsJSON();
            const auto row = replay::frameById(options.directory, stored.frameId);
            require(row && row->width == image.width() && row->height == image.height() && row->ocrState == "ready");
            ids.append(stored.frameId);
            QJsonObject measurement{{"job", i}, {"suite", jobs[i].suite}, {"source_index", jobs[i].index},
                {"width", image.width()}, {"height", image.height()}, {"input_decode_or_render_wall_ms", decodeWall},
                {"input_decode_or_render_cpu_ms", decodeCpu}, {"record_wall_ms", wall}, {"record_cpu_ms", cpu}};
            for (auto it = after.begin(); it != after.end(); ++it)
                if (it.key().startsWith("ocr_") && !it.key().startsWith("ocr_max_") &&
                    it.key() != "ocr_cpu_percent" && it.value().isDouble() && before.value(it.key()).isDouble())
                    measurement[it.key()] = it.value().toDouble() - before.value(it.key()).toDouble();
            measurements.append(measurement);
            emitJson({{"event", "job_end"}, {"job", i}});
        }
        recorder.finish();
        const QString baselineDirectory = parser.value("baseline-dir");
        QStringList vocabulary;
        for (const auto &job : jobs) for (const auto &term : job.truth) if (!vocabulary.contains(term)) vocabulary.append(term);
        QMap<QString, QSet<qint64>> searchHits;
        for (const auto &term : vocabulary)
            for (const auto &row : replay::searchFrames(options.directory, term, 1000)) searchHits[term].insert(row.id);
        QJsonObject totals{{"expected_terms", 0}, {"exact_term_hits", 0}, {"search_term_hits", 0},
                           {"unexpected_exact_hits", 0}, {"unexpected_search_hits", 0},
                           {"lost_baseline_exact_terms", 0}, {"gained_baseline_exact_terms", 0},
                           {"baseline_tokens", 0}, {"candidate_tokens", 0}, {"common_tokens", 0}};
        for (int i = 0; i < jobs.size(); ++i) {
            auto measurement = measurements[i].toObject();
            const auto candidate = replay::frameById(options.directory, ids[i]);
            require(bool(candidate));
            int exact = 0, search = 0, unexpectedExact = 0, unexpectedSearch = 0;
            for (const auto &term : vocabulary) {
                const bool expected = jobs[i].truth.contains(term);
                const bool exactHit = exactTerm(candidate->text, term), searchHit = searchHits[term].contains(ids[i]);
                if (expected) { exact += exactHit; search += searchHit; }
                else { unexpectedExact += exactHit; unexpectedSearch += searchHit; }
            }
            QJsonObject quality{{"expected_terms", jobs[i].truth.size()}, {"exact_term_hits", exact},
                {"search_term_hits", search}, {"unexpected_exact_hits", unexpectedExact}, {"unexpected_search_hits", unexpectedSearch}};
            const auto baseline = baselineDirectory.isEmpty() ? candidate : replay::frameById(baselineDirectory, ids[i]);
            require(baseline && baseline->width == candidate->width && baseline->height == candidate->height);
            const auto comparison = agreement(baseline->text, candidate->text);
            if (source != "private") measurement["baseline_agreement"] = comparison;
            for (const QString key : {"baseline_tokens", "candidate_tokens", "common_tokens"})
                totals[key] = totals[key].toInteger() + comparison[key].toInteger();
            int lost = 0, gained = 0;
            for (const auto &term : jobs[i].truth) {
                const bool was = exactTerm(baseline->text, term), now = exactTerm(candidate->text, term);
                lost += was && !now; gained += !was && now;
            }
            quality["lost_baseline_exact_terms"] = lost;
            quality["gained_baseline_exact_terms"] = gained;
            if (source != "private") measurement["quality"] = quality;
            for (auto it = quality.begin(); it != quality.end(); ++it) totals[it.key()] = totals[it.key()].toInteger() + it.value().toInteger();
            measurements[i] = measurement;
        }
        const auto a = totals["baseline_tokens"].toInteger(), b = totals["candidate_tokens"].toInteger(), common = totals["common_tokens"].toInteger();
        totals["baseline_token_recall"] = a ? QJsonValue(double(common) / a) : QJsonValue();
        totals["candidate_token_precision"] = b ? QJsonValue(double(common) / b) : QJsonValue();
        if (source == "private")
            for (const QString key : {"expected_terms", "exact_term_hits", "search_term_hits", "unexpected_exact_hits",
                                      "unexpected_search_hits", "lost_baseline_exact_terms", "gained_baseline_exact_terms"})
                totals.remove(key);
        emitJson({{"event", "result"}, {"source", source}, {"mode", options.ocrMode}, {"ocr_height", height},
                  {"frames", jobs.size()}, {"jobs", measurements}, {"totals", totals},
                  {"stats", safeStats(recorder.statsJSON())}, {"archive_dimensions_preserved", true},
                  {"agreement_is_accuracy", false}, {"baseline_reference_is_self", baselineDirectory.isEmpty()}});
        return 0;
    } catch (...) {
        // Library errors can contain source paths. Keep experiment logs numeric
        // and content-free even on private-input failures.
        emitJson({{"event", "error"}, {"failed", true}});
        return 1;
    }
}
