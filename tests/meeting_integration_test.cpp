#include "fixture.h"
#include "meeting_index.h"
#include "meeting_view.h"
#include "recorder.h"
#include "replay_config.h"
#include "viewer.h"
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <sqlite3.h>

class MeetingIntegrationTest final : public QObject {
    Q_OBJECT
    static void write(const QString& path, const QByteArray& data) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    static void source(const QString& root, const QString& name, qint64 start, bool imported = false) {
        const auto directory = root + '/' + name; QVERIFY(QDir().mkpath(directory));
        QJsonObject manifest{{"app", "omarchy-meeting-recorder"}, {"version", 1}, {"title", name},
            {"started_at", start / 1000}, {"duration_secs", 1800}};
        if (imported) manifest["imported"] = "phone-memo.ogg";
        write(directory + "/meeting.meeting-recorder", QJsonDocument(manifest).toJson());
        write(directory + "/transcript.md", QByteArray("# ") + name.toUtf8() +
            "\n\n## Transcript\n\n**[00:03] Patrick:** Continue with the smaller invoice.\n\n"
            "**[00:21] You:** The garden plan can wait until tomorrow.\n\n"
            "**[01:04] Patrick:** Continuity matters; keep the invoice decision in these notes.\n");
    }
    static void screenText(const QString& history, qint64 id) {
        sqlite3* db = nullptr; QCOMPARE(sqlite3_open(QFile::encodeName(history + "/index.sqlite").constData(), &db), SQLITE_OK);
        for (const auto& sql : {QString("UPDATE frames SET text='Continue the invoice',ocr_state='ready' WHERE id=%1").arg(id),
                QString("INSERT OR REPLACE INTO frame_text(rowid,text) VALUES(%1,'Continue the invoice')").arg(id)})
            QCOMPARE(sqlite3_exec(db, sql.toUtf8().constData(), nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(db);
    }
    static void settle(replay::MeetingImporter& importer, qint64 now) {
        // Importer requires old-enough source files across two stable scans.
        now = std::max(now, QDateTime::currentMSecsSinceEpoch() + 10000);
        for (int pass = 0; pass < 2; ++pass) {
            int limit = 400;
            while (importer.sync(now, 0).more && --limit) {}
            QVERIFY(limit > 0);
        }
    }
private slots:
    void groupedSearchFiltersTimelineAndUnavailableScreens() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto history = temporary.filePath("history"), meetings = temporary.filePath("meetings");
        const qint64 start = (QDateTime::currentMSecsSinceEpoch() / 1000 - 300) * 1000;
        replay::RecorderOptions options; options.directory = history; options.ocr = false; options.minFreeBytes = 0;
        qint64 frameId;
        { replay::Recorder recorder(options); frameId = recorder.addFrame(replay::fixtureFrame(0), start).frameId; recorder.finish(); }
        screenText(history, frameId);
        source(meetings, "Budget review", start);
        source(meetings, "Earlier discussion", start - 3600000);
        source(meetings, "Imported memo", start, true);
        replay::MeetingImporter importer(history, meetings); settle(importer, start + 300000);
        auto viewer = replay::createViewer(history); viewer->show(); viewer->activateWindow();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* filter = viewer->findChild<QComboBox*>("recallSourceFilter");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        auto* transcript = viewer->findChild<QPlainTextEdit*>("meetingTranscript");
        auto* timeline = viewer->findChild<QSlider*>("recallTimeline");
        QVERIFY(search && filter && results && transcript && timeline);
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        QTRY_COMPARE(viewer->property("meetingCount").toInt(), 3);
        // The initial screen preview must not select an unrelated meeting card.
        QCOMPARE(viewer->property("selectedFrameId").toLongLong(), frameId);
        QCOMPARE(results->currentRow(), -1);
        QVERIFY(filter->isVisible()); QCOMPARE(timeline->property("meetingMarkerCount").toInt(), 2);
        QTest::keyClick(viewer.get(), Qt::Key_Down);
        QTRY_VERIFY(viewer->property("selectedMeetingId").toLongLong() > 0);
        QCOMPARE(results->currentRow(), 0);
        search->setText("contin");
        QTRY_COMPARE(viewer->property("totalMatches").toInt(), 4);
        QTRY_VERIFY(viewer->property("selectedMeetingId").toLongLong() > 0);
        QTRY_VERIFY(!viewer->property("meetingLoading").toBool());
        QCOMPARE(results->count(), 4); QVERIFY(results->item(0)->text().contains("2 matches"));
        QVERIFY(transcript->toPlainText().contains("Continuity"));
        QVERIFY(!viewer->findChild<QPushButton*>("fitImage")->isVisible());
        QVERIFY(viewer->findChild<QLabel*>("keyboardHelp")->text().contains("[ ] passages"));
        // Selected search results open immediately, with independent passage keys.
        search->setFocus(); QTest::keyClick(search, Qt::Key_Escape); QVERIFY(viewer->isVisible());
        QTest::keyClick(viewer.get(), Qt::Key_BracketRight);
        QVERIFY(viewer->findChild<QLabel*>("meetingPassagePosition")->text().startsWith("2 /"));
        const qint64 selected = viewer->property("selectedMeetingId").toLongLong();
        QTest::keyClick(viewer.get(), Qt::Key_Down);
        QTRY_VERIFY(viewer->property("selectedMeetingId").toLongLong() != selected);

        filter->setCurrentIndex(1);
        QTRY_COMPARE(viewer->property("totalMatches").toInt(), 1);
        QTRY_COMPARE(viewer->property("selectedFrameId").toLongLong(), frameId);
        QCOMPARE(timeline->property("meetingMarkerCount").toInt(), 0);
        QVERIFY(!transcript->isVisible());
        QVERIFY(viewer->findChild<QPushButton*>("fitImage")->isVisible());
        filter->setCurrentIndex(2);
        QTRY_COMPARE(viewer->property("totalMatches").toInt(), 3);
        QTRY_VERIFY(!viewer->property("meetingLoading").toBool());
        QCOMPARE(results->count(), 3);
        search->clear();
        QTRY_VERIFY(results->item(0)->text().contains(" · Meeting"));
        QTRY_COMPARE(results->count(), 3);
        auto choose = [&](const QString& title) {
            for (int row = 0; row < results->count(); ++row)
                if (results->item(row)->text().startsWith(title)) { results->setCurrentRow(row); return; }
            QFAIL("Meeting missing from grouped list");
        };
        choose("Imported memo"); QTRY_COMPARE(viewer->findChild<QLabel*>("meetingTitle")->text(), "Imported memo");
        QVERIFY(!viewer->findChild<QPushButton*>("browseMeetingScreens")->isEnabled());
        choose("Earlier discussion"); QTRY_COMPARE(viewer->findChild<QLabel*>("meetingTitle")->text(), "Earlier discussion");
        QTest::mouseClick(viewer->findChild<QPushButton*>("browseMeetingScreens"), Qt::LeftButton);
        QTRY_VERIFY(viewer->findChild<QLabel*>("mediaStatus")->text().contains("No screen history"));
        QVERIFY(transcript->isVisible());
        choose("Budget review"); QTRY_COMPARE(viewer->findChild<QLabel*>("meetingTitle")->text(), "Budget review");
        QVERIFY(viewer->findChild<QLabel*>("recordedTimestamp")->text().endsWith("Meeting start"));
        const QString proof = qEnvironmentVariable("REPLAY_MEETING_PROOF_DIR");
        if (!proof.isEmpty()) {
            QVERIFY(QDir().mkpath(proof));
            search->setText("contin"); QTRY_VERIFY(results->item(0)->text().contains("2 matches"));
            choose("Budget review"); QTRY_COMPARE(viewer->findChild<QLabel*>("meetingTitle")->text(), "Budget review");
            QTRY_VERIFY(viewer->findChild<QLabel*>("meetingPassagePosition")->text().contains("2 passages"));
            viewer->resize(1280, 860); QTest::qWait(100);
            QVERIFY(viewer->grab().save(proof + "/meeting-desktop.png"));
            viewer->resize(720, 640); QTest::qWait(100);
            QVERIFY(viewer->grab().save(proof + "/meeting-compact.png"));
        }
        QTest::mouseClick(viewer->findChild<QPushButton*>("browseMeetingScreens"), Qt::LeftButton);
        QTRY_COMPARE(viewer->property("selectedFrameId").toLongLong(), frameId);
        QVERIFY(!transcript->isVisible());
        // Start marker opens a meeting even with the query cleared.
        search->clear(); filter->setCurrentIndex(0);
        QTRY_VERIFY(results->item(0)->text().contains(" · Meeting"));
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(timeline->width()-12, 7));
        QTRY_VERIFY(viewer->property("selectedMeetingId").toLongLong() > 0);
        QCOMPARE(results->currentItem()->data(Qt::UserRole).toLongLong(), -viewer->property("selectedMeetingId").toLongLong());
        viewer->close();
    }

    void meetingPagesDoNotLoseScreenMatches() {
        QTemporaryDir temporary;
        const auto history = temporary.filePath("history"), meetings = temporary.filePath("meetings");
        const qint64 start = QDateTime::currentMSecsSinceEpoch() - 600000;
        replay::RecorderOptions options; options.directory = history; options.ocr = false; options.minFreeBytes = 0;
        qint64 frameId;
        { replay::Recorder recorder(options); frameId = recorder.addFrame(replay::fixtureFrame(0), start).frameId; recorder.finish(); }
        screenText(history, frameId);
        for (int i = 0; i < 102; ++i) source(meetings, QString("Meeting %1").arg(i, 3, 10, QChar('0')), start + i * 1000);
        replay::MeetingImporter importer(history, meetings); settle(importer, start + 600000);
        auto viewer = replay::createViewer(history); viewer->show();
        auto* search = viewer->findChild<QLineEdit*>("recallSearch");
        auto* results = viewer->findChild<QListWidget*>("recallResults");
        search->setText("contin");
        QTRY_COMPARE(viewer->property("totalMatches").toInt(), 103);
        QCOMPARE(results->count(), 100);
        QTest::keyClick(viewer.get(), Qt::Key_PageDown);
        QTRY_COMPARE(viewer->property("matchPageOffset").toInt(), 100);
        QCOMPARE(results->count(), 3);
        results->setCurrentRow(2);
        QTRY_COMPARE(viewer->property("selectedFrameId").toLongLong(), frameId);
        QTest::keyClick(viewer.get(), Qt::Key_PageUp);
        QTRY_COMPARE(viewer->property("matchPageOffset").toInt(), 0);
        QCOMPARE(results->count(), 100);
        viewer->close();
    }
};
QTEST_MAIN(MeetingIntegrationTest)
#include "meeting_integration_test.moc"
