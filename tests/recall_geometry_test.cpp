#include "recorder.h"

#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct FixtureIndex {
    sqlite3 *db = nullptr;
    explicit FixtureIndex(const QString &directory) {
        require(sqlite3_open(QDir(directory).filePath("index.sqlite").toUtf8().constData(), &db) == SQLITE_OK,
                "Cannot open synthetic test index");
    }
    ~FixtureIndex() { sqlite3_close(db); }
    void exec(const char *sql) { require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "Synthetic SQL failed"); }
    void text(qint64 id, const QString &text, const QJsonArray &lines) {
        exec("BEGIN");
        sqlite3_stmt *statement = nullptr;
        require(sqlite3_prepare_v2(db, "UPDATE frames SET text=?,ocr_state='ready' WHERE id=?", -1, &statement, nullptr) == SQLITE_OK,
                "Cannot prepare fixture text");
        auto bytes = text.toUtf8();
        sqlite3_bind_text(statement, 1, bytes.constData(), bytes.size(), SQLITE_TRANSIENT); sqlite3_bind_int64(statement, 2, id);
        require(sqlite3_step(statement) == SQLITE_DONE, "Cannot store fixture text"); sqlite3_finalize(statement);
        require(sqlite3_prepare_v2(db, "INSERT INTO frame_text(rowid,text) VALUES(?,?)", -1, &statement, nullptr) == SQLITE_OK,
                "Cannot prepare fixture FTS");
        sqlite3_bind_int64(statement, 1, id); sqlite3_bind_text(statement, 2, bytes.constData(), bytes.size(), SQLITE_TRANSIENT);
        require(sqlite3_step(statement) == SQLITE_DONE, "Cannot store fixture FTS"); sqlite3_finalize(statement);
        require(sqlite3_prepare_v2(db, "INSERT INTO frame_ocr_geometry(frame_id,lines_json) VALUES(?,?)", -1, &statement, nullptr) == SQLITE_OK,
                "Cannot prepare fixture geometry");
        bytes = QJsonDocument(lines).toJson(QJsonDocument::Compact);
        sqlite3_bind_int64(statement, 1, id); sqlite3_bind_text(statement, 2, bytes.constData(), bytes.size(), SQLITE_TRANSIENT);
        require(sqlite3_step(statement) == SQLITE_DONE, "Cannot store fixture geometry"); sqlite3_finalize(statement);
        exec("COMMIT");
    }
};

void searchGeometry(const QString &root) {
    replay::RecorderOptions options; options.directory = root + "/tokens"; options.ocr = false;
    {
        replay::Recorder recorder(options);
        QImage image(640, 360, QImage::Format_RGBA8888); image.fill(Qt::white);
        recorder.addFrame(image, 1000); recorder.finish();
    }
    FixtureIndex index(options.directory);
    index.text(1, "Patrick invoice\nPatrickson elsewhere\ncafé résumé\nfoo bar\nfoo unrelated bar", {
        QJsonArray{10, 20, 220, 20, "Patrick invoice"},
        QJsonArray{10, 60, 220, 20, "Patrickson elsewhere"},
        QJsonArray{10, 100, 220, 20, "café résumé"},
        QJsonArray{10, 140, 220, 20, "foo bar"},
        QJsonArray{10, 180, 220, 20, "foo unrelated bar"}
    });
    require(replay::matchingTextRects(options.directory, 1, "PATRICK") == QVector<QRect>{QRect(10, 20, 220, 20)},
            "Exact case-insensitive token highlight matched a substring");
    require(replay::matchingTextRects(options.directory, 1, "cafe resume") == QVector<QRect>{QRect(10, 100, 220, 20)},
            "Highlight accent handling differs from unicode61 search");
    require(replay::matchingTextRects(options.directory, 1, "foo_bar") == QVector<QRect>{QRect(10, 140, 220, 20)},
            "Quoted token phrase highlighted separated words");
    require(replay::matchingTextRects(options.directory, 1, "Patrick: cafe").size() == 2,
            "Matching words on separate lines were not highlighted");
    require(replay::matchingTextRects(options.directory, 1, "Patrick missing").isEmpty(),
            "Incomplete AND query showed partial highlights");
    require(replay::matchingTextRects(options.directory, 1, "Patrick OR missing").isEmpty(),
            "User query operator was executed");
    require(replay::matchingTextRects(options.directory, 1, "***").isEmpty(), "Punctuation produced highlights");
    const auto copied = replay::matchingTextLines(options.directory, 1, "Patrick invoice");
    require(copied.geometryAvailable && copied.text == "Patrick invoice" &&
            copied.boxes == QVector<QRect>{QRect(10, 20, 220, 20)},
            "Matching-line copy included surrounding text or repeated a line for two matching words");
    const auto readingOrder = replay::matchingTextLines(options.directory, 1, "cafe Patrick");
    require(readingOrder.text == "Patrick invoice\ncafé résumé" && readingOrder.boxes.size() == 2,
            "Matching-line copy followed query order instead of recorded reading order");
    require(replay::matchingTextLines(options.directory, 1, "Patrick missing").text.isEmpty(),
            "Incomplete query copied partial matches");
    index.exec("DROP TABLE frame_ocr_geometry");
    require(replay::searchFrames(options.directory, "Patrick").size() == 1, "Legacy search stopped working");
    require(replay::matchingTextRects(options.directory, 1, "Patrick").isEmpty(), "Legacy coordinates were invented");
    const auto legacy = replay::matchingTextLines(options.directory, 1, "Patrick");
    require(!legacy.geometryAvailable && legacy.boxes.isEmpty() && legacy.text.isEmpty(),
            "Legacy matching-line copy silently fell back to whole-frame text");
    sqlite3_stmt *count = nullptr;
    sqlite3_prepare_v2(index.db, "SELECT COUNT(*) FROM sqlite_master WHERE name='frame_ocr_geometry'", -1, &count, nullptr);
    require(sqlite3_step(count) == SQLITE_ROW && sqlite3_column_int(count, 0) == 0, "Read-only highlights migrated history");
    sqlite3_finalize(count);
}

void prefixSearch(const QString &root) {
    replay::RecorderOptions options; options.directory = root + "/prefix"; options.ocr = false;
    {
        replay::Recorder recorder(options);
        QImage image(640, 360, QImage::Format_RGBA8888); image.fill(Qt::white);
        recorder.addFrame(image, 1000); recorder.finish();
    }
    FixtureIndex index(options.directory);
    const QStringList texts{"Please continue\nconstant", "Please continuous", "Please continuity", "Please discontinue", "contin", "con",
        "foo barge\nfoo unrelated barge", "CAFÉ résumé", "Cafe\u0301 resumes", "Pleasant continuous", "连续工作", "𐐀𐐁𐐂𐐃", "literal OR continuous"};
    for (int i = 0; i < texts.size(); ++i) {
        if (i) {
            const auto sql = QString("INSERT INTO frames(id,timestamp_ms,last_timestamp_ms,frame_index,path,codec,width,height,text,ocr_state) "
                "VALUES(%1,%2,%2,0,'media/fixture.webp','webp',640,360,'','pending')").arg(i + 1).arg((i + 1) * 1000).toUtf8();
            index.exec(sql.constData());
        }
        QJsonArray geometry;
        const auto lines = texts[i].split('\n');
        for (int row = 0; row < lines.size(); ++row) geometry.append(QJsonArray{10, 20 + row * 40, 220, 20, lines[row]});
        index.text(i + 1, texts[i], geometry);
    }
    const auto prefix = replay::SearchMode::PrefixLastToken;
    const auto page = [&](const QString &query) { return replay::searchFramePage(options.directory, query, 100, 0, prefix); };
    require(page("contin").totalMatches == 6, "Prefix did not include complete word variants or matched an interior substring");
    require(replay::searchFrames(options.directory, "contin").size() == 1, "Prefix behavior changed exact search defaults");
    require(replay::searchFramePage(options.directory, "contin").totalMatches == 1, "Paged search default is not exact");
    require(page("Please contin").totalMatches == 3 && page("Ple contin").totalMatches == 0,
            "Prefix changed earlier exact query terms");
    require(page("con").totalMatches == 7 && page("co").totalMatches == 0,
            "Three-character threshold did not gate prefix expansion");
    require(page("CAF res").totalMatches == 0 && page("cafe res").totalMatches == 2,
            "Prefix Unicode accent normalization or earlier exact term handling differs from FTS");
    require(page("foo_ba").totalMatches == 0 && page("foo_bar").totalMatches == 1,
            "Quoted phrase prefix length did not apply to the final FTS token");
    require(page("连续").totalMatches == 0 && page("连续工").totalMatches == 1,
            "Prefix threshold did not count Unicode characters");
    require(page("𐐀𐐁").totalMatches == 0 && page("𐐀𐐁𐐂").totalMatches == 1,
            "Prefix threshold counted UTF-16 units instead of Unicode characters");
    require(page("contin\" OR missing*").totalMatches == 0 && page("literal OR contin").totalMatches == 1,
            "User syntax was executed instead of tokenized literally");
    require(page("\"***\"").totalMatches == 0, "Punctuation became a match-all search");
    require(replay::matchingTextRects(options.directory, 1, "contin", prefix) == QVector<QRect>{QRect(10, 20, 220, 20)},
            "Prefix highlight did not select the matching line");
    require(replay::matchingTextRects(options.directory, 1, "contin").isEmpty(), "Exact highlight defaults changed");
    require(replay::matchingTextRects(options.directory, 4, "contin", prefix).isEmpty(), "Prefix highlighted a nonmatching substring");
    require(replay::matchingTextRects(options.directory, 7, "foo_bar", prefix) == QVector<QRect>{QRect(10, 20, 220, 20)},
            "Prefix highlight lost quoted-phrase adjacency");
    require(replay::matchingTextRects(options.directory, 7, "foo_ba", prefix).isEmpty(), "Short phrase token gained prefix highlights");
    require(!replay::matchingTextRects(options.directory, 9, "cafe res", prefix).isEmpty(),
            "Decomposed accent search did not agree with highlight tokenization");
    require(replay::matchingTextRects(options.directory, 1, "missing contin", prefix).isEmpty(),
            "Incomplete multiword prefix query showed highlights");
    const auto prefixLines = replay::matchingTextLines(options.directory, 1, "Please contin", prefix);
    require(prefixLines.geometryAvailable && prefixLines.text == "Please continue" &&
            prefixLines.boxes == replay::matchingTextRects(options.directory, 1, "Please contin", prefix),
            "Prefix matching-line copy differs from highlighted lines");
    require(replay::matchingTextLines(options.directory, 7, "foo_bar", prefix).text == "foo barge",
            "Matching-line copy included a line with separated phrase tokens");
    require(replay::matchingTextLines(options.directory, 9, "cafe res", prefix).text == "Cafe\u0301 resumes",
            "Matching-line copy changed recognized accents or prefix normalization");
    index.exec("UPDATE frame_ocr_geometry SET lines_json='[]' WHERE frame_id=1");
    const auto empty = replay::matchingTextLines(options.directory, 1, "contin", prefix);
    require(!empty.geometryAvailable && empty.boxes.isEmpty() && empty.text.isEmpty(),
            "Empty geometry silently copied the whole frame");
    index.exec("UPDATE frame_ocr_geometry SET lines_json='[[900,900,10,10,\"Please continue\"]]' WHERE frame_id=1");
    const auto invalid = replay::matchingTextLines(options.directory, 1, "contin", prefix);
    require(!invalid.geometryAvailable && invalid.text.isEmpty(), "Invalid line geometry was presented as copyable match text");
}

void boundedTimeline(const QString &root) {
    replay::RecorderOptions options; options.directory = root + "/timeline"; options.ocr = false;
    {
        replay::Recorder recorder(options);
        require(replay::timelineOverview(options.directory).totalFrames == 0, "Empty timeline had frames");
        require(!replay::frameNearTimestamp(options.directory, 100), "Empty seek found a frame");
        QImage image(32, 32, QImage::Format_RGBA8888); image.fill(Qt::white);
        recorder.addFrame(image, 1000); recorder.finish();
    }
    FixtureIndex index(options.directory);
    index.exec("WITH RECURSIVE n(x) AS (SELECT 2 UNION ALL SELECT x+1 FROM n WHERE x<5000) "
        "INSERT INTO frames(id,timestamp_ms,last_timestamp_ms,frame_index,path,codec,width,height,text,ocr_state) "
        "SELECT x,x*1000,x*1000,0,'media/fixture.webp','webp',32,32,'','pending' FROM n");
    index.exec("UPDATE frames SET last_timestamp_ms=5009000 WHERE id=5000");
    const auto timeline = replay::timelineOverview(options.directory, 73);
    require(timeline.totalFrames == 5000 && timeline.points.size() == 73, "Timeline was truncated or ignored its marker bound");
    require(timeline.firstTimestampMs == 1000 && timeline.lastTimestampMs == 5009000 &&
            timeline.points.first().id == 1 && timeline.points.last().id == 5000, "Timeline lost its endpoints or duplicate duration");
    for (int i = 1; i < timeline.points.size(); ++i)
        require(timeline.points[i - 1].timestampMs < timeline.points[i].timestampMs, "Timeline markers are unordered");
    require(replay::timelineOverview(options.directory, 1).points.size() == 2, "Tiny marker request lost an endpoint");
    require(replay::frameNearTimestamp(options.directory, 4000400)->id == 4000, "Timeline seek failed beyond old list limit");
    require(replay::frameNearTimestamp(options.directory, 4000600)->id == 4001, "Timeline seek did not find closest moment");
    require(replay::frameNearTimestamp(options.directory, -100)->id == 1, "Seek did not clamp before history");
    require(replay::frameNearTimestamp(options.directory, 9999999)->id == 5000, "Seek did not clamp after history");
    require(replay::frameNearTimestamp(options.directory, 5008000)->id == 5000, "Repeated observation interval was lost");
    index.exec("UPDATE frames SET text=CASE WHEN id%2=0 THEN 'continuous' ELSE 'unrelated' END,ocr_state='ready';"
        "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames");
    const auto mode = replay::SearchMode::PrefixLastToken;
    const auto matches = replay::searchFramePage(options.directory, "contin", 100, 200, mode, 73);
    require(matches.totalMatches == 2500 && matches.offset == 200 && matches.frames.size() == 100 &&
            matches.frames.first().id == 402 && matches.frames.last().id == 600, "Search pages lost matches beyond the first 100");
    require(matches.timeline.totalFrames == 2500 && matches.timeline.points.size() <= 73 &&
            matches.timeline.points.first().id == 2 && matches.timeline.points.last().id == 5000 &&
            matches.timeline.lastTimestampMs == 5009000, "Search markers did not cover the whole filtered timeline");
    for (const auto &point : matches.timeline.points) require(point.id % 2 == 0, "Search marker refers to a nonmatching frame");
    const auto lastPage = replay::searchFramePage(options.directory, "contin", 100, 2490, mode, 2);
    require(lastPage.frames.size() == 10 && lastPage.frames.last().id == 5000 && lastPage.timeline.points.size() == 2,
            "Last search page or two-marker endpoint bound is incorrect");
    require(replay::searchFrameOffsetNearTimestamp(options.directory, "contin", 4000100, mode) == 1999,
            "Nearest search offset did not reach beyond the first page");
    require(replay::searchFrameOffsetNearTimestamp(options.directory, "contin", 4001100, mode) == 2000,
            "Nearest search offset selected a nonmatching moment");
    require(replay::searchFrameOffsetNearTimestamp(options.directory, "contin", 5008000, mode) == 2499,
            "Nearest search did not preserve the repeated observation interval");
    require(replay::searchFrameOffsetNearTimestamp(options.directory, "contin", -100, mode) == 0 &&
            replay::searchFrameOffsetNearTimestamp(options.directory, "contin", 9999999, mode) == 2499,
            "Nearest search did not clamp to matching endpoints");
    require(!replay::searchFrameOffsetNearTimestamp(options.directory, "absent", 1000, mode),
            "Nearest search returned a row for an empty result set");
    const auto anchored = replay::searchFramePage(options.directory, "contin", 100, 0, mode, 73, 432);
    require(anchored.offset == 200 && anchored.selectedRow == 15 && anchored.frames[anchored.selectedRow].id == 432,
            "Search anchor beyond the first page did not select its matching frame");
    index.exec("UPDATE frames SET text='continuity' WHERE id<200 AND id%2=1;"
        "DELETE FROM frame_text WHERE rowid<200 AND rowid%2=1;"
        "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames WHERE id<200 AND id%2=1");
    const auto shifted = replay::searchFramePage(options.directory, "contin", 100, 0, mode, 73, 432);
    require(shifted.offset == 300 && shifted.selectedRow == 15 && shifted.frames[shifted.selectedRow].id == 432 &&
            shifted.totalMatches == 2600, "New earlier matches shifted the stable search anchor onto another frame");
    require(replay::searchFramePage(options.directory, "contin", 100, 0, mode, 73, 501).selectedRow == -1,
            "Nonmatching anchor claimed a selected search row");
    index.exec("DELETE FROM frames WHERE id BETWEEN 1000 AND 2000");
    const auto deleted = replay::timelineOverview(options.directory, 73);
    require(deleted.totalFrames == 3999 && deleted.points.size() <= 73 && deleted.points.last().id == 5000,
            "Sparse timeline violated bounds");
    require(replay::frameNearTimestamp(options.directory, 1900000)->id == 2001, "Timestamp seek landed in deleted history");
}

void resizedGeometry(const QString &root) {
    for (bool deferred : {false, true}) {
    replay::RecorderOptions options; options.directory = root + "/resized";
    options.directory += deferred ? "-deferred" : "-sync";
    options.deferredOcr = deferred;
    options.ocrMaxHeight = 360; options.ocrMode = "full";
    replay::Recorder recorder(options);
    QImage image(1280, 720, QImage::Format_RGBA8888); image.fill(Qt::white);
    { QPainter painter(&image); QFont font("DejaVu Sans"); font.setPixelSize(56);
      painter.setFont(font); painter.setPen(Qt::black); painter.drawText(80, 400, "Patrick invoice"); }
    const auto frame = recorder.addFrame(image, 1000); recorder.finish();
    if (deferred) {
        replay::IndexerOptions workerOptions; workerOptions.directory = options.directory;
        workerOptions.ocrMaxHeight = 360; workerOptions.ocrMode = "full";
        replay::Indexer worker(workerOptions);
        require(worker.processNext().state == "ready", "Deferred resize fixture failed");
    }
    const auto boxes = replay::matchingTextRects(options.directory, frame.frameId, "Patrick");
    require(!boxes.isEmpty() && boxes.first().contains(QPoint(150, 380)) && boxes.first().width() > 300,
            "Resized OCR boxes were not mapped back to original image coordinates");
    }
}

void cachedGeometry(const QString &root) {
    for (const QString &mode : {QString("incremental"), QString("regions")}) {
        replay::RecorderOptions options; options.directory = root + "/cache-" + mode; options.ocrMode = mode;
        replay::Recorder recorder(options);
        for (int step = 0; step < 3; ++step) {
            QImage image(1280, 720, QImage::Format_RGBA8888); image.fill(Qt::white);
            { QPainter painter(&image); QFont font("DejaVu Sans"); font.setPixelSize(32);
              painter.setFont(font); painter.setPen(Qt::black);
              painter.drawText(80, 100, "Stable header"); painter.drawText(80, 620, "Stable footer");
              if (step < 2) painter.drawText(80, 350, step == 0 ? "Patrick invoice" : "Brenda invoice"); }
            const auto frame = recorder.addFrame(image, 1000 + step * 1000);
            require(!replay::matchingTextRects(options.directory, frame.frameId, "Stable").isEmpty(),
                    "Cached unchanged lines lost highlight geometry");
            if (step == 0) require(!replay::matchingTextRects(options.directory, frame.frameId, "Patrick").isEmpty(),
                    "Initial cache line has no geometry");
            else require(replay::matchingTextRects(options.directory, frame.frameId, "Patrick").isEmpty(),
                    "Edited line retained stale highlight geometry");
            if (step == 1) require(!replay::matchingTextRects(options.directory, frame.frameId, "Brenda").isEmpty(),
                    "Replacement line has no geometry");
            if (step == 2) require(replay::matchingTextRects(options.directory, frame.frameId, "invoice").isEmpty(),
                    "Deleted line retained stale geometry");
        }
        recorder.finish();
        require(recorder.statsJSON()["ocr_partial_frames"].toInteger() > 0,
                "Geometry cache test did not exercise partial recognition");
    }
}
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    try {
        QTemporaryDir temporary; require(temporary.isValid(), "Cannot create test directory");
        searchGeometry(temporary.path()); prefixSearch(temporary.path()); boundedTimeline(temporary.path()); resizedGeometry(temporary.path()); cachedGeometry(temporary.path());
        std::cout << "PASS literal and prefix OCR highlights, Unicode token parity, paged search, bounded timelines, timestamp seeks and resize geometry\n";
        return 0;
    } catch (const std::exception &exception) { std::cerr << exception.what() << '\n'; return 1; }
}
