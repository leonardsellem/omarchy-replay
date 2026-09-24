#include "meeting_view.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <algorithm>

namespace replay {
namespace {

class TranscriptReader final : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    void setActivePassage(int position) {
        activePassage_ = position;
        viewport()->update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPlainTextEdit::paintEvent(event);
        if (activePassage_ < 0) return;
        const auto block = document()->findBlock(activePassage_);
        if (!block.isValid()) return;
        const auto bounds = blockBoundingGeometry(block).translated(contentOffset());
        if (bounds.bottom() < 0 || bounds.top() > viewport()->height()) return;
        // A one-pixel cue beside this passage leaves character-level search
        // highlights and the user's ordinary text selection unobscured.
        QPainter painter(viewport());
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
        painter.drawLine(QPointF(1.5, std::max(0.0, bounds.top() + 1)),
                         QPointF(1.5, std::min(qreal(viewport()->height()), bounds.bottom() - 1)));
    }

private:
    int activePassage_ = -1;
};

// Highlight within Qt's ordinary text blocks instead of constructing a rich
// document or one widget/selection per search hit. Transcript HTML stays text.
class TranscriptHighlighter final : public QSyntaxHighlighter {
public:
    explicit TranscriptHighlighter(QPlainTextEdit* reader)
        : QSyntaxHighlighter(reader->document()), reader_(reader) {}

    void setRanges(QVector<QPair<int, int>> ranges) {
        ranges_ = std::move(ranges);
        rehighlight();
    }

protected:
    void highlightBlock(const QString& text) override {
        const int start = currentBlock().position();
        const int end = start + text.size();
        auto first = std::lower_bound(ranges_.cbegin(), ranges_.cend(), start,
            [](const QPair<int, int>& range, int position) { return range.first + range.second <= position; });
        QTextCharFormat format;
        format.setBackground(reader_->palette().brush(QPalette::Highlight));
        format.setForeground(reader_->palette().brush(QPalette::HighlightedText));
        for (auto it = first; it != ranges_.cend() && it->first < end; ++it) {
            const int from = std::max(start, it->first);
            const int to = std::min(end, it->first + it->second);
            if (to > from) setFormat(from - start, to - from, format);
        }
    }

private:
    QPlainTextEdit* reader_;
    QVector<QPair<int, int>> ranges_;
};

QLabel* plainLabel(const char* name, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    return label;
}

QPushButton* action(const QString& text, const char* name, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

} // namespace

MeetingView::MeetingView(QWidget* parent) : QWidget(parent) {
    setObjectName("meetingView");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    title_ = plainLabel("meetingTitle", this);
    auto titleFont = title_->font();
    titleFont.setBold(true);
    title_->setFont(titleFont);
    metadata_ = plainLabel("meetingMetadata", this);
    layout->addWidget(title_);
    layout->addWidget(metadata_);

    auto* navigation = new QHBoxLayout;
    navigation->setSpacing(8);
    previous_ = action("Previous passage", "previousMeetingPassage", this);
    next_ = action("Next passage", "nextMeetingPassage", this);
    previous_->setToolTip("Previous matching passage · [ or Alt+Up");
    next_->setToolTip("Next matching passage · ] or Alt+Down");
    position_ = plainLabel("meetingPassagePosition", this);
    navigation->addWidget(previous_);
    navigation->addWidget(next_);
    navigation->addWidget(position_);
    navigation->addStretch();
    layout->addLayout(navigation);

    transcript_ = new TranscriptReader(this);
    transcript_->setObjectName("meetingTranscript");
    transcript_->setAccessibleName("Meeting transcript");
    transcript_->setReadOnly(true);
    transcript_->setUndoRedoEnabled(false);
    transcript_->setTabChangesFocus(true);
    transcript_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    transcript_->setPlaceholderText("Choose a meeting to read its transcript.");
    highlighter_ = new TranscriptHighlighter(transcript_);
    layout->addWidget(transcript_, 1);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    copy_ = action("Copy transcript", "copyMeetingTranscript", this);
    copy_->setToolTip("Copy the complete transcript · Ctrl+Shift+C");
    open_ = action("Open recording", "openMeetingRecording", this);
    open_->setToolTip("Open this saved meeting in Meeting Recorder");
    browse_ = action("Browse screens", "browseMeetingScreens", this);
    actions->addWidget(copy_);
    actions->addWidget(open_);
    actions->addWidget(browse_);
    actions->addStretch();
    layout->addLayout(actions);
    notice_ = plainLabel("meetingNotice", this);
    notice_->hide();
    layout->addWidget(notice_);

    connect(previous_, &QPushButton::clicked, this, [this] { focusPassage(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { focusPassage(1); });
    connect(copy_, &QPushButton::clicked, this, [this] { copyText(true); });
    connect(open_, &QPushButton::clicked, this, [this] { openRecording(); });
    connect(browse_, &QPushButton::clicked, this, [this] {
        if (meeting_.timeKnown && browseScreens) browseScreens(meeting_.startedAtMs);
    });
    for (auto* child : findChildren<QWidget*>()) child->installEventFilter(this);
    installEventFilter(this);
    clear();
}

void MeetingView::setMeeting(const MeetingRecord& meeting, const QString& query) {
    // Background refresh can return the same meeting while someone is reading,
    // selecting or scrolling. Rebuilding that document would lose their place.
    if (meeting.id > 0 && meeting_.id == meeting.id && meeting_.revision == meeting.revision &&
        sourceTranscript_ == meeting.transcript && query_ == query && meeting_.title == meeting.title &&
        meeting_.startedAtMs == meeting.startedAtMs && meeting_.timeKnown == meeting.timeKnown &&
        meeting_.durationSeconds == meeting.durationSeconds &&
        meeting_.sourceDirectory == meeting.sourceDirectory && meeting_.manifestPath == meeting.manifestPath)
        return;
    meeting_ = meeting;
    sourceTranscript_ = meeting.transcript;
    query_ = query;
    title_->setText(meeting.title.isEmpty() ? "Untitled meeting" : meeting.title);
    const QString date = meeting.timeKnown
        ? QDateTime::fromMSecsSinceEpoch(meeting.startedAtMs).toString("ddd, MMM d · HH:mm")
        : "Start time unknown";
    metadata_->setText(date + (meeting.durationSeconds > 0
        ? QString(" · %1 min").arg((meeting.durationSeconds + 59) / 60) : QString()));
    transcript_->setPlainText(meeting.transcript);
    // QTextDocument normalizes CRLF and paragraph separators; highlights and
    // passage copies must use the same UTF-16 coordinate space as the reader.
    meeting_.transcript = transcript_->toPlainText();
    auto ranges = meetingMatchRanges(meeting_.transcript, query);
    passages_.clear();
    for (const auto& range : ranges) {
        const auto block = transcript_->document()->findBlock(range.first);
        if (block.isValid() && (passages_.isEmpty() || passages_.last().first != block.position()))
            passages_.append({block.position(), block.text().size()});
    }
    static_cast<TranscriptHighlighter*>(highlighter_)->setRanges(std::move(ranges));
    activePassage_ = passages_.isEmpty() ? -1 : 0;
    previous_->setEnabled(passages_.size() > 1);
    next_->setEnabled(passages_.size() > 1);
    copy_->setEnabled(!meeting.transcript.isEmpty());
    open_->setEnabled(meeting.id > 0);
    browse_->setEnabled(meeting.timeKnown);
    browse_->setToolTip(meeting.timeKnown ? "Browse screen history around the meeting's start"
                                         : "This recording has no verified start time");
    notice_->clear();
    notice_->hide();
    updatePassage();
}

void MeetingView::clear() {
    meeting_ = {};
    sourceTranscript_.clear();
    query_.clear();
    passages_.clear();
    activePassage_ = -1;
    title_->clear();
    metadata_->clear();
    position_->clear();
    static_cast<TranscriptHighlighter*>(highlighter_)->setRanges({});
    transcript_->clear();
    static_cast<TranscriptReader*>(transcript_)->setActivePassage(-1);
    for (auto* button : {previous_, next_, copy_, open_, browse_}) button->setEnabled(false);
    notice_->clear();
    notice_->hide();
}

void MeetingView::focusPassage(int direction) {
    if (passages_.isEmpty()) return;
    activePassage_ = (activePassage_ + (direction < 0 ? -1 : 1) + passages_.size()) % passages_.size();
    updatePassage();
    transcript_->setFocus(Qt::ShortcutFocusReason);
}

void MeetingView::updatePassage() {
    if (activePassage_ < 0) {
        position_->setText(query_.trimmed().isEmpty() ? QString() : "No matching passages");
        static_cast<TranscriptReader*>(transcript_)->setActivePassage(-1);
        return;
    }
    position_->setText(QString("%1 / %2 passages").arg(activePassage_ + 1).arg(passages_.size()));
    QTextCursor cursor(transcript_->document());
    cursor.setPosition(passages_[activePassage_].first);
    transcript_->setTextCursor(cursor);
    transcript_->ensureCursorVisible();
    static_cast<TranscriptReader*>(transcript_)->setActivePassage(passages_[activePassage_].first);
}

void MeetingView::copyText(bool whole) {
    QString text;
    if (whole || query_.trimmed().isEmpty()) text = meeting_.transcript;
    if (!whole && transcript_->textCursor().hasSelection()) {
        text = transcript_->textCursor().selectedText();
        text.replace(QChar::ParagraphSeparator, '\n');
    } else if (!whole && !query_.trimmed().isEmpty()) {
        QStringList lines;
        for (const auto& passage : passages_) lines.append(meeting_.transcript.mid(passage.first, passage.second));
        text = lines.join('\n');
    }
    if (text.isEmpty()) {
        notice("Select transcript text to copy.");
        return;
    }
    QApplication::clipboard()->setText(text);
    notice(whole ? "Transcript copied" : "Text copied");
}

void MeetingView::openRecording() {
    // A source path comes from an imported record. It is never interpreted as a
    // shell command, URL, executable or desktop-file association.
    const QFileInfo source(meeting_.sourceDirectory);
    const QFileInfo manifest(meeting_.manifestPath);
    if (!source.isAbsolute() || !source.isDir() || !manifest.isFile() ||
        manifest.suffix() != "meeting-recorder" ||
        manifest.canonicalPath() != source.canonicalFilePath()) {
        notice("The original meeting folder is unavailable. The saved transcript is still readable.");
        return;
    }
    const QString executable = QStandardPaths::findExecutable("omarchy-meeting-recorder");
    if (executable.isEmpty()) {
        notice("Meeting Recorder is not installed. The saved transcript is still readable.");
        return;
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments({source.canonicalFilePath()});
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (!process.startDetached()) notice("Meeting Recorder could not open. Try opening it from your app launcher.");
    else { notice_->clear(); notice_->hide(); }
}

void MeetingView::notice(const QString& text) {
    notice_->setText(text);
    notice_->show();
}

bool MeetingView::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_C && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            copyText(true); return true;
        }
        if (key->matches(QKeySequence::Copy)) { copyText(); return true; }
        const bool backwards = (key->key() == Qt::Key_BracketLeft && key->modifiers() == Qt::NoModifier) ||
            (key->key() == Qt::Key_Up && key->modifiers() == Qt::AltModifier);
        const bool forwards = (key->key() == Qt::Key_BracketRight && key->modifiers() == Qt::NoModifier) ||
            (key->key() == Qt::Key_Down && key->modifiers() == Qt::AltModifier);
        if (backwards || forwards) { focusPassage(backwards ? -1 : 1); return true; }
    }
    return QWidget::eventFilter(object, event);
}

void MeetingView::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && highlighter_) {
        highlighter_->rehighlight();
        updatePassage();
    }
}

} // namespace replay
