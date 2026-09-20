#include "recorder.h"
#include "ocr_fixtures.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

struct Dataset {
    QString directory;
    QVector<replay::FrameRecord> frames;
    QJsonObject stats;
};

Dataset record(const QString &root, QSize size, const QString &mode) {
    replay::RecorderOptions options;
    options.directory = root + "/" + mode;
    options.codec = "webp";
    options.ocrMode = mode;
    replay::Recorder recorder(options);
    for (int index = 0; index < ocr_fixture::count; ++index) {
        const auto result = recorder.addFrame(ocr_fixture::frame(index, size), 1000 + index * 2000);
        if (!result.stored || result.duplicate)
            throw std::runtime_error("A changed source image was discarded");
    }
    recorder.finish();
    return {options.directory, replay::listFrames(options.directory), recorder.statsJSON()};
}

QSet<qint64> hits(const Dataset &dataset, const QString &term) {
    QSet<qint64> result;
    for (const auto &frame : replay::searchFrames(dataset.directory, term)) result.insert(frame.id);
    return result;
}

QJsonObject checkSequence(const QString &root, QSize size, QStringList &failures) {
    const Dataset full = record(root, size, "full");
    const Dataset incremental = record(root, size, "incremental");
    const Dataset regions = record(root, size, "regions");
    if (full.frames.size() != ocr_fixture::count || incremental.frames.size() != ocr_fixture::count ||
        regions.frames.size() != ocr_fixture::count)
        throw std::runtime_error("Wrong retained-frame count");
    if (incremental.stats["ocr_partial_frames"].toInteger() == 0)
        failures << QString("%1px: no partial OCR ran; incremental reuse was not exercised").arg(size.width());
    if (regions.stats["ocr_regions_frames"].toInteger() == 0)
        failures << QString("%1px: no regions OCR completed; cache reuse was not exercised").arg(size.width());

    QStringList vocabulary;
    for (int index = 0; index < ocr_fixture::count; ++index)
        for (const auto &term : ocr_fixture::visibleTerms(index))
            if (!vocabulary.contains(term)) vocabulary << term;

    QJsonArray baselineMisses, baselineFalseHits, regressions, staleHits, recovered, frames;
    QJsonArray regionRegressions, regionStaleHits;
    int expectedOccurrences = 0, baselineRecognized = 0, incrementalRecognized = 0, regionsRecognized = 0;
    QSet<int> baselinePositiveFrames;
    for (const auto &term : vocabulary) {
        const auto fullHits = hits(full, term);
        const auto incrementalHits = hits(incremental, term);
        const auto regionHits = hits(regions, term);
        for (int index = 0; index < ocr_fixture::count; ++index) {
            const bool expected = ocr_fixture::visibleTerms(index).contains(term);
            const bool baselineHit = fullHits.contains(full.frames[index].id);
            const bool incrementalHit = incrementalHits.contains(incremental.frames[index].id);
            const bool regionHit = regionHits.contains(regions.frames[index].id);
            const QJsonObject occurrence{{"frame", index}, {"scene", ocr_fixture::name(index)}, {"term", term}};
            if (expected) {
                ++expectedOccurrences;
                if (baselineHit) { ++baselineRecognized; baselinePositiveFrames.insert(index); }
                else baselineMisses.append(occurrence);
                if (incrementalHit) ++incrementalRecognized;
                if (regionHit) ++regionsRecognized;
                if (baselineHit && !incrementalHit) regressions.append(occurrence);
                if (!baselineHit && incrementalHit) recovered.append(occurrence);
                if (baselineHit && !regionHit) regionRegressions.append(occurrence);
            } else {
                if (baselineHit) baselineFalseHits.append(occurrence);
                if (incrementalHit) staleHits.append(occurrence);
                if (regionHit) regionStaleHits.append(occurrence);
            }
        }
    }

    for (int index = 0; index < ocr_fixture::count; ++index) {
        const auto &baseline = full.frames[index];
        const auto &candidate = incremental.frames[index];
        const QSize expectedSize = ocr_fixture::frameSize(index, size);
        if (QSize(candidate.width, candidate.height) != expectedSize)
            failures << QString("%1px/%2: incorrect recorded dimensions").arg(size.width()).arg(ocr_fixture::name(index));
        if (index != 11 && !baselinePositiveFrames.contains(index))
            failures << QString("%1px/%2: baseline recognized no expected term; comparison is inconclusive")
                            .arg(size.width()).arg(ocr_fixture::name(index));
        if (index == 11 && !candidate.text.trimmed().isEmpty())
            failures << QString("%1px/blank: previous OCR text survived a blank image").arg(size.width());
        if (index == 11 && !regions.frames[index].text.trimmed().isEmpty())
            failures << QString("%1px/regions blank: previous OCR text survived a blank image").arg(size.width());
        if (replay::loadFrame(regions.directory, regions.frames[index].id) != ocr_fixture::frame(index, size))
            failures << QString("%1px/regions: changed archive pixels").arg(size.width());
        frames.append(QJsonObject{{"frame", index}, {"scene", ocr_fixture::name(index)},
                                  {"width", expectedSize.width()}, {"height", expectedSize.height()},
                                  {"visible_terms", QJsonArray::fromStringList(ocr_fixture::visibleTerms(index))},
                                  {"full_text", baseline.text}, {"incremental_text", candidate.text},
                                  {"regions_text", regions.frames[index].text}});
    }

    // The same term must identify exactly its historical frames. An absent
    // word may never be carried forward, even if full-frame OCR also errs.
    for (const auto &regression : regressions)
        failures << QString("%1px: baseline-recognized term lost: %2")
                        .arg(size.width()).arg(QString::fromUtf8(QJsonDocument(regression.toObject()).toJson(QJsonDocument::Compact)));
    for (const auto &stale : staleHits)
        failures << QString("%1px: invisible term remains searchable: %2")
                        .arg(size.width()).arg(QString::fromUtf8(QJsonDocument(stale.toObject()).toJson(QJsonDocument::Compact)));
    for (const auto &regression : regionRegressions)
        failures << QString("%1px/regions: baseline-recognized term lost: %2")
                        .arg(size.width()).arg(QString::fromUtf8(QJsonDocument(regression.toObject()).toJson(QJsonDocument::Compact)));
    for (const auto &stale : regionStaleHits)
        failures << QString("%1px/regions: invisible term remains searchable: %2")
                        .arg(size.width()).arg(QString::fromUtf8(QJsonDocument(stale.toObject()).toJson(QJsonDocument::Compact)));

    return {{"width", size.width()}, {"height", size.height()}, {"source_frames", ocr_fixture::count},
            {"expected_token_occurrences", expectedOccurrences}, {"full_recognized", baselineRecognized},
            {"incremental_recognized", incrementalRecognized}, {"baseline_misses", baselineMisses},
            {"baseline_false_hits", baselineFalseHits}, {"incremental_regressions", regressions},
            {"incremental_false_hits", staleHits}, {"incremental_recovered", recovered},
            {"regions_recognized", regionsRecognized}, {"regions_regressions", regionRegressions},
            {"regions_false_hits", regionStaleHits}, {"regions_stats", regions.stats},
            {"full_stats", full.stats}, {"incremental_stats", incremental.stats}, {"frames", frames}};
}

QJsonObject checkOcrResize(const QString &root) {
    QImage large(1280, 960, QImage::Format_RGBA8888);
    large.fill(Qt::white);
    {
        QPainter painter(&large);
        QFont font("DejaVu Sans");
        font.setPixelSize(64);
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(80, 160, "ORIGINAL PIXELS");
    }
    QImage tinyEdit = large;
    tinyEdit.setPixelColor(1, 1, QColor(255, 254, 255));
    QImage small(320, 240, QImage::Format_RGBA8888);
    small.fill(QColor("#edf2f7"));
    const QVector<QImage> originals{large, tinyEdit, small};
    QJsonObject results;
    for (const bool deferred : {false, true}) {
        replay::RecorderOptions options;
        options.directory = root + (deferred ? "/resize-deferred" : "/resize-sync");
        options.codec = "webp";
        options.ocrMaxHeight = 480;
        options.deferredOcr = deferred;
        replay::Recorder recorder(options);
        for (int index = 0; index < originals.size(); ++index) {
            const auto result = recorder.addFrame(originals[index], 1000 + index * 1000);
            if (!result.stored || result.duplicate)
                throw std::runtime_error("OCR resizing discarded an original-pixel change");
            if (replay::loadFrame(options.directory, result.frameId) != originals[index])
                throw std::runtime_error("OCR resizing changed retained original pixels");
        }
        recorder.finish();
        QJsonObject stats = recorder.statsJSON();
        if (deferred) {
            replay::IndexerOptions indexOptions;
            indexOptions.directory = options.directory;
            indexOptions.ocrMode = "full";
            indexOptions.ocrMaxHeight = 480;
            replay::Indexer indexer(indexOptions);
            for (int index = 0; index < originals.size(); ++index) {
                const auto result = indexer.processNext();
                if (!result.processed || result.state != "ready")
                    throw std::runtime_error("Deferred resized OCR did not finish its original source");
            }
            stats = indexer.statsJSON();
        }
        const auto frames = replay::listFrames(options.directory);
        if (frames.size() != originals.size()) throw std::runtime_error("Resizing changed archival frame count");
        if (stats["ocr_original_input_pixels"].toInteger() != 2 * 1280 * 960 + 320 * 240 ||
            stats["ocr_input_pixels"].toInteger() != 2 * 640 * 480 + 320 * 240 ||
            stats["ocr_resized_frames"].toInteger() != 2)
            throw std::runtime_error("OCR height cap did not downsize large images while preserving small inputs");
        for (int index = 0; index < frames.size(); ++index) {
            if (QSize(frames[index].width, frames[index].height) != originals[index].size() ||
                replay::loadFrame(options.directory, frames[index].id) != originals[index])
                throw std::runtime_error("Indexed archive does not preserve original dimensions and pixels");
        }
        results[deferred ? "deferred" : "sync"] = stats;
    }
    return results;
}

QString denseIdentifier(int scene, int column, int row) {
    if (scene >= 2 && column == 1 && row == 8) return {};
    const QChar label = scene == 3 && column == 1 ? QChar('D') : QChar(char('A' + column));
    // Keep the edited lower row distinct from the known full-OCR 210 -> 216
    // confusion, so this fixture can falsify stale-cache retention itself.
    const bool edited = scene >= 1 && ((column == 0 && row == 4) || (column == 2 && row == 14));
    return QString("COL%1-%2-%3").arg(label).arg(201 + row).arg(edited ? 732 : 731);
}

QImage denseFrame(int scene, QStringList *terms = nullptr) {
    QImage image(1920, 1080, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::black);
    QFont font("DejaVu Sans Mono"); font.setPixelSize(22); painter.setFont(font);
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 18; ++row) {
            const QString identifier = denseIdentifier(scene, column, row);
            if (identifier.isEmpty()) continue;
            painter.drawText(90 + column * 630, 140 + row * 46, identifier);
            if (terms) terms->append(identifier);
        }
    }
    return image;
}

bool containsIdentifier(const QString &text, const QString &identifier) {
    const QRegularExpression exact("(?<![\\p{L}\\p{N}_])" + QRegularExpression::escape(identifier) +
        "(?![\\p{L}\\p{N}_])", QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption);
    return exact.match(text).hasMatch();
}

QJsonObject checkDenseRegions(const QString &root, QStringList &failures) {
    QVector<Dataset> datasets;
    QVector<QStringList> truth;
    QStringList vocabulary;
    for (int scene = 0; scene < 4; ++scene) {
        QStringList terms; denseFrame(scene, &terms); truth.append(terms);
        for (const auto &term : terms) if (!vocabulary.contains(term)) vocabulary.append(term);
    }
    for (const QString mode : {QString("full"), QString("regions")}) {
        replay::RecorderOptions options; options.directory = root + "/dense-" + mode; options.ocrMode = mode;
        replay::Recorder recorder(options);
        for (int scene = 0; scene < 4; ++scene) {
            const auto result = recorder.addFrame(denseFrame(scene), 1000 + scene * 2000);
            if (!result.stored) throw std::runtime_error("Dense column change was not retained");
        }
        recorder.finish();
        datasets.append({options.directory, replay::listFrames(options.directory), recorder.statsJSON()});
    }
    const auto &full = datasets[0], &regions = datasets[1];
    int baselineHits = 0, regionHits = 0;
    QVector<int> baselinePerScene(4, 0);
    QJsonArray searchDisagreements, baselineSearchFalseHits, regionSearchFalseHits, frames;
    // FTS currently splits COLA-205-731 into three words and ANDs them across
    // the frame. Other rows can supply 731 after the exact identifier changes.
    // Stale OCR must therefore be checked in the extracted text itself, while
    // preserving a separate comparison of the actual search behavior.
    if (containsIdentifier("COLA-205-732\nCOLB-209-731", "COLA-205-731") ||
        !containsIdentifier("COLA-205-731\n", "COLA-205-731") ||
        containsIdentifier("COLA-205-7318", "COLA-205-731"))
        throw std::runtime_error("Exact identifier boundary check is not discriminating");
    for (const auto &term : vocabulary) {
        const auto baseline = hits(full, term), candidate = hits(regions, term);
        for (int scene = 0; scene < 4; ++scene) {
            const bool expected = truth[scene].contains(term);
            const bool foundFull = containsIdentifier(full.frames[scene].text, term);
            const bool foundRegion = containsIdentifier(regions.frames[scene].text, term);
            const bool fullSearchHit = baseline.contains(full.frames[scene].id);
            const bool regionSearchHit = candidate.contains(regions.frames[scene].id);
            const QJsonObject occurrence{{"scene", scene}, {"term", term},
                {"full_search_hit", fullSearchHit}, {"regions_search_hit", regionSearchHit}};
            if (fullSearchHit != regionSearchHit) searchDisagreements.append(occurrence);
            if (!expected && fullSearchHit) baselineSearchFalseHits.append(occurrence);
            if (!expected && regionSearchHit) regionSearchFalseHits.append(occurrence);
            if (expected && foundFull) { ++baselineHits; ++baselinePerScene[scene]; }
            if (expected && foundRegion) ++regionHits;
            if (expected && foundFull && !foundRegion)
                failures << QString("dense regions/%1: lost baseline identifier %2").arg(scene).arg(term);
            if (!expected && foundRegion)
                failures << QString("dense regions/%1: stale identifier %2").arg(scene).arg(term);
        }
    }
    for (int scene = 0; scene < 4; ++scene)
        if (baselinePerScene[scene] < 20)
            failures << QString("dense scene %1: fewer than 20 baseline identifiers; comparison is inconclusive").arg(scene);
    for (int scene = 0; scene < 4; ++scene)
        frames.append(QJsonObject{{"scene", scene}, {"visible_identifiers", QJsonArray::fromStringList(truth[scene])},
            {"full_text", full.frames[scene].text}, {"regions_text", regions.frames[scene].text}});
    if (regions.stats["ocr_regions_frames"].toInteger() == 0 ||
        regions.stats["ocr_regions_used"].toInteger() <= regions.stats["ocr_regions_frames"].toInteger())
        failures << "Dense columns did not exercise successful separated OCR regions";
    return {{"full_recognized", baselineHits}, {"regions_recognized", regionHits},
            {"recognition_measure", "complete identifiers in extracted text, case insensitive with word boundaries"},
            {"search_disagreements", searchDisagreements}, {"full_search_false_hits", baselineSearchFalseHits},
            {"regions_search_false_hits", regionSearchFalseHits}, {"frames", frames},
            {"full_stats", full.stats}, {"regions_stats", regions.stats}};
}

QJsonObject checkPaddingCascade(const QString &root, QStringList &failures) {
    const auto frame = [](bool edited) {
        QImage image(1280, 1536, QImage::Format_RGBA8888); image.fill(Qt::white);
        QPainter painter(&image); painter.setPen(Qt::black);
        QFont font("DejaVu Sans Mono"); font.setPixelSize(22); painter.setFont(font);
        for (int row = 0; row < 56; ++row) {
            const QString token = row == 27 ? (edited ? "INSERTED" : "VANISHED") : "STEADY-900";
            painter.drawText(50, 90 + row * 24, QString("Row %1  %2 / unchanged billing account and delivery details")
                .arg(row, 2, 10, QChar('0')).arg(token));
        }
        return image;
    };
    QJsonObject report;
    for (const QString mode : {QString("full"), QString("regions")}) {
        replay::RecorderOptions options; options.directory = root + "/padding-" + mode; options.ocrMode = mode;
        replay::Recorder recorder(options);
        recorder.addFrame(frame(false), 1000); recorder.addFrame(frame(true), 3000); recorder.finish();
        const auto frames = replay::listFrames(options.directory);
        if (frames.size() != 2 || !containsIdentifier(frames[0].text, "VANISHED") ||
            !containsIdentifier(frames[1].text, "INSERTED") || containsIdentifier(frames[1].text, "VANISHED"))
            failures << "Dense line-spacing fixture lost or retained the changed identifier in " + mode;
        const auto stats = recorder.statsJSON();
        // This asserts bounded planning, not that a crop must be accepted:
        // the independent clipped-text guard may correctly request full OCR.
        if (mode == "regions" && (stats["ocr_regions_attempted"].toInteger() < 1 ||
            stats["ocr_regions_candidate_pixels"].toInteger() > 1280 * 1536 / 20 ||
            stats["ocr_full_reason_broad_change"].toInteger() != 0))
            failures << "One dense-line edit expanded into unrelated lines through context padding";
        report[mode + "_stats"] = stats;
        QJsonArray texts;
        for (const auto &item : frames) texts.append(item.text);
        report[mode + "_text"] = texts;
    }
    return report;
}

} // namespace

int main(int argc, char **argv) {
    qputenv("OMP_THREAD_LIMIT", "1");
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    QStringList failures;
    QJsonArray sequences;
    QJsonObject resizing;
    QJsonObject denseRegions;
    QJsonObject paddingCascade;
    try {
        if (!temporary.isValid()) throw std::runtime_error("Cannot create temporary test directory");
        if (!qEnvironmentVariableIsSet("REPLAY_OCR_PADDING_ONLY")) {
            for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160)})
                sequences.append(checkSequence(temporary.path() + "/" + QString::number(size.width()), size, failures));
            resizing = checkOcrResize(temporary.path());
            denseRegions = checkDenseRegions(temporary.path(), failures);
        }
        paddingCascade = checkPaddingCascade(temporary.path(), failures);
    } catch (const std::exception &exception) {
        failures << QString::fromUtf8(exception.what());
    }
    QJsonObject report{{"passed", failures.empty()}, {"sequences", sequences}, {"resizing", resizing}, {"dense_regions", denseRegions},
                       {"padding_cascade", paddingCascade},
                       {"failures", QJsonArray::fromStringList(failures)}};
    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const QString output = qEnvironmentVariable("REPLAY_OCR_REPORT");
    if (!output.isEmpty()) {
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) {
            std::cerr << "Cannot write REPLAY_OCR_REPORT\n";
            return 1;
        }
    }
    for (const auto &sequence : sequences) {
        const auto result = sequence.toObject();
        std::cout << result["width"].toInt() << "px: full " << result["full_recognized"].toInt()
                  << '/' << result["expected_token_occurrences"].toInt() << ", incremental "
                  << result["incremental_recognized"].toInt() << "; baseline misses "
                  << QJsonDocument(result["baseline_misses"].toArray()).toJson(QJsonDocument::Compact).constData() << '\n';
    }
    for (const auto &failure : failures) std::cerr << "FAIL: " << failure.toStdString() << '\n';
    if (failures.empty()) std::cout << "PASS original-image changes, deletion, addition, scroll, scene/dimension reset and historical search\n";
    return failures.empty() ? 0 : 1;
}
