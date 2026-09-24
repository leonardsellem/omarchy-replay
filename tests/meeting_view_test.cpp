#include "meeting_view.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>

namespace {
class Environment {
public:
    explicit Environment(const QByteArray& name, const QByteArray& value)
        : name_(name), old_(qgetenv(name.constData())), existed_(qEnvironmentVariableIsSet(name.constData())) {
        qputenv(name.constData(), value);
    }
    ~Environment() {
        if (existed_) qputenv(name_.constData(), old_); else qunsetenv(name_.constData());
    }
private:
    QByteArray name_, old_;
    bool existed_;
};

replay::MeetingRecord sample() {
    replay::MeetingRecord meeting;
    meeting.id = 7;
    meeting.title = "Synthetic invoice review";
    meeting.startedAtMs = 1770000000000;
    meeting.timeKnown = true;
    meeting.durationSeconds = 125;
    meeting.transcript = "[00:00] Maya: The invoice is here. Keep the invoice number.\n"
                         "[00:08] Jo: That looks right.\n"
                         "[00:13] Maya: A second invoice arrived.";
    return meeting;
}

bool write(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
} // namespace

class MeetingViewTest : public QObject {
    Q_OBJECT
private slots:
    void groupedPassageNavigation() {
        replay::MeetingView view;
        view.resize(820, 600);
        view.setMeeting(sample(), "invoi");
        view.show();
        auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        auto* position = view.findChild<QLabel*>("meetingPassagePosition");
        QCOMPARE(view.meetingId(), 7);
        QCOMPARE(position->text(), "1 / 2 passages");
        QCOMPARE(transcript->textCursor().blockNumber(), 0);
        transcript->setFocus();
        QTest::keyClick(transcript, Qt::Key_BracketRight);
        QCOMPARE(position->text(), "2 / 2 passages");
        QCOMPARE(transcript->textCursor().blockNumber(), 2);
        QTest::keyClick(transcript, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(position->text(), "1 / 2 passages");
        QTest::keyClick(transcript, Qt::Key_BracketLeft);
        QCOMPARE(position->text(), "2 / 2 passages");
        QTest::keyClick(transcript, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(position->text(), "1 / 2 passages");
        view.findChild<QPushButton*>("nextMeetingPassage")->click();
        QCOMPARE(position->text(), "2 / 2 passages");
        view.findChild<QPushButton*>("previousMeetingPassage")->click();
        QCOMPARE(position->text(), "1 / 2 passages");
    }

    void activePassageKeepsSearchHighlightsUnobscured() {
        replay::MeetingView view;
        auto meeting = sample();
        meeting.transcript = "Continue with the draft.\nContinuity needs more notes.";
        view.setMeeting(meeting, "contin");
        auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        const auto checkHighlights = [&] {
            // Current-passage emphasis must not add a background overlay over
            // the matched word's character-level syntax format.
            QVERIFY(transcript->extraSelections().isEmpty());
            for (auto block = transcript->document()->begin(); block.isValid(); block = block.next()) {
                const auto formats = block.layout()->formats();
                QCOMPARE(formats.size(), 1);
                QCOMPARE(formats.first().start, 0);
                QCOMPARE(formats.first().format.background().color(), transcript->palette().color(QPalette::Highlight));
                QCOMPARE(formats.first().format.foreground().color(), transcript->palette().color(QPalette::HighlightedText));
            }
        };
        checkHighlights();
        view.focusPassage(1);
        checkHighlights();
    }

    void copiesSelectedMatchingAndWholeText() {
        replay::MeetingView view;
        const auto meeting = sample();
        view.setMeeting(meeting, "invoi");
        auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        view.copyText();
        QCOMPARE(QApplication::clipboard()->text(),
            "[00:00] Maya: The invoice is here. Keep the invoice number.\n[00:13] Maya: A second invoice arrived.");
        auto cursor = transcript->textCursor();
        const int from = meeting.transcript.indexOf("That looks right.");
        cursor.setPosition(from);
        cursor.setPosition(from + 17, QTextCursor::KeepAnchor);
        transcript->setTextCursor(cursor);
        QTest::keyClick(transcript, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), "That looks right.");
        QTest::keyClick(transcript, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), meeting.transcript);
        QApplication::clipboard()->clear();
        view.findChild<QPushButton*>("copyMeetingTranscript")->click();
        QCOMPARE(QApplication::clipboard()->text(), meeting.transcript);
    }

    void transcriptIsPlainTextAndUnknownTimeCannotBrowse() {
        replay::MeetingView view;
        auto meeting = sample();
        meeting.timeKnown = false;
        meeting.title = "<img src=\"file:///not-a-real-private-path\">";
        meeting.transcript = "<script>invoice()</script>\n"
                             "![invoice](https://example.invalid/image)\n"
                             "[invoice](file:///not-a-real-private-path)";
        view.setMeeting(meeting, "invoice");
        auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        QCOMPARE(transcript->toPlainText(), meeting.transcript);
        QVERIFY(transcript->isReadOnly());
        QCOMPARE(view.findChild<QLabel*>("meetingTitle")->textFormat(), Qt::PlainText);
        QCOMPARE(view.findChild<QLabel*>("meetingTitle")->text(), meeting.title);
        QVERIFY(view.findChild<QLabel*>("meetingMetadata")->text().startsWith("Start time unknown"));
        auto* browse = view.findChild<QPushButton*>("browseMeetingScreens");
        QVERIFY(!browse->isEnabled());
        qint64 requested = -1;
        view.browseScreens = [&requested](qint64 time) { requested = time; };
        browse->click();
        QCOMPARE(requested, -1);
        meeting.timeKnown = true;
        view.setMeeting(meeting, "invoice");
        browse->click();
        QCOMPARE(requested, meeting.startedAtMs);
    }

    void noMatchDoesNotOverwriteClipboardAndClearResets() {
        replay::MeetingView view;
        view.setMeeting(sample(), "unfindable");
        auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        QApplication::clipboard()->setText("Keep this");
        view.copyText();
        QCOMPARE(QApplication::clipboard()->text(), "Keep this");
        QCOMPARE(view.findChild<QLabel*>("meetingPassagePosition")->text(), "No matching passages");
        QVERIFY(!view.findChild<QPushButton*>("nextMeetingPassage")->isEnabled());
        view.clear();
        QCOMPARE(view.meetingId(), 0);
        QVERIFY(transcript->toPlainText().isEmpty());
        for (const auto* name : {"nextMeetingPassage", "copyMeetingTranscript", "openMeetingRecording", "browseMeetingScreens"})
            QVERIFY(!view.findChild<QPushButton*>(name)->isEnabled());
    }

    void normalizedLineEndingsKeepCopiesAligned() {
        auto meeting = sample();
        meeting.transcript.replace('\n', "\r\n");
        replay::MeetingView view;
        view.setMeeting(meeting, "invoi");
        view.focusPassage(1);
        QCOMPARE(view.findChild<QPlainTextEdit*>("meetingTranscript")->textCursor().blockNumber(), 2);
        view.copyText();
        QCOMPARE(QApplication::clipboard()->text(),
            "[00:00] Maya: The invoice is here. Keep the invoice number.\n[00:13] Maya: A second invoice arrived.");
    }

    void unchangedRefreshPreservesReadingPosition_data() {
        QTest::addColumn<bool>("crlf");
        QTest::newRow("native-line-endings") << false;
        QTest::newRow("normalized-line-endings") << true;
    }

    void unchangedRefreshPreservesReadingPosition() {
        QFETCH(bool, crlf);
        auto meeting = sample();
        meeting.revision = "synthetic-revision-1";
        meeting.transcript.clear();
        for (int i = 0; i < 120; ++i)
            meeting.transcript += QString("[00:%1] Maya: Discuss invoice %1 and its details.%2")
                .arg(i).arg(crlf ? "\r\n" : "\n");
        replay::MeetingView view;
        view.resize(720, 460);
        view.setMeeting(meeting, "invoice");
        view.show();
        auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        auto* position = view.findChild<QLabel*>("meetingPassagePosition");
        view.focusPassage(1);
        view.focusPassage(1);
        auto cursor = transcript->textCursor();
        cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor, 4);
        transcript->setTextCursor(cursor);
        auto* scroll = transcript->verticalScrollBar();
        scroll->setValue(scroll->maximum() / 2);
        const int scrolled = scroll->value();
        QVERIFY(scrolled > 0);
        const int cursorPosition = cursor.position();
        const int cursorAnchor = cursor.anchor();
        const auto selectedText = cursor.selectedText();
        const auto passage = position->text();
        QSignalSpy changed(transcript, &QPlainTextEdit::textChanged);

        view.setMeeting(meeting, "invoice");
        QCOMPARE(changed.size(), 0);
        QCOMPARE(transcript->textCursor().position(), cursorPosition);
        QCOMPARE(transcript->textCursor().anchor(), cursorAnchor);
        QCOMPARE(transcript->textCursor().selectedText(), selectedText);
        QCOMPARE(scroll->value(), scrolled);
        QCOMPARE(position->text(), passage);
        view.focusPassage(1);
        QCOMPARE(position->text(), "4 / 120 passages");

        meeting.revision = "synthetic-revision-2";
        meeting.title = "Updated synthetic meeting";
        view.setMeeting(meeting, "invoice");
        QVERIFY(changed.size() > 0);
        QCOMPARE(position->text(), "1 / 120 passages");
        QCOMPARE(view.findChild<QLabel*>("meetingTitle")->text(), meeting.title);
        view.setMeeting(meeting, "unfindable");
        QCOMPARE(position->text(), "No matching passages");
    }

    void compactLayoutFitsInheritedMonospaceTheme() {
        QWidget host;
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(10);
        host.setFont(font);
        host.setStyleSheet("QPushButton { border: 1px solid; padding: 6px 10px; }"
                          "QPlainTextEdit { border: 1px solid; padding: 4px; }");
        replay::MeetingView view(&host);
        host.resize(720, 520);
        view.setGeometry(host.rect());
        view.setMeeting(sample(), "invoice");
        host.show();
        QCoreApplication::processEvents();
        QCOMPARE(view.width(), 720);
        for (const auto* name : {"previousMeetingPassage", "nextMeetingPassage", "copyMeetingTranscript",
                                 "openMeetingRecording", "browseMeetingScreens"}) {
            const auto* button = view.findChild<QPushButton*>(name);
            QVERIFY(view.rect().contains(QRect(button->mapTo(&view, QPoint()), button->size())));
            QVERIFY2(button->width() >= QFontMetrics(button->font()).horizontalAdvance(button->text()) + 20, name);
        }
        const auto* transcript = view.findChild<QPlainTextEdit*>("meetingTranscript");
        QVERIFY(transcript->height() > host.height() / 2);
    }

    void missingSourceAndMissingRecorderRemainReadable() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        Environment path("PATH", root.path().toUtf8());
        replay::MeetingView view;
        auto meeting = sample();
        meeting.sourceDirectory = root.filePath("missing-meeting");
        view.setMeeting(meeting, {});
        view.findChild<QPushButton*>("openMeetingRecording")->click();
        QVERIFY(view.findChild<QLabel*>("meetingNotice")->text().startsWith("The original meeting folder is unavailable"));
        meeting.sourceDirectory = root.path();
        meeting.manifestPath = root.filePath("Synthetic.meeting-recorder");
        QVERIFY(write(meeting.manifestPath, "{}"));
        view.setMeeting(meeting, {});
        view.findChild<QPushButton*>("openMeetingRecording")->click();
        QVERIFY(view.findChild<QLabel*>("meetingNotice")->text().startsWith("Meeting Recorder is not installed"));
        QCOMPARE(view.findChild<QPlainTextEdit*>("meetingTranscript")->toPlainText(), meeting.transcript);
    }

    void recordingLaunchUsesOneLiteralFolderArgument() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString bin = root.filePath("bin");
        const QString folder = root.filePath("Meeting; $(touch not-created)");
        QVERIFY(QDir().mkpath(bin));
        QVERIFY(QDir().mkpath(folder));
        const QString executable = QDir(bin).filePath("omarchy-meeting-recorder");
        QVERIFY(write(executable,
            "#!/bin/sh\n[ \"$#\" -eq 1 ] || exit 2\nprintf '%s' \"$1\" > \"$REPLAY_MEETING_TEST_ARGUMENT\"\n"));
        QVERIFY(QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        Environment path("PATH", bin.toUtf8());
        Environment output("REPLAY_MEETING_TEST_ARGUMENT", root.filePath("argument.txt").toUtf8());
        auto meeting = sample();
        meeting.sourceDirectory = folder;
        meeting.manifestPath = QDir(folder).filePath("Synthetic.meeting-recorder");
        QVERIFY(write(meeting.manifestPath, "{}"));
        replay::MeetingView view;
        view.setMeeting(meeting, {});
        view.findChild<QPushButton*>("openMeetingRecording")->click();
        QTRY_VERIFY(QFile::exists(root.filePath("argument.txt")));
        QFile argument(root.filePath("argument.txt"));
        QVERIFY(argument.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(argument.readAll()), folder);
        QVERIFY(!QFile::exists(root.filePath("not-created")));
    }

    void tabNavigationReachesAllActions() {
        replay::MeetingView view;
        view.resize(820, 600);
        view.setMeeting(sample(), "invoice");
        view.show();
        auto* previous = view.findChild<QPushButton*>("previousMeetingPassage");
        view.activateWindow();
        previous->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), previous);
        for (const auto* name : {"nextMeetingPassage", "meetingTranscript", "copyMeetingTranscript",
                                 "openMeetingRecording", "browseMeetingScreens"}) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QTRY_COMPARE(QApplication::focusWidget(), view.findChild<QWidget*>(name));
        }
    }
};

QTEST_MAIN(MeetingViewTest)
#include "meeting_view_test.moc"
