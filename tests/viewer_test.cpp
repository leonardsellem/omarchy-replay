#include "fixture.h"
#include "recorder.h"
#include "viewer.h"
#include "index_service.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QSlider>
#include <QScrollBar>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <sqlite3.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <unistd.h>

// This exercises the GUI with a deterministic index, independently of OCR/model
// accuracy. The CLI feasibility harness verifies OCR against fixtureGroundTruth.
class ViewerTest final : public QObject {
    Q_OBJECT

    static qint64 storedNumber(const QString& directory, const char* sql) {
        sqlite3* database = nullptr;
        if (sqlite3_open_v2(QDir(directory).filePath("index.sqlite").toUtf8().constData(),
                            &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (database) sqlite3_close(database);
            return -1;
        }
        // Viewer refreshes open SQLite connections asynchronously. Match the
        // production reader's bounded wait instead of treating a transient
        // connection/lock race as a missing fixture row.
        sqlite3_busy_timeout(database, 1000);
        sqlite3_stmt* statement = nullptr;
        qint64 value = -1;
        int status = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
        if (status == SQLITE_OK) status = sqlite3_step(statement);
        if (status == SQLITE_ROW) value = sqlite3_column_int64(statement, 0);
        else if (status != SQLITE_DONE)
            qWarning("Fixture database query failed (%d): %s", status, sqlite3_errmsg(database));
        sqlite3_finalize(statement);
        sqlite3_close(database);
        return value;
    }

    static QImage prefixScreen(const QString& line, int moment) {
        QImage image(1280, 720, QImage::Format_RGBA8888);
        image.fill(QColor("#111820"));
        QPainter painter(&image);
        painter.fillRect(QRect(0, 0, 1280, 52), QColor("#1c2632"));
        painter.setFont(QFont("monospace", 12));
        painter.setPen(QColor("#a8b5c5"));
        painter.drawText(QRect(32, 0, 1216, 52), Qt::AlignVCenter,
                         QString("SYNTHETIC SEARCH FIXTURE                                      moment %1").arg(moment));
        painter.setFont(QFont("monospace", 28));
        painter.setPen(QColor("#ece8da"));
        painter.drawText(QRect(80, 130, 1120, 60), Qt::AlignVCenter, "Picking up where we left off");
        painter.setFont(QFont("monospace", 22));
        painter.drawText(QRect(80, 240, 1120, 60), Qt::AlignVCenter, line);
        painter.setFont(QFont("monospace", 15));
        painter.setPen(QColor("#a8b5c5"));
        painter.drawText(QRect(80, 344, 1120, 50), Qt::AlignVCenter,
                         "Local notes  /  September 19  /  Personal workspace");
        painter.drawLine(80, 423, 1200, 423);
        painter.drawText(QRect(80, 454, 1120, 50), Qt::AlignVCenter,
                         "The text and coordinates below are deterministic test data.");
        return image;
    }

    static bool indexText(const QString& directory, qint64 id, const QString& text,
                          const QJsonArray& lines = {}) {
        sqlite3* database = nullptr;
        if (sqlite3_open(QDir(directory).filePath("index.sqlite").toUtf8().constData(), &database) != SQLITE_OK) {
            if (database) sqlite3_close(database);
            return false;
        }
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(database, sqlite3_close);
        for (const char* sql : {"UPDATE frames SET text=?,ocr_state='ready' WHERE id=?",
                               "INSERT OR REPLACE INTO frame_text(text,rowid) VALUES(?,?)",
                               "INSERT OR REPLACE INTO frame_ocr_geometry(lines_json,frame_id) VALUES(?,?)"}) {
            sqlite3_stmt* statement = nullptr;
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
            std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> prepared(statement, sqlite3_finalize);
            const QByteArray value = QByteArray(sql).contains("geometry")
                ? QJsonDocument(lines).toJson(QJsonDocument::Compact) : text.toUtf8();
            if (sqlite3_bind_text(statement, 1, value.constData(), value.size(), SQLITE_TRANSIENT) != SQLITE_OK ||
                sqlite3_bind_int64(statement, 2, id) != SQLITE_OK || sqlite3_step(statement) != SQLITE_DONE) return false;
        }
        return true;
    }

private slots:
    void fixtureContainsExactDuplicatesAndSmallChanges() {
        QCOMPARE(replay::fixtureFrameCount(), 16);
        QCOMPARE(replay::fixtureGroundTruth().size(), 16);
        const QImage original = replay::fixtureFrame(0);
        QCOMPARE(original, replay::fixtureFrame(1));
        QVERIFY(original != replay::fixtureFrame(2));
        QCOMPARE(replay::fixtureFrame(9), replay::fixtureFrame(10));
        QVERIFY(replay::fixtureFrame(10) != replay::fixtureFrame(11));
        QVERIFY(replay::fixtureGroundTruth(11).value("tokens").toArray().contains("EDGE-7F8"));
    }

    void keyboardSearchOpenAndTimeline() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory = temporary.filePath("synthetic-history");
        replay::RecorderOptions options;
        options.directory = directory;
        options.ocr = false;
        options.minFreeBytes = 0;
        constexpr qint64 start = 1789725600000;
        std::array<qint64, 4> ids{};
        {
            replay::Recorder recorder(options);
            const std::array<int, 4> fixtureIndices{0, 2, 4, 5};
            for (int i = 0; i < 4; ++i)
                ids[i] = recorder.addFrame(replay::fixtureFrame(fixtureIndices[i]), start + i * 2000).frameId;
            recorder.finish();
        }

        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        const std::array<QString, 4> indexedText{
            "Patrick invoice XYZ-1042 Northwind", "Patrick invoice XYZ-1043 Northwind",
            "Omakase demo cedar fixtures", "Omakase demo juniper completed"};
        for (int i = 0; i < 4; ++i) {
            for (const char* sql : {"UPDATE frames SET text=?,ocr_state='ready' WHERE id=?",
                                    "INSERT OR REPLACE INTO frame_text(text,rowid) VALUES(?,?)"}) {
                sqlite3_stmt* statement = nullptr;
                QCOMPARE(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr), SQLITE_OK);
                const QByteArray value = indexedText[i].toUtf8();
                QCOMPARE(sqlite3_bind_text(statement, 1, value.constData(), value.size(), SQLITE_TRANSIENT), SQLITE_OK);
                QCOMPARE(sqlite3_bind_int64(statement, 2, ids[i]), SQLITE_OK);
                QCOMPARE(sqlite3_step(statement), SQLITE_DONE);
                sqlite3_finalize(statement);
            }
        }
        QCOMPARE(sqlite3_exec(database,
            "INSERT INTO frame_ocr_geometry(frame_id,lines_json) VALUES(1,'[[122,234,140,38,\"Patrick\"],[122,504,140,38,\"Patrick\"]]')",
            nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);

        auto viewer = replay::createViewer(directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        QVERIFY(search);
        QVERIFY(results);
        QTRY_VERIFY(viewer->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(search->hasFocus());
        QTest::keyClicks(search, "Patrick");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 2);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[0]);
        QVERIFY(results->hasFocus());

        QTest::keyClick(results, Qt::Key_Down);
        QCOMPARE(results->currentRow(), 1);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[1]);

        // Time navigation crosses outside the matches without losing the query
        // or the user's selected result. Clicking the same card returns immediately.
        QTRY_VERIFY(viewer->findChild<QPushButton*>("laterMoment")->isEnabled());
        QTest::keyClick(results, Qt::Key_Right);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[2]);
        QCOMPARE(search->text(), "Patrick");
        QCOMPARE(results->count(), 2);
        QCOMPARE(results->currentRow(), 1);
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier, results->visualItemRect(results->currentItem()).center());
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[1]);

        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "Omakase");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 2);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[2]);
        QTest::keyClick(results, Qt::Key_Down);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[3]);

        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "nothing-matches-this-fixture");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 0);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(0));
        QCOMPARE(viewer->findChild<QLabel*>("recordedTimestamp")->text(), "No matching recorded text");

        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "Patrick");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[0]);
        QVERIFY(QDir().mkpath("runs"));
        QTRY_COMPARE(viewer->property("highlightCount").toInt(), 2);
        QVERIFY(QDir().mkpath("runs/design-review"));
        QVERIFY(viewer->grab().save("runs/design-review/search-desktop.png"));
        viewer->resize(900, 620);
        QTest::qWait(60);
        QVERIFY(viewer->grab().save("runs/design-review/search-compact.png"));
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClick(search, Qt::Key_Backspace);
        // Debounced search and timeline selection need no Enter key.
        QTRY_VERIFY(!results->isVisible());
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[3]);
        auto* timeline = viewer->findChild<QSlider*>("recallTimeline");
        QVERIFY(timeline && timeline->isVisible());
        timeline->setFocus();
        QTest::keyClick(timeline, Qt::Key_Home);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[0]);
        QTest::keyClick(timeline, Qt::Key_Right);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[1]);
        QTest::keyClick(timeline, Qt::Key_End);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[3]);
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(12, 43));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[0]);
        QTest::keyClick(timeline, Qt::Key_1);
        QVERIFY(!viewer->findChild<QWidget*>("evidenceView")->property("fit").toBool());
        QTest::keyClick(timeline, Qt::Key_F);
        QVERIFY(viewer->findChild<QWidget*>("evidenceView")->property("fit").toBool());
        viewer->resize(1440, 920);
        QTest::qWait(60);
        QVERIFY(viewer->grab().save("runs/design-review/timeline-desktop.png"));
        QTest::keyClick(timeline, Qt::Key_Question);
        QVERIFY(viewer->findChild<QWidget*>("helpPanel")->isVisible());
        QVERIFY(!viewer->findChild<QWidget*>("detailsPanel")->isVisible());
        QTest::keyClick(timeline, Qt::Key_Question);
        QVERIFY(!viewer->findChild<QWidget*>("helpPanel")->isVisible());

        QTest::keyClick(timeline, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());
        QVERIFY(viewer->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_Escape);
        QTRY_VERIFY(!viewer->isVisible());
    }

    void livePrefixSearchAndSeparatedPanels() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("prefix-history");
        options.ocr = false;
        options.minFreeBytes = 0;
        constexpr qint64 start = 1789837200000;
        const std::array<QString, 3> lines{
            "Continue the invoice review with Patrick.",
            "Continuous capture keeps moments available.",
            "Continuity helps us return to earlier work."};
        std::array<qint64, 3> ids{};
        {
            replay::Recorder recorder(options);
            for (int i = 0; i < 3; ++i)
                ids[i] = recorder.addFrame(prefixScreen(lines[i], i + 1), start + i * 60000).frameId;
            recorder.finish();
        }
        for (int i = 0; i < 3; ++i) {
            const auto bounds = QFontMetrics(QFont("monospace", 22)).boundingRect(
                QRect(80, 240, 1120, 60), Qt::AlignVCenter, lines[i]);
            QVERIFY(indexText(options.directory, ids[i], lines[i],
                QJsonArray{QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height(), lines[i]}}));
        }

        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* timeline = viewer->findChild<QSlider*>("recallTimeline");
        auto* indexState = viewer->findChild<QLabel*>("indexState");
        auto* details = viewer->findChild<QWidget*>("detailsPanel");
        auto* help = viewer->findChild<QWidget*>("helpPanel");
        auto* heading = viewer->findChild<QLabel*>("resultsHeading");
        auto* clearSearch = viewer->findChild<QAction*>("clearSearch");
        QVERIFY(search && results && timeline && indexState && details && help && heading && clearSearch);
        QVERIFY(!viewer->findChild<QLabel*>("matchExcerpt"));
        QCOMPARE(clearSearch->text(), "Clear search");
        QVERIFY(!clearSearch->icon().isNull());
        QVERIFY(!search->isClearButtonEnabled());
        QVERIFY(!clearSearch->isVisible());
        for (const auto* label : viewer->findChildren<QLabel*>()) QVERIFY(label->text() != "Replay");
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[2]);
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "con");
        // Three characters trigger the bounded prefix query without Enter.
        QTRY_COMPARE(viewer->property("totalMatches").toLongLong(), qint64(3));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[0]);
        QTest::keyClicks(search, "tin");
        QTest::qWait(220); // Allow the 180 ms typing debounce to start its read.
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QTRY_COMPARE(viewer->property("highlightCount").toInt(), 1);
        QCOMPARE(search->text(), "contin");
        QTRY_COMPARE(timeline->property("matchMarkerCount").toInt(), 3);
        QVERIFY(indexState->text().isEmpty());
        QVERIFY(!indexState->isVisible());
        QVERIFY(!details->isVisible());
        QVERIFY(!help->isVisible());
        QVERIFY(clearSearch->isVisible());

        // Escape keeps the live query and returns to a neutral keyboard target.
        // Match arrows and letter shortcuts then work without another click.
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());
        QVERIFY(viewer->isVisible());
        QCOMPARE(search->text(), "contin");
        QTest::keyClick(viewer.get(), Qt::Key_1);
        QVERIFY(!viewer->findChild<QWidget*>("evidenceView")->property("fit").toBool());
        QTest::keyClick(viewer.get(), Qt::Key_F);
        QVERIFY(viewer->findChild<QWidget*>("evidenceView")->property("fit").toBool());
        for (int i = 1; i < 3; ++i) {
            QTest::keyClick(viewer.get(), Qt::Key_Down);
            QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[i]);
            QTRY_COMPARE(viewer->property("highlightCount").toInt(), 1);
        }
        QVERIFY(heading->text().startsWith("3 / 3"));
        QVERIFY(results->item(0)->text().contains(':'));
        QVERIFY(!results->item(0)->text().contains('\n'));
        QVERIFY(results->mapTo(viewer.get(), QPoint()).y() >= timeline->mapTo(viewer.get(), QPoint()).y() + timeline->height());

        QVERIFY(QDir().mkpath("runs/design-review-v2"));
        QVERIFY(viewer->grab().save("runs/design-review-v2/search-desktop.png"));
        viewer->resize(900, 620);
        QTest::qWait(60);
        QVERIFY(viewer->grab().save("runs/design-review-v2/search-compact.png"));
        viewer->resize(1440, 920);
        QTest::keyClick(results, Qt::Key_I);
        QVERIFY(details->isVisible());
        QVERIFY(!help->isVisible());
        QVERIFY(!viewer->findChild<QPushButton*>("processMoment")->isVisible());
        QVERIFY(!viewer->findChild<QPushButton*>("catchUpIndexing")->isVisible());
        QVERIFY(!viewer->findChild<QPushButton*>("copyIndexCommand")->isVisible());
        QTest::qWait(60);
        QVERIFY(viewer->grab().save("runs/design-review-v2/index-ready.png"));
        QTest::keyClick(results, Qt::Key_Question);
        QVERIFY(help->isVisible());
        QVERIFY(!details->isVisible());
        QTest::qWait(60);
        QVERIFY(viewer->grab().save("runs/design-review-v2/keyboard-help.png"));
        QTest::keyClick(results, Qt::Key_I);
        QVERIFY(details->isVisible());
        QVERIFY(!help->isVisible());
        auto* refresh = viewer->findChild<QPushButton*>("refreshHistory");
        QVERIFY(refresh);
        refresh->setFocus();
        QTest::keyClick(refresh, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());
        QVERIFY(!details->isVisible());
        QVERIFY(viewer->isVisible());
        QCOMPARE(search->text(), "contin");

        QTest::keyClick(viewer.get(), Qt::Key_Question);
        QVERIFY(help->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(search->hasFocus());
        QTest::keyClick(search, Qt::Key_Escape);
        QVERIFY(!help->isVisible());
        QVERIFY(viewer->hasFocus());
        QVERIFY(viewer->isVisible());
        QCOMPARE(search->text(), "contin");
        timeline->setFocus();
        QTest::keyClick(timeline, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());
        QVERIFY(viewer->isVisible());
        QCOMPARE(search->text(), "contin");

        clearSearch->trigger();
        QCOMPARE(search->text(), QString());
        QTRY_VERIFY(search->hasFocus());
        QVERIFY(!clearSearch->isVisible());
        QTRY_VERIFY(!results->isVisible());
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[2]);
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());
        QVERIFY(viewer->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_Escape);
        QTRY_VERIFY(!viewer->isVisible());

        // A pending image remains viewable, while the index panel exposes only
        // the work and recovery actions relevant to its stopped worker.
        options.directory = temporary.filePath("pending-prefix-history");
        options.ocr = true;
        options.deferredOcr = true;
        qint64 pendingId = 0;
        {
            replay::Recorder recorder(options);
            pendingId = recorder.addFrame(prefixScreen(lines[0], 4), start + 180000).frameId;
            recorder.finish();
        }
        auto pendingViewer = replay::createViewer(options.directory);
        pendingViewer->show();
        pendingViewer->activateWindow();
        QTRY_COMPARE(pendingViewer->property("displayedFrameId").toLongLong(), pendingId);
        QTest::mouseClick(pendingViewer->findChild<QPushButton*>("toggleDetails"), Qt::LeftButton);
        QVERIFY(pendingViewer->findChild<QWidget*>("detailsPanel")->isVisible());
        QVERIFY(!pendingViewer->findChild<QWidget*>("helpPanel")->isVisible());
        QVERIFY(pendingViewer->findChild<QPushButton*>("processMoment")->isVisible());
        QVERIFY(pendingViewer->findChild<QPushButton*>("catchUpIndexing")->isVisible());
        QVERIFY(pendingViewer->findChild<QPushButton*>("copyIndexCommand")->isVisible());
        QVERIFY(pendingViewer->findChild<QLabel*>("indexState")->isVisible());
        QVERIFY(pendingViewer->findChild<QLabel*>("indexWorkerHint")->text().contains("waiting"));
        QTest::qWait(60);
        QVERIFY(pendingViewer->grab().save("runs/design-review-v2/index-pending.png"));
        pendingViewer->close();
    }

    void copyMatchesRespectsFocusAndGeometry() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("copy-prefix-history");
        options.ocr = false;
        options.minFreeBytes = 0;
        const QString firstLine = "Continue the invoice review with Patrick.";
        const QString secondLine = "Continuous capture keeps moments available.";
        const QString unrelated = "An unrelated note must stay out of the match copy.";
        const QString fullText = "Picking up where we left off\n" + firstLine + '\n' + secondLine + '\n' + unrelated;
        const QString withoutGeometry = "Continuity remains searchable without stored positions.\nAnother unrelated note.";
        std::array<qint64, 2> ids{};
        {
            replay::Recorder recorder(options);
            QImage image = prefixScreen(firstLine, 1);
            {
                QPainter painter(&image);
                painter.fillRect(QRect(80, 330, 1120, 185), QColor("#111820"));
                painter.setFont(QFont("monospace", 15));
                painter.setPen(QColor("#ece8da"));
                painter.drawText(QRect(80, 344, 1120, 50), Qt::AlignVCenter, secondLine);
                painter.drawText(QRect(80, 454, 1120, 50), Qt::AlignVCenter, unrelated);
            }
            ids[0] = recorder.addFrame(image, 1000).frameId;
            ids[1] = recorder.addFrame(prefixScreen(withoutGeometry.section('\n', 0, 0), 2), 3000).frameId;
            recorder.finish();
        }
        QVERIFY(indexText(options.directory, ids[0], fullText, QJsonArray{
            QJsonArray{80, 130, 1120, 60, "Picking up where we left off"},
            QJsonArray{80, 240, 1120, 60, firstLine},
            QJsonArray{80, 344, 1120, 50, secondLine},
            QJsonArray{80, 454, 1120, 50, unrelated}}));
        QVERIFY(indexText(options.directory, ids[1], withoutGeometry));

        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* copyStatus = viewer->findChild<QLabel*>("copyStatus");
        QVERIFY(search && results && copyStatus);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[1]);
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "contin");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 2);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[0]);
        QTRY_COMPARE(viewer->property("highlightCount").toInt(), 2);
        QTest::keyClick(results, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());

        auto* clipboard = QApplication::clipboard();
        clipboard->setText("synthetic clipboard sentinel");
        QTest::keyClick(viewer.get(), Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(clipboard->text(), firstLine + '\n' + secondLine);
        QVERIFY(copyStatus->isVisible());
        QVERIFY(!copyStatus->text().isEmpty());
        QTest::keyClick(viewer.get(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(clipboard->text(), fullText);

        // The input retains ordinary text selection and copy behavior.
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        search->setSelection(0, 3);
        QTest::keyClick(search, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(clipboard->text(), QString("con"));
        QCOMPARE(search->text(), QString("contin"));
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(viewer->hasFocus());

        // Copy immediately after selecting another frame, before the worker's
        // queued completion is delivered, must not reuse the previous lines.
        clipboard->setText("keep while positions load");
        results->setCurrentRow(1);
        QCOMPARE(viewer->property("selectedFrameId").toLongLong(), ids[1]);
        QKeyEvent pendingCopy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(viewer.get(), &pendingCopy);
        QCOMPARE(clipboard->text(), QString("keep while positions load"));
        QVERIFY(copyStatus->isVisible());
        QVERIFY(!copyStatus->text().isEmpty());

        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[1]);
        QTRY_COMPARE(viewer->property("highlightCount").toInt(), 0);
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QTRY_VERIFY(viewer->findChild<QLabel*>("indexState")->toolTip().startsWith("No matching text positions"));
        clipboard->setText("keep when positions are unavailable");
        QTest::keyClick(viewer.get(), Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(clipboard->text(), QString("keep when positions are unavailable"));
        QVERIFY(copyStatus->isVisible());
        QVERIFY(copyStatus->text().contains("unavailable", Qt::CaseInsensitive));
        QTest::keyClick(viewer.get(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(clipboard->text(), withoutGeometry);

        search->clear();
        QTRY_VERIFY(!results->isVisible());
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        clipboard->setText("copy all without a query");
        QTest::keyClick(viewer.get(), Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(clipboard->text(), withoutGeometry);
        viewer->close();
    }

    void prefixMatchesPageAndSelectFromTimeline() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("paged-prefix-history");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            for (int i = 0; i < 225; ++i) {
                QImage image(64, 64, QImage::Format_RGBA8888);
                image.fill(QColor(30 + i, 80, 120));
                recorder.addFrame(image, 1000 + i * 2000);
            }
            recorder.finish();
        }
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        QCOMPARE(sqlite3_exec(database,
            "UPDATE frames SET text='continuous timeline fixture',ocr_state='ready';"
            "UPDATE frames SET text=text || ' uniqueneedle' WHERE id=17;"
            "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames", nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* timeline = viewer->findChild<QSlider*>("recallTimeline");
        QVERIFY(search && results && timeline);
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "contin");
        QTRY_COMPARE(results->count(), 100);
        QTRY_COMPARE(viewer->property("totalMatches").toLongLong(), qint64(225));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(1));
        QCOMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(0));
        QCOMPARE(timeline->property("matchMarkerCount").toInt(), 225);
        results->setFocus();
        QTRY_VERIFY(viewer->findChild<QPushButton*>("laterMoment")->isEnabled());
        QTest::keyClick(results, Qt::Key_Right);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));
        QCOMPARE(results->currentRow(), 1);
        QTest::keyClick(results, Qt::Key_Down);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(3));
        results->setCurrentRow(99);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(100));
        // Repeated input while a page read is queued must not skip a page or
        // restore an obsolete selection from the still-visible old page.
        QTest::keyClick(results, Qt::Key_Down);
        QTest::keyClick(results, Qt::Key_Down);
        QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(100));
        QTRY_COMPARE(results->count(), 100);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(101));
        QTest::keyClick(results, Qt::Key_Up);
        QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(0));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(100));
        QTest::keyClick(results, Qt::Key_PageDown);
        QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(100));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(101));
        QTest::keyClick(results, Qt::Key_PageUp);
        QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(0));

        // The final timeline hit is outside the loaded result page. Clicking
        // it loads that result's page and selects the same exact moment.
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(timeline->width() - 12, 19));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(225));
        QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(200));
        QCOMPARE(results->currentItem()->data(Qt::UserRole).toLongLong(), qint64(225));
        QVERIFY(viewer->findChild<QLabel*>("resultsHeading")->text().startsWith("225 / 225"));
        QCOMPARE(search->text(), "contin");

        for (const auto repeatKey : {Qt::Key_Return, Qt::Key_F5}) {
            QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
            search->setText("continuous");
            QTest::keyClick(search, Qt::Key_Return);
            QTRY_COMPARE(viewer->property("totalMatches").toLongLong(), qint64(225));
            QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(0));
            QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(1));
            QTest::keyClick(results, Qt::Key_PageDown);
            QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(100));
            QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(101));

            // The new query starts at zero while the old query's second page
            // is still displayed. A repeated submission/refresh before the
            // completion is delivered must preserve the pending query's page.
            QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
            search->setText("uniqueneedle");
            QTest::keyClick(search, Qt::Key_Return);
            QVERIFY(viewer->property("historyLoading").toBool());
            QTest::keyClick(repeatKey == Qt::Key_Return ? search : viewer.get(), repeatKey);
            QTRY_COMPARE(viewer->property("totalMatches").toLongLong(), qint64(1));
            QTRY_COMPARE(viewer->property("matchPageOffset").toLongLong(), qint64(0));
            QTRY_COMPARE(results->count(), 1);
            QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(17));
            QCOMPARE(results->currentItem()->data(Qt::UserRole).toLongLong(), qint64(17));
            QCOMPARE(search->text(), "uniqueneedle");
        }
        viewer->close();
    }

    void pendingImagesAndBackgroundRefresh() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("pending-history");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        replay::Recorder recorder(options);
        const auto first = recorder.addFrame(replay::fixtureFrame(0), 1000);
        const auto second = recorder.addFrame(replay::fixtureFrame(2), 3000);

        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* indexState = viewer->findChild<QLabel*>("indexState");
        auto* status = viewer->findChild<QLabel*>("recallStatus");
        auto* refresh = viewer->findChild<QPushButton*>("refreshHistory");
        QVERIFY(search && results && indexState && status && refresh);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), second.frameId);
        QCOMPARE(results->count(), 2);
        QVERIFY(indexState->text().contains("pending", Qt::CaseInsensitive));

        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "Patrick");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 0);
        QVERIFY(status->text().contains("pending", Qt::CaseInsensitive));
        QVERIFY(viewer->findChild<QLabel*>("mediaStatus")->text().contains("not searchable", Qt::CaseInsensitive));

        replay::IndexerOptions indexing;
        indexing.directory = options.directory;
        indexing.ocrMode = "full";
        replay::Indexer indexer(indexing);
        QCOMPARE(indexer.processNext().state, "ready");
        QTest::keyClick(viewer.get(), Qt::Key_F5);
        QTRY_COMPARE(results->count(), 1);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), first.frameId);

        results->setFocus();
        QTRY_VERIFY(viewer->findChild<QPushButton*>("laterMoment")->isEnabled());
        QTest::keyClick(results, Qt::Key_Right);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), second.frameId);
        QVERIFY(indexState->text().contains("pending", Qt::CaseInsensitive));
        QCOMPARE(indexer.processNext().state, "ready");

        // Refresh keeps the viewed moment in place and selects its newly
        // searchable result instead of retaining an unrelated match cursor.
        QTRY_COMPARE_WITH_TIMEOUT(results->count(), 2, 5000);
        QCOMPARE(search->text(), "Patrick");
        QCOMPARE(results->currentItem()->data(Qt::UserRole).toLongLong(), second.frameId);
        QCOMPARE(viewer->property("selectedFrameId").toLongLong(), second.frameId);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), second.frameId);
        QVERIFY(indexState->text().isEmpty());
        QVERIFY(!indexState->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_F5);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), second.frameId);
        recorder.finish();
        viewer->close();
    }

    void newerSelectionCancelsTimelineSeek() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("seek-order");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            for (int i = 0; i < 4; ++i) {
                QImage image(64, 64, QImage::Format_RGBA8888);
                image.fill(QColor(40 + i * 40, 80, 120));
                recorder.addFrame(image, 1000 + i * 2000);
            }
            recorder.finish();
        }
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(database, sqlite3_close);
        // DELETE mode lets this fixture hold reads while a seek is in flight.
        // Nothing outside this temporary dataset is changed.
        QCOMPARE(sqlite3_exec(database,
            "PRAGMA journal_mode=DELETE;"
            "UPDATE frames SET text='timeline fixture',ocr_state='ready';"
            "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames", nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_busy_timeout(database, 1000);
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* timeline = viewer->findChild<QSlider*>("recallTimeline");
        QVERIFY(search && results && timeline);
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "timeline");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 4);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(1));

        // Both actions occur before the scrub debounce fires. The later result
        // must win even after enough time passes for an uncanceled seek to finish.
        timeline->setValue(timeline->maximum());
        results->setCurrentRow(1);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));
        QTest::qWait(150);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));

        results->setCurrentRow(0);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(1));
        QTRY_VERIFY(viewer->findChild<QPushButton*>("laterMoment")->isEnabled());
        timeline->setValue(timeline->maximum());
        QTest::keyClick(results, Qt::Key_Right);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));
        QTest::qWait(150);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));

        // A database read that has already started also cannot overwrite a
        // newer card choice when its stale completion finally arrives.
        results->setCurrentRow(1);
        results->setCurrentRow(0);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(1));
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QCOMPARE(sqlite3_exec(database, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr), SQLITE_OK);
        timeline->setValue(timeline->maximum());
        QTest::qWait(100); // Past the 35 ms debounce; the SQLite read is blocked.
        results->setCurrentRow(1);
        QCOMPARE(viewer->property("selectedFrameId").toLongLong(), qint64(2));
        QCOMPARE(sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr), SQLITE_OK);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));
        QTest::qWait(200);
        QCOMPARE(viewer->property("selectedFrameId").toLongLong(), qint64(2));
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(2));
        viewer->close();
    }

    void horizontalResultsStayPutAcrossRefresh() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("scroll-history");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            for (int i = 0; i < 30; ++i) {
                QImage image(64, 64, QImage::Format_RGBA8888);
                image.fill(QColor(20 + i * 7, 80, 120));
                recorder.addFrame(image, 1000 + i * 2000);
            }
            recorder.finish();
        }
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(database, sqlite3_close);
        QCOMPARE(sqlite3_exec(database,
            "UPDATE frames SET text='timeline fixture',ocr_state='ready';"
            "INSERT INTO frame_text(rowid,text) SELECT id,text FROM frames", nullptr, nullptr, nullptr), SQLITE_OK);
        auto viewer = replay::createViewer(options.directory);
        viewer->resize(900, 620);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        QVERIFY(search && results);
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "timeline");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 30);
        results->setCurrentRow(18);
        results->scrollToItem(results->currentItem(), QAbstractItemView::PositionAtCenter);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(19));
        auto* scroll = results->horizontalScrollBar();
        QTRY_VERIFY(scroll->maximum() > 1000);
        const int position = scroll->value();
        QVERIFY(position > 0);
        const qint64 selectedId = results->currentItem()->data(Qt::UserRole).toLongLong();
        QTest::keyClick(viewer.get(), Qt::Key_F5);
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QCOMPARE(scroll->value(), position);
        QCOMPARE(results->currentItem()->data(Qt::UserRole).toLongLong(), selectedId);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), selectedId);

        // Remove a later search hit so result count proves the automatic refresh
        // actually ran. Earlier card positions and the user's viewport remain.
        QCOMPARE(sqlite3_exec(database,
            "UPDATE frames SET text='different fixture' WHERE id=30;"
            "UPDATE frame_text SET text='different fixture' WHERE rowid=30", nullptr, nullptr, nullptr), SQLITE_OK);
        QTRY_COMPARE_WITH_TIMEOUT(results->count(), 29, 5000);
        QCOMPARE(scroll->value(), position);
        QCOMPARE(results->currentItem()->data(Qt::UserRole).toLongLong(), selectedId);
        QCOMPARE(viewer->property("displayedFrameId").toLongLong(), selectedId);
        viewer->close();
    }

    void disabledTextIsNotClaimedRecognized() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("disabled-history");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            recorder.addFrame(replay::fixtureFrame(0), 1000);
            recorder.finish();
        }
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        auto* indexState = viewer->findChild<QLabel*>("indexState");
        auto* status = viewer->findChild<QLabel*>("recallStatus");
        QVERIFY(indexState && status);
        QTRY_VERIFY(indexState->text().contains("disabled", Qt::CaseInsensitive));
        QVERIFY(status->text().contains("disabled", Qt::CaseInsensitive));
        QVERIFY(!indexState->text().contains("no text was recognized", Qt::CaseInsensitive));
        viewer->close();
    }

    void servicePauseAndStopAreVisibleAndKeyboardAccessible() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("service-history");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            recorder.addFrame(prefixScreen("Saved while indexing waits", 1),
                QDateTime::currentMSecsSinceEpoch() - 7 * 60000);
            recorder.finish();
        }
        const QJsonObject policy{{"scheduler", "fixed"}, {"ocr_mode", "incremental"},
            {"ocr_cpu_percent", 10}, {"ocr_max_wall_ms", 60000}};
        replay::controlIndexService(options.directory, "pause", policy);
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        QTRY_VERIFY(viewer->property("displayedFrameId").toLongLong() > 0);
        auto* action = viewer->findChild<QPushButton*>("indexServiceAction");
        auto* stop = viewer->findChild<QPushButton*>("stopIndexService");
        auto* hint = viewer->findChild<QLabel*>("indexWorkerHint");
        QVERIFY(action && stop && hint);
        QCOMPARE(action->text(), "Resume");
        QVERIFY(hint->text().contains("Paused"));
        QVERIFY(hint->text().contains("7 min"));
        QVERIFY(!viewer->findChild<QPushButton*>("catchUpIndexing")->isEnabled());
        QVERIFY(!viewer->findChild<QPushButton*>("copyIndexCommand")->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_Escape);
        QTest::keyClick(viewer.get(), Qt::Key_I);
        QVERIFY(action->isVisible());
        QVERIFY(stop->isVisible());
        QVERIFY(QDir().mkpath("runs/design-review-service"));
        QVERIFY(viewer->grab().save("runs/design-review-service/paused-desktop.png"));
        viewer->resize(900, 620);
        QTest::qWait(60);
        QVERIFY(viewer->grab().save("runs/design-review-service/paused-compact.png"));
        action->setFocus();
        QTest::keyClick(action, Qt::Key_Tab);
        QTRY_VERIFY(stop->hasFocus());
        QTest::keyClick(stop, Qt::Key_Space);
        QTRY_VERIFY(!replay::indexServiceStatus(options.directory).value("enabled").toBool());
        QTRY_VERIFY(!viewer->property("serviceRequestInFlight").toBool());
        QVERIFY(!replay::indexServiceStatus(options.directory).value("running").toBool());
        viewer->close();
        // Reopening history alone must not undo the saved pause or start work.
        auto reopened = replay::createViewer(options.directory);
        reopened->show();
        QTRY_VERIFY(reopened->property("displayedFrameId").toLongLong() > 0);
        QVERIFY(!replay::indexServiceStatus(options.directory).value("running").toBool());
        QCOMPARE(replay::indexingStatus(options.directory).value("pending").toInteger(), qint64(1));
        reopened->close();
    }

    void brokenServiceSettingsDoNotBlockHistoryOrStop() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("invalid-service-policy");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            recorder.addFrame(prefixScreen("Saved image remains usable", 1), 1000);
            recorder.finish();
        }
        QFile state(QDir(options.directory).filePath(".index-service.json"));
        QVERIFY(state.open(QIODevice::WriteOnly));
        QVERIFY(state.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const QJsonObject policy{{"scheduler", "fixed"}, {"ocr_data_path", temporary.filePath("removed-model")}};
        QVERIFY(state.write(QJsonDocument(QJsonObject{{"enabled", true}, {"policy", policy}}).toJson()) > 0);
        state.close();
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        QTRY_VERIFY(viewer->property("displayedFrameId").toLongLong() > 0);
        auto* hint = viewer->findChild<QLabel*>("indexWorkerHint");
        QTRY_VERIFY(hint->text().contains("needs attention"));
        QTest::keyClick(viewer.get(), Qt::Key_Escape);
        QTest::keyClick(viewer.get(), Qt::Key_I);
        auto* stop = viewer->findChild<QPushButton*>("stopIndexService");
        QVERIFY(stop->isVisible());
        stop->setFocus();
        QTest::keyClick(stop, Qt::Key_Space);
        QTRY_VERIFY(!replay::indexServiceStatus(options.directory).value("enabled").toBool());
        QTRY_VERIFY(!viewer->property("serviceRequestInFlight").toBool());
        QVERIFY(viewer->property("displayedFrameId").toLongLong() > 0);
        viewer->close();
    }

    void dwellAndKeyboardRequestsPersistWithoutStartingWorker() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("priority-history");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        std::array<qint64, 4> ids{};
        {
            replay::Recorder recorder(options);
            const std::array<int, 4> scenes{0, 2, 4, 5};
            for (int i = 0; i < 4; ++i)
                ids[i] = recorder.addFrame(replay::fixtureFrame(scenes[i], QSize(640, 360)), 1000 + i * 60000).frameId;
            recorder.finish();
        }
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* process = viewer->findChild<QPushButton*>("processMoment");
        auto* catchUp = viewer->findChild<QPushButton*>("catchUpIndexing");
        auto* hint = viewer->findChild<QLabel*>("indexWorkerHint");
        QVERIFY(results && process && catchUp && hint);
        QTRY_COMPARE(results->count(), 4);
        // Rapid scrubbing should request only the final selected moment after
        // its dwell, not every pending image passed along the way.
        results->setFocus();
        results->setCurrentRow(1);
        QTest::keyClick(results, Qt::Key_Return);
        results->setCurrentRow(2);
        QTest::keyClick(results, Qt::Key_Return);
        QCOMPARE(replay::indexingStatus(options.directory)["priority_pending"].toInteger(), qint64(0));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), ids[2]);
        QTRY_COMPARE(replay::indexingStatus(options.directory)["priority_pending"].toInteger(), qint64(1));
        QTRY_VERIFY(!viewer->property("indexingRequestInFlight").toBool());
        QCOMPARE(storedNumber(options.directory, "SELECT frame_id FROM index_requests ORDER BY request_order DESC,rank LIMIT 1"), ids[2]);
        QCOMPARE(storedNumber(options.directory, "SELECT rank FROM index_requests ORDER BY request_order DESC,rank LIMIT 1"), qint64(0));
        QVERIFY(storedNumber(options.directory, "SELECT expires_ms FROM index_requests ORDER BY request_order DESC,rank LIMIT 1") > QDateTime::currentMSecsSinceEpoch());
        QCOMPARE(storedNumber(options.directory, "SELECT COUNT(*) FROM index_requests"), qint64(1));
        const qint64 order = storedNumber(options.directory, "SELECT request_order FROM index_schedule WHERE id=1");
        QTest::keyClick(viewer.get(), Qt::Key_F5);
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QTest::qWait(2200); // Includes a regular refresh; it must not renew the request.
        QCOMPARE(storedNumber(options.directory, "SELECT request_order FROM index_schedule WHERE id=1"), order);
        QCOMPARE(viewer->property("selectedFrameId").toLongLong(), ids[2]);
        QVERIFY(hint->text().contains("indexing is stopped", Qt::CaseInsensitive));
        QVERIFY(hint->text().contains("waiting", Qt::CaseInsensitive));
        QTest::mouseClick(viewer->findChild<QPushButton*>("toggleDetails"), Qt::LeftButton);
        QVERIFY(viewer->findChild<QPushButton*>("copyIndexCommand")->isVisible());

        results->setCurrentRow(3);
        QTest::keyClick(results, Qt::Key_Return);
        QTest::keyClick(results, Qt::Key_P);
        QTRY_COMPARE(replay::indexingStatus(options.directory)["priority_pending"].toInteger(), qint64(2));
        QTRY_VERIFY(!viewer->property("indexingRequestInFlight").toBool());
        QCOMPARE(storedNumber(options.directory, "SELECT frame_id FROM index_requests ORDER BY request_order DESC,rank LIMIT 1"), ids[3]);

        QTRY_VERIFY(catchUp->isEnabled());
        const qint64 requestedAt = QDateTime::currentMSecsSinceEpoch();
        QTest::keyClick(results, Qt::Key_C);
        QTRY_VERIFY(replay::indexingStatus(options.directory)["catch_up_until_ms"].toInteger() > requestedAt);
        const qint64 until = replay::indexingStatus(options.directory)["catch_up_until_ms"].toInteger();
        QVERIFY(until >= requestedAt + 119000 && until <= requestedAt + 122000);
        QTRY_VERIFY(!viewer->property("indexingRequestInFlight").toBool());
        QTRY_VERIFY(!catchUp->isEnabled());
        QTest::keyClick(results, Qt::Key_C);
        QCOMPARE(replay::indexingStatus(options.directory)["catch_up_until_ms"].toInteger(), until);
        QCOMPARE(replay::indexingStatus(options.directory)["ready"].toInteger(), qint64(0));
        QCOMPARE(replay::indexingStatus(options.directory)["pending"].toInteger(), qint64(4));
        QVERIFY(!replay::indexingStatus(options.directory)["indexer_running"].toBool());
        {
            replay::IndexerOptions indexing;
            indexing.directory = options.directory;
            replay::Indexer worker(indexing); // Holds the worker lock; no OCR is run.
            replay::publishIndexWorkerPolicy(options.directory, {{"mode", "pressure"}, {"effective_cpu_percent", 10}});
            QTest::keyClick(viewer.get(), Qt::Key_F5);
            QTRY_VERIFY(hint->text().startsWith("Another worker is indexing", Qt::CaseInsensitive));
            QVERIFY(hint->text().contains("10% of one core"));
            QTRY_VERIFY(!viewer->findChild<QPushButton*>("copyIndexCommand")->isVisible());
            QCOMPARE(viewer->property("selectedFrameId").toLongLong(), ids[3]);
        }
        viewer->close();
    }

    void failedPriorityWriteCanBeRetried() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("retry-priority");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        qint64 frameId = 0;
        {
            replay::Recorder recorder(options);
            frameId = recorder.addFrame(replay::fixtureFrame(0, QSize(640, 360)), 1000).frameId;
            recorder.finish();
        }
        // Initialize request tables, then reject writes without damaging the
        // saved image. This tests UI recovery independently of OCR timing.
        QCOMPARE(replay::requestCatchUp(options.directory, 1), 1);
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        QCOMPARE(sqlite3_exec(database,
            "CREATE TRIGGER reject_priority BEFORE INSERT ON index_requests BEGIN SELECT RAISE(ABORT,'synthetic request failure'); END",
            nullptr, nullptr, nullptr), SQLITE_OK);
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* message = viewer->findChild<QLabel*>("indexingRequestStatus");
        QTRY_COMPARE(results->count(), 1);
        results->setFocus();
        QTest::keyClick(results, Qt::Key_P);
        QTRY_VERIFY(message->text().contains("Unable to queue", Qt::CaseInsensitive));
        QTRY_VERIFY(!viewer->property("indexingRequestInFlight").toBool());
        QCOMPARE(replay::indexingStatus(options.directory)["priority_pending"].toInteger(), qint64(0));
        QCOMPARE(sqlite3_exec(database, "DROP TRIGGER reject_priority", nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);
        QTest::keyClick(results, Qt::Key_P);
        QTRY_COMPARE(replay::indexingStatus(options.directory)["priority_pending"].toInteger(), qint64(1));
        QTRY_VERIFY(!viewer->property("indexingRequestInFlight").toBool());
        QTRY_VERIFY(!message->text().contains("Unable to queue", Qt::CaseInsensitive));
        QTRY_COMPARE(storedNumber(options.directory, "SELECT frame_id FROM index_requests WHERE rank=0"), frameId);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), frameId);
        viewer->close();
    }

    void closeSettlesBlockedRequestWithoutLateWrite() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("close-pending-request");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            recorder.addFrame(replay::fixtureFrame(0, QSize(640, 360)), 1000);
            recorder.finish();
        }
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        QTRY_COMPARE(results->count(), 1);
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(database, sqlite3_close);
        QCOMPARE(sqlite3_exec(database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr), SQLITE_OK);
        results->setFocus();
        QTest::keyClick(results, Qt::Key_P);
        QVERIFY(viewer->property("indexingRequestInFlight").toBool());
        QTest::qWait(100);
        QElapsedTimer elapsed;
        elapsed.start();
        viewer->close();
        QVERIFY(elapsed.elapsed() < 2500);
        QVERIFY(!viewer->isVisible());
        QVERIFY(!viewer->property("indexingRequestInFlight").toBool());
        QVERIFY(!viewer->property("historyLoading").toBool());
        QCOMPARE(sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr), SQLITE_OK);
        QTest::qWait(100);
        QCOMPARE(storedNumber(options.directory, "SELECT COUNT(*) FROM index_requests"), qint64(0));
    }

    void closeCancelsAndReapsSlowImageDecoder() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("close-decoder");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            recorder.addFrame(replay::fixtureFrame(0, QSize(640, 360)), 1000);
            recorder.finish();
        }
        // Use an owned sleeping executable instead of a real video decoder.
        // Only the synthetic row's codec changes; no desktop pixels are read.
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        QCOMPARE(sqlite3_exec(database, "UPDATE frames SET codec='h264'", nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);
        const QString binaryDirectory = temporary.filePath("bin");
        QVERIFY(QDir().mkpath(binaryDirectory));
        QFile decoder(QDir(binaryDirectory).filePath("ffmpeg"));
        QVERIFY(decoder.open(QIODevice::WriteOnly));
        QVERIFY(decoder.write("#!/bin/sh\nprintf '%s' \"$$\" > \"$REPLAY_TEST_DECODER_PID\"\nexec sleep 30\n") > 0);
        decoder.close();
        QVERIFY(decoder.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        struct RestoreEnvironment {
            QByteArray path = qgetenv("PATH");
            QByteArray marker = qgetenv("REPLAY_TEST_DECODER_PID");
            bool hadMarker = qEnvironmentVariableIsSet("REPLAY_TEST_DECODER_PID");
            ~RestoreEnvironment() {
                qputenv("PATH", path);
                if (hadMarker) qputenv("REPLAY_TEST_DECODER_PID", marker);
                else qunsetenv("REPLAY_TEST_DECODER_PID");
            }
        } restore;
        const QString marker = temporary.filePath("decoder.pid");
        qputenv("PATH", binaryDirectory.toUtf8() + ':' + restore.path);
        qputenv("REPLAY_TEST_DECODER_PID", marker.toUtf8());
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        QTRY_VERIFY(QFileInfo::exists(marker));
        QTRY_VERIFY(QFileInfo(marker).size() > 0);
        QFile pidFile(marker);
        QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const auto pid = pidFile.readAll().toLongLong();
        QVERIFY(pid > 1);
        QElapsedTimer elapsed;
        elapsed.start();
        viewer->close();
        QVERIFY(elapsed.elapsed() < 2500);
        QVERIFY(!viewer->property("mediaLoading").toBool());
        QVERIFY(::kill(pid_t(pid), 0) == -1 && errno == ESRCH);
    }

    void failedIndexExplainsEmptySearchAndKeepsImagesBrowsable() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("failed-history");
        options.deferredOcr = true;
        options.minFreeBytes = 0;
        qint64 frameId = 0;
        {
            replay::Recorder recorder(options);
            frameId = recorder.addFrame(replay::fixtureFrame(0), 1000).frameId;
            recorder.finish();
        }
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* message = viewer->findChild<QLabel*>("mediaStatus");
        QVERIFY(search && results && message);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), frameId);
        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(search, "Patrick");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 0);
        QVERIFY(message->text().contains("pending", Qt::CaseInsensitive));

        // Inject a worker failure in this synthetic dataset without making the
        // GUI test depend on OCR speed or an intentional wall-clock timeout.
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        QCOMPARE(sqlite3_exec(database,
            "UPDATE frames SET ocr_state='failed',ocr_error='Synthetic deadline failure' WHERE ocr_state='pending'",
            nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);
        QTest::keyClick(viewer.get(), Qt::Key_F5);
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QCOMPARE(results->count(), 0);
        QVERIFY(message->text().contains("indexing failed", Qt::CaseInsensitive));
        QVERIFY(message->text().contains("Clear the search", Qt::CaseInsensitive));
        QVERIFY(message->text().contains("browse", Qt::CaseInsensitive));
        QVERIFY(!message->text().contains("spelling", Qt::CaseInsensitive));
        QVERIFY(!message->text().contains("shorter word", Qt::CaseInsensitive));

        QTest::keyClick(viewer.get(), Qt::Key_F, Qt::ControlModifier);
        QTest::keyClick(search, Qt::Key_Backspace);
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(results->count(), 1);
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), frameId);
        QVERIFY(viewer->findChild<QLabel*>("indexState")->text().contains("failed", Qt::CaseInsensitive));
        viewer->close();
    }

    void legacyEmptyTextKeepsUnknownStatus() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        replay::RecorderOptions options;
        options.directory = temporary.filePath("legacy-history");
        options.ocr = false;
        options.minFreeBytes = 0;
        {
            replay::Recorder recorder(options);
            recorder.addFrame(replay::fixtureFrame(0), 1000);
            recorder.finish();
        }
        // Reproduce the old reader schema, which had no indexing-state fields.
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QDir(options.directory).filePath("index.sqlite").toUtf8().constData(), &database), SQLITE_OK);
        const char* legacy =
            "ALTER TABLE frames RENAME TO new_frames;"
            "CREATE TABLE frames AS SELECT id,timestamp_ms,last_timestamp_ms,observation_count,segment_id,"
            "frame_index,path,codec,width,height,text FROM new_frames;"
            "DROP TABLE new_frames;"
            "UPDATE metadata SET value='1' WHERE key='schema_version';";
        QCOMPARE(sqlite3_exec(database, legacy, nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);
        auto viewer = replay::createViewer(options.directory);
        viewer->show();
        auto* indexState = viewer->findChild<QLabel*>("indexState");
        QVERIFY(indexState);
        QTRY_VERIFY(indexState->text().contains("not recorded", Qt::CaseInsensitive));
        QVERIFY(!indexState->text().contains("no text was recognized", Qt::CaseInsensitive));
        QTRY_COMPARE(viewer->property("displayedFrameId").toLongLong(), qint64(1));
        viewer->close();
    }
};

QTEST_MAIN(ViewerTest)
#include "viewer_test.moc"
