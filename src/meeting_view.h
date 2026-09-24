#pragma once

#include "meeting_index.h"
#include <QWidget>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSyntaxHighlighter;

namespace replay {

// A plain-text reader for imported transcripts. Recording remains owned by
// Meeting Recorder; this view never starts capture or transcribes audio.
class MeetingView final : public QWidget {
public:
    explicit MeetingView(QWidget* parent = nullptr);
    void setMeeting(const MeetingRecord& meeting, const QString& query);
    void clear();
    void focusPassage(int direction);
    void copyText(bool whole = false);
    qint64 meetingId() const { return meeting_.id; }

    // Only offered when the recorder supplied a known original start time.
    std::function<void(qint64)> browseScreens;

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updatePassage();
    void openRecording();
    void notice(const QString& text);

    MeetingRecord meeting_;
    QString sourceTranscript_;
    QString query_;
    QVector<QPair<int, int>> passages_;
    int activePassage_ = -1;
    QLabel *title_ = nullptr, *metadata_ = nullptr, *position_ = nullptr, *notice_ = nullptr;
    QPlainTextEdit* transcript_ = nullptr;
    QSyntaxHighlighter* highlighter_ = nullptr;
    QPushButton *previous_ = nullptr, *next_ = nullptr, *copy_ = nullptr,
                *open_ = nullptr, *browse_ = nullptr;
};

} // namespace replay
