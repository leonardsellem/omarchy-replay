#include "viewer.h"
#include "recorder.h"
#include "index_service.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QProgressBar>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <exception>

namespace replay {
namespace {

struct ReplayColors {
    QColor background = QColor("#16181c"), foreground = QColor("#e5e7eb");
    QColor accent = QColor("#8ab4f8"), surface, muted, border;
};

QColor mix(const QColor& a, const QColor& b, double amount) {
    return QColor::fromRgbF(a.redF() * amount + b.redF() * (1 - amount),
        a.greenF() * amount + b.greenF() * (1 - amount),
        a.blueF() * amount + b.blueF() * (1 - amount));
}

ReplayColors theme(QWidget* widget) {
    ReplayColors colors;
    const QString state = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state");
    const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    for (const QString& path : {state + "/omarchy/current/theme/colors.toml", config + "/omarchy/current/theme/colors.toml"}) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QString data = QString::fromUtf8(file.read(65536));
        for (const auto& entry : {std::pair{"background", &colors.background},
                                  std::pair{"foreground", &colors.foreground},
                                  std::pair{"accent", &colors.accent}}) {
            const auto match = QRegularExpression(QString("(?m)^%1\\s*=\\s*[\"'](#[0-9a-fA-F]{6})[\"']").arg(entry.first)).match(data);
            if (match.hasMatch()) *entry.second = QColor(match.captured(1));
        }
        break;
    }
    colors.surface = mix(colors.foreground, colors.background, .045);
    colors.muted = mix(colors.foreground, colors.background, .72);
    colors.border = mix(colors.foreground, colors.background, .23);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, colors.background);
    palette.setColor(QPalette::Base, colors.background);
    palette.setColor(QPalette::WindowText, colors.foreground);
    palette.setColor(QPalette::Text, colors.foreground);
    palette.setColor(QPalette::ButtonText, colors.foreground);
    palette.setColor(QPalette::Highlight, colors.accent);
    palette.setColor(QPalette::HighlightedText, colors.background);
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFile terminal(config + "/alacritty/alacritty.toml");
    if (terminal.open(QIODevice::ReadOnly)) {
        const auto match = QRegularExpression("family\\s*=\\s*\"([^\"]+)\"").match(QString::fromUtf8(terminal.read(65536)));
        if (match.hasMatch()) font.setFamily(match.captured(1));
    }
    font.setPointSize(10);
    widget->setFont(font);
    widget->setStyleSheet(QString(R"(
        QWidget { color: %1; }
        QLineEdit { background: %2; border: 1px solid %3; padding: 8px 12px; selection-background-color: %4; selection-color: %5; }
        QLineEdit:focus { border-color: %4; }
        QPushButton { background: transparent; border: 1px solid transparent; padding: 5px 8px; }
        QPushButton:hover, QPushButton:checked { background: %2; }
        QPushButton:focus { border-color: %4; }
        QPushButton:disabled { color: %3; }
        QListWidget { background: transparent; border: none; outline: none; }
        QListWidget::item { padding: 3px 12px; border: none; border-bottom: 2px solid transparent; color: %6; }
        QListWidget::item:hover { background: %2; }
        QListWidget::item:selected { border-bottom-color: %4; background: %2; color: %1; }
        QListWidget:focus { border-bottom: 1px solid %3; }
        QProgressBar { border: none; background: %3; height: 3px; }
        QProgressBar::chunk { background: %4; }
        QLabel[keycap="true"] { color: %1; background: %5; padding: 4px 7px; }
        QWidget#helpPanel { background: %2; }
        QScrollArea { border: none; background: %2; }
        QScrollBar:horizontal { height: 5px; background: %5; }
        QScrollBar:vertical { width: 6px; background: %5; }
        QScrollBar::handle { background: %3; min-width: 16px; min-height: 16px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QLabel#keyboardHelp, QLabel#indexState, QLabel#recallStatus { color: %6; }
        QWidget#detailsPanel { background: %2; }
    )").arg(colors.foreground.name(), colors.surface.name(), colors.border.name(),
            colors.accent.name(), colors.background.name(), colors.muted.name()));
    return colors;
}

class ImageCanvas final : public QWidget {
public:
    QImage image;
    QVector<QRect> highlights;
    bool showHighlights = true;
    explicit ImageCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("recordedImage");
        setAccessibleName("Recorded screen with matching text highlights");
    }
protected:
    void paintEvent(QPaintEvent*) override {
        if (image.isNull()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(rect(), image);
        if (!showHighlights) return;
        painter.scale(double(width()) / image.width(), double(height()) / image.height());
        QPen pen(QColor("#f6cc59"));
        pen.setCosmetic(true);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.setBrush(QColor(246, 204, 89, 62));
        for (const auto& box : highlights) painter.drawRect(box);
    }
};

class EvidenceView final : public QScrollArea {
public:
    EvidenceView() {
        setObjectName("evidenceView");
        setAccessibleName("Recorded screen image");
        setAlignment(Qt::AlignCenter);
        setFrameShape(QFrame::NoFrame);
        canvas_ = new ImageCanvas;
        setWidget(canvas_);
        setFocusPolicy(Qt::StrongFocus);
    }
    void setImage(const QImage& image) { canvas_->image = image; render(); }
    void setHighlights(const QVector<QRect>& boxes) { canvas_->highlights = boxes; canvas_->update(); }
    void toggleHighlights() { canvas_->showHighlights = !canvas_->showHighlights; canvas_->update(); }
    void setFit(bool fit) { fit_ = fit; setProperty("fit", fit); render(); }
protected:
    void resizeEvent(QResizeEvent* event) override { QScrollArea::resizeEvent(event); render(); }
private:
    void render() {
        const auto& image = canvas_->image;
        const QSize target = image.isNull() ? viewport()->size() : fit_
            ? image.size().scaled(viewport()->size(), Qt::KeepAspectRatio) : image.size();
        canvas_->resize(target);
        canvas_->update();
    }
    ImageCanvas* canvas_ = nullptr;
    bool fit_ = true;
};

class TimelineView final : public QSlider {
public:
    explicit TimelineView(const ReplayColors& colors) : QSlider(Qt::Horizontal), colors_(colors) {
        setObjectName("recallTimeline");
        setAccessibleName("Recorded history timeline");
        setRange(0, 1000000);
        setFixedHeight(56);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }
    void setOverview(const TimelineOverview& overview) { overview_ = overview; update(); }
    void setMatches(const TimelineOverview& matches) {
        hits_.clear();
        for (const auto& point : matches.points) hits_.append(point);
        setProperty("matchMarkerCount", hits_.size());
        update();
    }
    std::function<void(qint64)> activateMatch;
    void setMoment(qint64 moment) {
        moment_ = moment;
        setAccessibleDescription(QDateTime::fromMSecsSinceEpoch(moment).toString("dddd MMMM d HH:mm:ss"));
        const QSignalBlocker blocker(this);
        setValue(int(fraction(moment) * maximum()));
        update();
    }
    qint64 timeAt(int value) const {
        return overview_.firstTimestampMs + qint64(double(value) / maximum() *
            (overview_.lastTimestampMs - overview_.firstTimestampMs));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        const int left = 12, right = width() - 12, y = 19;
        p.setPen(QPen(colors_.border, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(left, y, right, y);
        if (!overview_.totalFrames) return;
        QSet<int> occupied;
        p.setPen(Qt::NoPen);
        p.setBrush(colors_.muted);
        for (const auto& point : overview_.points) {
            const int x = left + fraction(point.timestampMs) * (right - left);
            if (occupied.contains(x / 3)) continue;
            occupied.insert(x / 3);
            p.drawEllipse(QPointF(x, y), 1.5, 1.5);
        }
        occupied.clear();
        p.setBrush(colors_.accent);
        for (const auto& hit : hits_) {
            const int x = left + fraction(hit.timestampMs) * (right - left);
            if (occupied.contains(x / 5)) continue;
            occupied.insert(x / 5);
            p.drawRoundedRect(QRectF(x - 3, y - 4, 6, 8), 3, 3);
        }
        const int cursor = left + fraction(moment_) * (right - left);
        p.setPen(QPen(colors_.foreground, 1));
        p.drawLine(cursor, 4, cursor, 31);
        p.setBrush(colors_.foreground);
        p.drawEllipse(QPointF(cursor, y), 3, 3);
        p.setPen(colors_.muted);
        p.drawText(QRect(left, 35, width()/2-12, 20), Qt::AlignLeft,
            QDateTime::fromMSecsSinceEpoch(overview_.firstTimestampMs).toString("MMM d · HH:mm:ss"));
        p.drawText(QRect(width()/2, 35, width()/2-12, 20), Qt::AlignRight,
            QDateTime::fromMSecsSinceEpoch(overview_.lastTimestampMs).toString("MMM d · HH:mm:ss"));
        if (hasFocus()) { p.setPen(colors_.accent); p.drawLine(12, height()-1, width()-12, height()-1); }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        setFocus();
        if (std::abs(event->position().y() - 19) < 12 && activateMatch) {
            qint64 nearest = 0;
            double distance = 9;
            bool found = false;
            for (const auto& hit : hits_) {
                const double delta = std::abs(12 + fraction(hit.timestampMs) * (width() - 24) - event->position().x());
                if (delta < distance) { nearest = hit.id; distance = delta; found = true; }
            }
            if (found) { activateMatch(nearest); return; }
        }
        scrub(event->position().x());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) scrub(event->position().x());
    }

private:
    double fraction(qint64 time) const {
        if (overview_.lastTimestampMs <= overview_.firstTimestampMs) return 0;
        return std::clamp(double(time - overview_.firstTimestampMs) /
            (overview_.lastTimestampMs - overview_.firstTimestampMs), 0., 1.);
    }
    void scrub(double x) { setValue(int(std::clamp((x - 12) / std::max(1, width() - 24), 0., 1.) * maximum())); }
    ReplayColors colors_;
    TimelineOverview overview_;
    QVector<TimelinePoint> hits_;
    qint64 moment_ = 0;
};

struct DecodedFrame {
    qint64 id = 0;
    QImage image;
    QString error;
};

struct HistorySnapshot {
    quint64 generation = 0;
    quint64 selectionRevision = 0;
    QString query;
    bool preserve = false;
    qint64 currentId = 0;
    QJsonObject indexing;
    QJsonObject service, policy;
    QVector<FrameRecord> matches;
    TimelineOverview timeline;
    SearchPage page;
    qint64 offset = 0;
    int targetRow = -1;
    qint64 anchorFrameId = 0;
    std::optional<FrameRecord> current;
    QString error;
};

struct TimelineNeighbors {
    qint64 id = 0;
    std::optional<FrameRecord> previous, next;
    QString error;
};

struct IndexingRequestResult {
    qint64 frameId = 0;
    bool catchUp = false;
    int changed = 0;
    QString error;
};

struct ServiceControlResult { QString action, error; QJsonObject status; };

QString elapsedDescription(qint64 milliseconds) {
    if (milliseconds < 60000) return "less than a minute";
    const qint64 minutes = milliseconds / 60000;
    if (minutes < 60) return QString("%1 min").arg(minutes);
    if (minutes < 1440) return QString("%1 hr %2 min").arg(minutes / 60).arg(minutes % 60);
    return QString("%1 days %2 hr").arg(minutes / 1440).arg(minutes / 60 % 24);
}

struct SeekResult { quint64 revision = 0; std::optional<FrameRecord> frame; QString error; };
struct HighlightResult { qint64 id = 0; QString query; TextMatches matches; QString error; };

class Viewer final : public QWidget {
public:
    explicit Viewer(QString directory) : directory_(QDir(directory).absolutePath()) {
        setWindowTitle("Replay");
        setObjectName("replayViewer");
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(720, 480);
        resize(1440, 920);
        const auto colors = theme(this);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 12, 16, 10);
        layout->setSpacing(8);
        auto* header = new QHBoxLayout;
        query_ = new QLineEdit;
        query_->setObjectName("recallSearch");
        query_->setAccessibleName("Search recorded text");
        query_->setPlaceholderText("Search what you saw");
        QPixmap clearIcon(32, 32);
        clearIcon.fill(Qt::transparent);
        {
            QPainter painter(&clearIcon);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(colors.muted, 2.5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(10, 10, 22, 22);
            painter.drawLine(22, 10, 10, 22);
        }
        auto* clearSearch = query_->addAction(QIcon(clearIcon), QLineEdit::TrailingPosition);
        clearSearch->setObjectName("clearSearch");
        clearSearch->setText("Clear search");
        clearSearch->setToolTip("Clear search");
        clearSearch->setVisible(false);
        connect(clearSearch, &QAction::triggered, this, [this] { query_->clear(); query_->setFocus(); });
        connect(query_, &QLineEdit::textChanged, clearSearch, [clearSearch](const QString& text) { clearSearch->setVisible(!text.isEmpty()); });
        header->addWidget(query_, 1);
        detailsToggle_ = new QPushButton("Indexing");
        detailsToggle_->setObjectName("toggleDetails");
        detailsToggle_->setCheckable(true);
        detailsToggle_->setToolTip("Search indexing status (I)");
        header->addWidget(detailsToggle_);
        helpToggle_ = new QPushButton("?");
        helpToggle_->setObjectName("toggleHelp");
        helpToggle_->setAccessibleName("Keyboard shortcuts");
        helpToggle_->setToolTip("Keyboard shortcuts (?)");
        helpToggle_->setCheckable(true);
        header->addWidget(helpToggle_);
        layout->addLayout(header);

        details_ = new QWidget;
        details_->setObjectName("detailsPanel");
        auto* detailLayout = new QVBoxLayout(details_);
        detailLayout->setContentsMargins(18, 14, 18, 14);
        detailLayout->setSpacing(10);
        auto* detailHeader = new QHBoxLayout;
        auto* indexTitle = new QLabel("Search index");
        auto indexFont = indexTitle->font(); indexFont.setBold(true); indexTitle->setFont(indexFont);
        detailHeader->addWidget(indexTitle);
        detailHeader->addStretch();
        serviceAction_ = new QPushButton;
        serviceAction_->setObjectName("indexServiceAction");
        stopService_ = new QPushButton("Stop");
        stopService_->setObjectName("stopIndexService");
        stopService_->setToolTip("Stop background indexing for this history; saved moments remain available");
        detailHeader->addWidget(serviceAction_);
        detailHeader->addWidget(stopService_);
        auto* refresh = new QPushButton("Refresh");
        refresh->setObjectName("refreshHistory");
        refresh->setToolTip("Refresh indexing status (F5)");
        detailHeader->addWidget(refresh);
        detailLayout->addLayout(detailHeader);
        status_ = new QLabel;
        status_->setObjectName("recallStatus");
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        detailLayout->addWidget(status_);
        indexProgress_ = new QProgressBar;
        indexProgress_->setObjectName("indexProgress");
        indexProgress_->setAccessibleName("Search indexing completion");
        indexProgress_->setRange(0, 1000);
        indexProgress_->setTextVisible(false);
        indexProgress_->setFixedHeight(3);
        detailLayout->addWidget(indexProgress_);
        workerHint_ = new QLabel;
        workerHint_->setObjectName("indexWorkerHint");
        workerHint_->setTextFormat(Qt::PlainText);
        workerHint_->setWordWrap(true);
        detailLayout->addWidget(workerHint_);
        priorityStatus_ = new QLabel;
        priorityStatus_->setObjectName("indexingRequestStatus");
        priorityStatus_->setTextFormat(Qt::PlainText);
        priorityStatus_->setWordWrap(true);
        detailLayout->addWidget(priorityStatus_);
        auto* actions = new QHBoxLayout;
        processMoment_ = new QPushButton("Index this moment  P");
        processMoment_->setObjectName("processMoment");
        catchUp_ = new QPushButton("Catch up  C");
        catchUp_->setObjectName("catchUpIndexing");
        catchUp_->setToolTip("Request two minutes of faster indexing when resources permit");
        copyWorkerCommand_ = new QPushButton("Copy worker command");
        copyWorkerCommand_->setObjectName("copyIndexCommand");
        for (auto* button : {processMoment_, catchUp_, copyWorkerCommand_}) actions->addWidget(button);
        actions->addStretch();
        detailLayout->addLayout(actions);
        details_->hide();
        connect(detailsToggle_, &QPushButton::toggled, this, [this](bool shown) {
            if (shown) helpToggle_->setChecked(false);
            details_->setVisible(shown);
        });
        layout->addWidget(details_);
        help_ = new QWidget;
        help_->setObjectName("helpPanel");
        auto* helpLayout = new QGridLayout(help_);
        helpLayout->setContentsMargins(18, 14, 18, 14);
        helpLayout->setHorizontalSpacing(18);
        helpLayout->setVerticalSpacing(8);
        const QVector<QPair<QString, QString>> shortcuts{
            {"/  Ctrl+F", "Search"}, {"↑ ↓  J K", "Browse matches"}, {"← →  H L", "Explore time"},
            {"Home  End", "First / latest moment"}, {"PgUp  PgDn", "Previous / next match page"}, {"F  /  1", "Fit / original size"},
            {"Shift+arrows", "Pan original image"}, {"Ctrl+C", "Copy matching lines (or all text without a search)"},
            {"Ctrl+Shift+C", "Copy all recognized screen text"}, {"M", "Toggle highlights"},
            {"P  /  C", "Index moment / catch up"}, {"I  /  ?", "Index status / shortcuts"}, {"Esc", "Leave control / dismiss panel, then close"}};
        const int helpRows = (shortcuts.size() + 1) / 2;
        for (int i = 0; i < shortcuts.size(); ++i) {
            const int row = i % helpRows, column = i / helpRows * 2;
            auto* key = new QLabel(shortcuts[i].first); key->setProperty("keycap", true);
            auto* action = new QLabel(shortcuts[i].second);
            action->setWordWrap(true);
            helpLayout->addWidget(key, row, column, Qt::AlignLeft);
            helpLayout->addWidget(action, row, column + 1);
        }
        help_->hide();
        connect(helpToggle_, &QPushButton::toggled, this, [this](bool shown) {
            if (shown) detailsToggle_->setChecked(false);
            help_->setVisible(shown);
        });
        layout->addWidget(help_);

        resultsHeading_ = new QLabel;
        resultsHeading_->setObjectName("resultsHeading");
        resultsHeading_->hide();

        results_ = new QListWidget;
        results_->setObjectName("recallResults");
        results_->setAccessibleName("Matching recorded moments");
        results_->setFlow(QListView::LeftToRight);
        results_->setWrapping(false);
        results_->setWordWrap(false);
        results_->setTextElideMode(Qt::ElideRight);
        results_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        results_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        results_->setFixedHeight(34);
        results_->hide();
        mediaStatus_ = new QLabel;
        mediaStatus_->setObjectName("mediaStatus");
        mediaStatus_->setWordWrap(true);
        mediaStatus_->setTextFormat(Qt::PlainText);
        layout->addWidget(mediaStatus_);
        evidence_ = new EvidenceView;
        layout->addWidget(evidence_, 1);
        auto* caption = new QHBoxLayout;
        recorded_ = new QLabel("Loading history…");
        recorded_->setObjectName("recordedTimestamp");
        recorded_->setTextFormat(Qt::PlainText);
        caption->addWidget(recorded_, 1);
        copyStatus_ = new QLabel;
        copyStatus_->setObjectName("copyStatus");
        copyStatus_->setTextFormat(Qt::PlainText);
        copyStatus_->hide();
        caption->addWidget(copyStatus_);
        copyNoticeTimer_.setSingleShot(true);
        copyNoticeTimer_.setInterval(3000);
        connect(&copyNoticeTimer_, &QTimer::timeout, copyStatus_, &QWidget::hide);
        indexState_ = new QLabel;
        indexState_->setObjectName("indexState");
        indexState_->setTextFormat(Qt::PlainText);
        caption->addWidget(indexState_);
        layout->addLayout(caption);
        matchReadout_ = new QWidget;
        auto* matchLayout = new QHBoxLayout(matchReadout_);
        matchLayout->setContentsMargins(0, 0, 0, 0);
        matchLayout->setSpacing(18);
        matchLayout->addWidget(resultsHeading_);
        matchLayout->addStretch();
        previousMatch_ = new QPushButton("↑"); previousMatch_->setAccessibleName("Previous match");
        nextMatch_ = new QPushButton("↓"); nextMatch_->setAccessibleName("Next match");
        matchLayout->addWidget(previousMatch_); matchLayout->addWidget(nextMatch_);
        connect(previousMatch_, &QPushButton::clicked, this, [this] { stepMatch(-1); });
        connect(nextMatch_, &QPushButton::clicked, this, [this] { stepMatch(1); });
        layout->addWidget(matchReadout_);
        matchReadout_->hide();
        timeline_ = new TimelineView(colors);
        timeline_->activateMatch = [this](qint64 id) { search(false, -1, -1, id); };
        layout->addWidget(timeline_);
        layout->addWidget(results_);
        auto* controls = new QHBoxLayout;
        previous_ = new QPushButton("←"); previous_->setObjectName("earlierMoment");
        previous_->setAccessibleName("Earlier moment");
        next_ = new QPushButton("→"); next_->setObjectName("laterMoment");
        next_->setAccessibleName("Later moment");
        auto* fit = new QPushButton("Fit"); fit->setObjectName("fitImage");
        auto* actual = new QPushButton("100%"); actual->setObjectName("actualImageSize");
        for (auto* button : {previous_, next_, fit, actual}) controls->addWidget(button);
        controls->addStretch();
        auto* keys = new QLabel("/ search   ← → time   ↑ ↓ matches   ? help");
        keys->setObjectName("keyboardHelp");
        controls->addWidget(keys);
        layout->addLayout(controls);
        searchTimer_.setSingleShot(true);
        searchTimer_.setInterval(180);
        connect(&searchTimer_, &QTimer::timeout, this, [this] { search(); });
        connect(query_, &QLineEdit::textChanged, this, [this] { cancelSeek(); searchTimer_.start(); });
        connect(query_, &QLineEdit::returnPressed, this, [this] {
            search();
            if (query_->text().trimmed().isEmpty()) timeline_->setFocus(); else results_->setFocus();
        });

        connect(refresh, &QPushButton::clicked, this, [this] { search(true); });
        connect(results_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { openResult(); });
        connect(results_, &QListWidget::currentRowChanged, this, [this](int) { openResult(); });
        connect(results_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { openResult(); });
        connect(previous_, &QPushButton::clicked, this, [this] { stepTime(-1); });
        connect(next_, &QPushButton::clicked, this, [this] { stepTime(1); });
        connect(processMoment_, &QPushButton::clicked, this, [this] { requestMoment(false); });
        connect(catchUp_, &QPushButton::clicked, this, [this] { requestCatchUpNow(); });
        connect(serviceAction_, &QPushButton::clicked, this, [this] {
            requestService(serviceAction_->property("action").toString());
        });
        connect(stopService_, &QPushButton::clicked, this, [this] { requestService("stop"); });
        connect(copyWorkerCommand_, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(workerCommand());
            requestMessage_ = "Index command copied. Run it in a terminal to process queued requests.";
            updateIndexingActions();
        });
        connect(fit, &QPushButton::clicked, this, [this] { evidence_->setFit(true); });
        connect(actual, &QPushButton::clicked, this, [this] { evidence_->setFit(false); });
        auto* focusSearch = new QShortcut(QKeySequence::Find, this);
        connect(focusSearch, &QShortcut::activated, this, [this] { query_->setFocus(); query_->selectAll(); });
        auto* close = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        close->setAutoRepeat(false);
        connect(close, &QShortcut::activated, this, [this] { escape(); });
        auto* earlier = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
        connect(earlier, &QShortcut::activated, this, [this] { stepTime(-1); });
        auto* later = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
        connect(later, &QShortcut::activated, this, [this] { stepTime(1); });
        auto* refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
        connect(refreshShortcut, &QShortcut::activated, this, [this] { search(true); });

        seekTimer_.setSingleShot(true);
        seekTimer_.setInterval(35);
        connect(timeline_, &QSlider::valueChanged, this, [this](int value) { seekTime(timeline_->timeAt(value)); });
        connect(&seekTimer_, &QTimer::timeout, this, [this] { startSeek(); });
        connect(&seeker_, &QFutureWatcher<SeekResult>::finished, this, [this] {
            if (closing_->load()) return;
            const auto result = seeker_.result();
            if (result.revision != seekRevision_) { if (seekPending_) startSeek(); return; }
            if (result.frame) openFrame(*result.frame);
            else if (!result.error.isEmpty()) { mediaStatus_->setText(result.error); mediaStatus_->show(); }
        });
        connect(&highlightReader_, &QFutureWatcher<HighlightResult>::finished, this, [this] {
            if (closing_->load()) return;
            const auto result = highlightReader_.result();
            if (result.id != selected_.id || result.query != completedQuery_) { startHighlights(); return; }
            highlighted_ = result;
            evidence_->setHighlights(result.matches.boxes);
            setProperty("highlightCount", result.matches.boxes.size());
            indexState_->setToolTip(result.matches.boxes.isEmpty() && !completedQuery_.isEmpty()
                ? "No matching text positions stored for this moment. Older history remains searchable without highlights."
                : "Highlights mark recognized text lines, not exact word boundaries. M toggles highlights.");
        });

        dwellTimer_.setSingleShot(true);
        dwellTimer_.setInterval(500);
        connect(&dwellTimer_, &QTimer::timeout, this, [this] { requestMoment(true); });
        connect(&historyReader_, &QFutureWatcher<HistorySnapshot>::finished, this, [this] {
            if (closing_->load()) return;
            const auto snapshot = historyReader_.result();
            if (snapshot.generation != historyGeneration_) { startHistoryRead(); return; }
            setProperty("historyLoading", false);
            applyHistory(snapshot);
        });
        connect(&neighborReader_, &QFutureWatcher<TimelineNeighbors>::finished, this, [this] {
            if (closing_->load()) return;
            const auto neighbors = neighborReader_.result();
            if (neighbors.id != selected_.id) { startNeighborRead(); return; }
            previousFrame_ = neighbors.previous;
            nextFrame_ = neighbors.next;
            previous_->setEnabled(previousFrame_.has_value());
            next_->setEnabled(nextFrame_.has_value());
            if (!neighbors.error.isEmpty()) status_->setText("Timeline unavailable: " + neighbors.error);
        });
        connect(&indexRequest_, &QFutureWatcher<IndexingRequestResult>::finished, this, [this] {
            if (closing_->load()) return;
            const auto result = indexRequest_.result();
            setProperty("indexingRequestInFlight", false);
            if (!result.error.isEmpty()) {
                requestMessage_ = "Unable to queue indexing: " + result.error;
            } else {
                if (result.frameId) requestedMoments_.insert(result.frameId);
                if (result.catchUp) catchUpUntil_ = QDateTime::currentMSecsSinceEpoch() + 120000;
                requestMessage_ = result.catchUp ? "Two-minute catch-up request saved."
                    : QString("Moment request saved; %1 pending moments prioritized.").arg(result.changed);
            }
            updateIndexingActions();
            search(true);
            if (selected_.id != result.frameId && selected_.ocrState == "pending" &&
                !requestedMoments_.contains(selected_.id)) dwellTimer_.start();
        });

        connect(&serviceRequest_, &QFutureWatcher<ServiceControlResult>::finished, this, [this] {
            if (closing_->load()) return;
            const auto result = serviceRequest_.result();
            setProperty("serviceRequestInFlight", false);
            if (!result.error.isEmpty()) requestMessage_ = "Unable to change indexing: " + result.error;
            else {
                service_ = result.status;
                requestMessage_.clear();
            }
            updateIndexingActions();
            search(true);
        });

        connect(&decoder_, &QFutureWatcher<DecodedFrame>::finished, this, [this] {
            if (closing_->load()) return;
            const DecodedFrame result = decoder_.result();
            if (result.id == selected_.id) {
                setProperty("mediaLoading", false);
                if (result.image.isNull()) {
                    mediaStatus_->setText(result.error.isEmpty() ? "Recorded image is unavailable." : result.error);
                    mediaStatus_->show();
                } else {
                    evidence_->setImage(result.image);
                    setProperty("displayedFrameId", result.id);
                    mediaStatus_->clear();
                    mediaStatus_->hide();
                    evidence_->setToolTip(QString("Recorded screen · %1 × %2 · F fit / 1 original size").arg(result.image.width()).arg(result.image.height()));
                }
            } else {
                startDecode();
            }
        });
        for (QWidget* child : findChildren<QWidget*>()) child->installEventFilter(this);
        installEventFilter(this);
        updateNavigation();
        updateIndexingActions();
        search();
        refreshTimer_.setInterval(2000);
        connect(&refreshTimer_, &QTimer::timeout, this, [this] {
            if (isVisible() && !searchTimer_.isActive() && !historyReader_.isRunning()) search(true);
        });
        refreshTimer_.start();
        query_->setFocus();
    }

    ~Viewer() override { finishWork(); }

protected:
    void closeEvent(QCloseEvent* event) override {
        finishWork();
        QWidget::closeEvent(event);
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (object != query_ && key->modifiers() == Qt::ShiftModifier &&
                (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right || key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
                auto* scroll = (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)
                    ? evidence_->horizontalScrollBar() : evidence_->verticalScrollBar();
                const int direction = (key->key() == Qt::Key_Left || key->key() == Qt::Key_Up) ? -1 : 1;
                scroll->setValue(scroll->value() + direction * 100);
                return true;
            }
            if (object != query_ && key->key() == Qt::Key_C && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
                copyText(true);
                return true;
            }
            if (key->matches(QKeySequence::Copy) && object != query_) {
                copyText(false);
                return true;
            }
            if (object != query_ && key->key() == Qt::Key_Question) { helpToggle_->toggle(); return true; }
            if (key->modifiers() == Qt::NoModifier) {
                if (object != query_) {
                    switch (key->key()) {
                    case Qt::Key_Slash: query_->setFocus(); query_->selectAll(); return true;
                    case Qt::Key_Question: helpToggle_->toggle(); return true;
                    case Qt::Key_I: detailsToggle_->toggle(); return true;
                    case Qt::Key_PageDown: changePage(1); return true;
                    case Qt::Key_PageUp: changePage(-1); return true;
                    case Qt::Key_F: evidence_->setFit(true); return true;
                    case Qt::Key_1: evidence_->setFit(false); return true;
                    case Qt::Key_M: evidence_->toggleHighlights(); return true;
                    case Qt::Key_H: stepTime(-1); return true;
                    case Qt::Key_L: stepTime(1); return true;
                    case Qt::Key_Home: seekTime(overview_.firstTimestampMs); return true;
                    case Qt::Key_End: seekTime(overview_.lastTimestampMs); return true;
                    case Qt::Key_J: case Qt::Key_Down: if (completedQuery_.trimmed().isEmpty()) stepTime(1); else stepMatch(1); return true;
                    case Qt::Key_K: case Qt::Key_Up: if (completedQuery_.trimmed().isEmpty()) stepTime(-1); else stepMatch(-1); return true;
                    default: break;
                    }
                }
                if (object != query_ && key->key() == Qt::Key_P) {
                    requestMoment(false);
                    return true;
                }
                if (object != query_ && key->key() == Qt::Key_C) {
                    requestCatchUpNow();
                    return true;
                }
                if (object == query_ && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
                    searchTimer_.stop();
                    if (query_->text() != completedQuery_) search();
                    if (query_->text().trimmed().isEmpty()) timeline_->setFocus(); else results_->setFocus();
                    if (results_->currentRow() < 0 && results_->count()) results_->setCurrentRow(0);
                    return true;
                }
                if (object != query_ && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
                    stepTime(key->key() == Qt::Key_Left ? -1 : 1);
                    return true;
                }
            }
        }
        return QWidget::eventFilter(object, event);
    }

private:
    void escape() {
        if (query_->hasFocus() && query_->text() != completedQuery_) search();
        if (details_->isVisible() || help_->isVisible()) {
            detailsToggle_->setChecked(false);
            helpToggle_->setChecked(false);
            setFocus(Qt::OtherFocusReason);
            return;
        }
        if (focusWidget() && focusWidget() != this) {
            setFocus(Qt::OtherFocusReason);
            return;
        }
        close();
    }

    void copyNotice(const QString& text) {
        copyStatus_->setText(text);
        copyStatus_->show();
        copyNoticeTimer_.start();
    }

    void copyText(bool wholeScreen) {
        if (!selected_.id || property("displayedFrameId").toLongLong() != selected_.id) {
            copyNotice("Wait for the recorded image to load.");
            return;
        }
        const bool searching = !query_->text().trimmed().isEmpty();
        QString text = selected_.text;
        if (!wholeScreen && searching) {
            if (query_->text() != completedQuery_ || highlighted_.id != selected_.id || highlighted_.query != completedQuery_) {
                copyNotice("Matching text is loading. Try copying again.");
                return;
            }
            if (!highlighted_.error.isEmpty() || !highlighted_.matches.geometryAvailable) {
                copyNotice("Matching lines unavailable. Ctrl+Shift+C copies all screen text.");
                return;
            }
            text = highlighted_.matches.text;
        }
        if (text.trimmed().isEmpty()) {
            copyNotice(searching && !wholeScreen ? "No matching lines to copy." : "No recognized text to copy.");
            return;
        }
        QApplication::clipboard()->setText(text);
        copyNotice(searching && !wholeScreen ? "Matching lines copied" : "Screen text copied");
    }

    void finishWork() {
        if (closing_->exchange(true)) return;
        searchTimer_.stop();
        refreshTimer_.stop();
        dwellTimer_.stop();
        seekTimer_.stop();
        copyNoticeTimer_.stop();
        // Value-captured jobs never access widgets. Stop queued jobs, cancel an
        // active decoder, and let a started SQLite transaction finish (the core
        // uses a one-second busy timeout) before the dataset/viewer can go away.
        indexRequest_.waitForFinished();
        serviceRequest_.waitForFinished();
        historyReader_.waitForFinished();
        neighborReader_.waitForFinished();
        decoder_.waitForFinished();
        seeker_.waitForFinished();
        highlightReader_.waitForFinished();
        setProperty("indexingRequestInFlight", false);
        setProperty("serviceRequestInFlight", false);
        setProperty("historyLoading", false);
        setProperty("mediaLoading", false);
    }

    QString indexingNotice() const {
        QStringList notices;
        if (pending_ > 0)
            notices << QString("%1 saved moments have text indexing pending; search is incomplete.").arg(pending_);
        if (failed_ > 0)
            notices << QString("Text indexing failed for %1 saved moments.").arg(failed_);
        if (disabled_ > 0)
            notices << QString("Text indexing is disabled for %1 saved moments.").arg(disabled_);
        if (legacy_)
            notices << "Older dataset: original indexing status was not recorded.";
        return notices.join(' ');
    }

    void search(bool preserve = false, int targetRow = -1, qint64 requestedOffset = -1, qint64 anchorFrameId = 0) {
        if (closing_->load()) return;
        const bool newQuery = query_->text() != completedQuery_;
        if (newQuery) { requestedOffset = 0; targetRow = -1; anchorFrameId = 0; }
        else if (property("historyLoading").toBool() && requestedOffset < 0 && !anchorFrameId) {
            // A repeated search or refresh must keep the in-flight destination;
            // pageOffset_ still describes the previously displayed query/page.
            requestedOffset = requestedHistory_.offset;
            anchorFrameId = requestedHistory_.anchorFrameId;
            if (targetRow < 0) targetRow = requestedHistory_.targetRow;
            preserve = preserve && requestedHistory_.preserve;
        }
        preserve = preserve && !newQuery;
        searchTimer_.stop();
        if (!preserve) { dwellTimer_.stop(); cancelSeek(); }
        completedQuery_ = query_->text();
        requestedHistory_ = {};
        requestedHistory_.generation = ++historyGeneration_;
        requestedHistory_.selectionRevision = selectionRevision_;
        requestedHistory_.query = completedQuery_;
        requestedHistory_.offset = requestedOffset >= 0 ? requestedOffset : pageOffset_;
        requestedHistory_.anchorFrameId = anchorFrameId;
        requestedHistory_.targetRow = targetRow;
        requestedHistory_.preserve = preserve;
        requestedHistory_.currentId = preserve ? selected_.id : 0;
        setProperty("historyLoading", true);
        if (!historyReader_.isRunning()) startHistoryRead();
    }

    void startHistoryRead() {
        if (closing_->load()) return;
        const QString directory = directory_;
        const auto request = requestedHistory_;
        const auto closing = closing_;
        historyReader_.setFuture(QtConcurrent::run([directory, request, closing]() mutable {
            auto result = request;
            try {
                if (closing->load()) return result;
                result.indexing = indexingStatus(directory);
                try {
                    result.service = indexServiceStatus(directory);
                    result.policy = savedIndexPolicy(directory);
                } catch (const std::exception& error) {
                    result.service["state"] = "error";
                    result.service["error"] = QString::fromUtf8(error.what());
                }
                if (closing->load()) return result;
                result.timeline = timelineOverview(directory);
                if (result.query.trimmed().isEmpty()) result.matches = listFrames(directory, 200, 0);
                else {
                    result.page = searchFramePage(directory, result.query, 100, result.offset, SearchMode::PrefixLastToken, 1000, result.anchorFrameId);
                    result.matches = result.page.frames;
                }
                if (!closing->load() && result.currentId) result.current = frameById(directory, result.currentId);
                else if (!closing->load() && result.query.trimmed().isEmpty() && result.timeline.totalFrames)
                    result.current = frameNearTimestamp(directory, result.timeline.lastTimestampMs);
            } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
    }

    void applyHistory(const HistorySnapshot& snapshot) {
        if (query_->text() != snapshot.query) { search(); return; }
        const bool preserve = snapshot.preserve;
        const bool navigated = snapshot.selectionRevision != selectionRevision_;
        const qint64 resultId = results_->currentItem() ? results_->currentItem()->data(Qt::UserRole).toLongLong() : 0;
        const int scroll = results_->horizontalScrollBar()->value();
        if (snapshot.error.isEmpty()) {
            const auto& index = snapshot.indexing;
            legacy_ = index.value("legacy_schema").toBool();
            pending_ = index.value("pending").toInteger();
            failed_ = index.value("failed").toInteger();
            disabled_ = index.value("disabled").toInteger();
            priorityPending_ = index.value("priority_pending").toInteger();
            catchUpUntil_ = index.value("catch_up_until_ms").toInteger();
            indexerRunning_ = index.value("indexer_running").toBool();
            oldestPendingMs_ = index.value("index_lag_ms").toInteger();
            service_ = snapshot.service;
            servicePolicy_ = snapshot.policy;
            matches_ = snapshot.matches;
            overview_ = snapshot.timeline;
            timeline_->setOverview(overview_);
            totalMatches_ = snapshot.page.totalMatches;
            pageOffset_ = snapshot.page.offset;
            timeline_->setMatches(snapshot.page.timeline);
            setProperty("totalMatches", totalMatches_);
            setProperty("matchPageOffset", pageOffset_);
            const QSignalBlocker blockResults(results_);
            results_->clear();
            int selectedRow = snapshot.page.selectedRow >= 0 ? snapshot.page.selectedRow : std::max(0, snapshot.targetRow);
            for (const FrameRecord& frame : matches_) {
                auto* item = new QListWidgetItem(QDateTime::fromMSecsSinceEpoch(frame.timestampMs).toString("HH:mm:ss"));
                item->setData(Qt::UserRole, frame.id);
                item->setToolTip(QDateTime::fromMSecsSinceEpoch(frame.timestampMs).toString("MMM d · HH:mm:ss"));
                item->setSizeHint(QSize(108, 28));
                if ((preserve || navigated) && frame.id == resultId) selectedRow = results_->count();
                results_->addItem(item);
            }
            const bool recent = completedQuery_.trimmed().isEmpty();
            resultsHeading_->setText(QString("%1 matches").arg(totalMatches_));
            results_->setVisible(!recent && !matches_.isEmpty());
            resultsHeading_->setVisible(!recent);
            matchReadout_->setVisible(!recent);
            ready_ = index.value("ready").toInteger();
            totalFrames_ = index.value("coverage_total_frames").toInteger();
            detailsToggle_->setText(pending_ > 0 ? QString("%1 pending").arg(pending_) : failed_ > 0
                ? QString("%1 failed").arg(failed_) : "Index");
            detailsToggle_->setToolTip(indexingNotice().isEmpty() ? "Search indexing status (I)" : indexingNotice());
            if (!matches_.isEmpty()) results_->setCurrentRow(std::min(selectedRow, int(matches_.size()) - 1));
            if (preserve || navigated) results_->horizontalScrollBar()->setValue(scroll);
            if (navigated && selected_.id) {
                const auto found = std::find_if(matches_.begin(), matches_.end(), [this](const auto& row) { return row.id == selected_.id; });
                if (found != matches_.end()) openFrame(*found);
                else describeFrame();
            } else if (snapshot.current && snapshot.targetRow < 0) {
                openFrame(*snapshot.current);
            } else if (matches_.isEmpty()) {
                selected_ = {};
                ++selectionRevision_;
                dwellTimer_.stop();
                setProperty("selectedFrameId", 0);
                setProperty("displayedFrameId", 0);
                evidence_->setImage({});
                indexState_->clear();
                recorded_->setText(recent ? "No recorded history in this dataset" : "No matching recorded text");
                mediaStatus_->show();
                mediaStatus_->setText(pending_ > 0 ? "Saved images are not searchable yet. Clear the search to browse them while indexing is pending."
                    : failed_ > 0 ? "Text indexing failed for saved images. Clear the search to browse the recorded images."
                    : recent ? "Run the controlled demo to create synthetic history."
                             : "Try a shorter word or another spelling. This does not prove it never appeared.");
                updateNavigation();
            } else {
                openResult();
            }
        } else {
            if (preserve) {
                status_->setText("Unable to refresh history: " + snapshot.error);
                return;
            }
            results_->clear();
            matches_.clear();
            selected_ = {};
            ++selectionRevision_;
            dwellTimer_.stop();
            setProperty("selectedFrameId", 0);
            setProperty("displayedFrameId", 0);
            evidence_->setImage({});
            indexState_->clear();
            resultsHeading_->setText("History unavailable");
            recorded_->setText("Unable to read this local dataset");
            mediaStatus_->setText(snapshot.error);
            mediaStatus_->show();
            status_->setText(directory_);
            updateNavigation();
        }
        updateIndexingActions();
        updateMatchReadout();
    }

    void openResult() {
        cancelSeek();
        const int row = results_->currentRow();
        if (row >= 0 && row < matches_.size()) openFrame(matches_[row]);
    }

    void describeFrame() {
        const auto& frame = selected_;
        recorded_->setText(QDateTime::fromMSecsSinceEpoch(frame.timestampMs).toString("ddd, MMM d · HH:mm:ss"));
        if (legacy_) indexState_->setText("Indexing status not recorded");
        else if (frame.ocrState == "pending") indexState_->setText("Saved · text pending");
        else if (frame.ocrState == "failed") indexState_->setText("Saved · indexing failed");
        else if (frame.ocrState == "disabled") indexState_->setText("Saved · indexing disabled");
        else indexState_->setText(frame.text.trimmed().isEmpty() ? "No text recognized" : QString());
        indexState_->setVisible(!indexState_->text().isEmpty());
        indexState_->setToolTip(frame.ocrError);
        timeline_->setMoment(frame.timestampMs);
        updateMatchReadout();
    }

    void openFrame(const FrameRecord& frame) {
        const bool changed = selected_.id != frame.id;
        const bool keepImage = selected_.id == frame.id && selected_.available == frame.available;
        if (changed) { ++selectionRevision_; dwellTimer_.stop(); }
        selected_ = frame;
        setProperty("selectedFrameId", frame.id);
        if (!completedQuery_.trimmed().isEmpty()) {
            const auto found = std::find_if(matches_.cbegin(), matches_.cend(), [this](const auto& row) { return row.id == selected_.id; });
            if (found != matches_.cend()) {
                const int row = int(std::distance(matches_.cbegin(), found));
                if (results_->currentRow() != row) {
                    const QSignalBlocker blocker(results_);
                    results_->setCurrentRow(row);
                    results_->scrollToItem(results_->currentItem());
                }
            }
        }
        describeFrame();
        startHighlights();
        updateNavigation(changed);
        updateIndexingActions();
        if (changed && frame.ocrState == "pending" && !requestedMoments_.contains(frame.id)) dwellTimer_.start();
        if (keepImage) return;
        setProperty("displayedFrameId", 0);
        evidence_->setImage({});
        mediaStatus_->show();
        mediaStatus_->setText(frame.available ? "Loading recorded image…" : "Recorded media is not available yet.");
        if (!decoder_.isRunning()) startDecode();
    }

    void startDecode() {
        if (closing_->load()) return;
        if (selected_.id <= 0 || !selected_.available) {
            setProperty("mediaLoading", false);
            return;
        }
        const QString directory = directory_;
        const qint64 id = selected_.id;
        const auto closing = closing_;
        setProperty("mediaLoading", true);
        decoder_.setFuture(QtConcurrent::run([directory, id, closing] {
            DecodedFrame result;
            result.id = id;
            try { result.image = loadFrame(directory, id, [closing] { return closing->load(); }); }
            catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
    }

    void updateNavigation(bool reset = true) {
        if (reset) {
            previousFrame_.reset();
            nextFrame_.reset();
            previous_->setEnabled(false);
            next_->setEnabled(false);
        }
        if (!neighborReader_.isRunning()) startNeighborRead();
    }

    void startNeighborRead() {
        if (closing_->load() || selected_.id <= 0) return;
        const QString directory = directory_;
        const qint64 id = selected_.id;
        const auto closing = closing_;
        neighborReader_.setFuture(QtConcurrent::run([directory, id, closing] {
            TimelineNeighbors result;
            result.id = id;
            try {
                if (closing->load()) return result;
                result.previous = adjacentFrame(directory, id, -1);
                if (!closing->load()) result.next = adjacentFrame(directory, id, 1);
            } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
    }

    QString workerCommand() const {
        const auto quote = [](QString value) { value.replace('\'', "'\\''"); return '\'' + value + '\''; };
        return quote(QCoreApplication::applicationFilePath()) + " index --dir " + quote(directory_) +
            " --follow --scheduler adaptive --ocr-cpu-percent 10 --ocr-max-wall-ms 60000";
    }

    void updateIndexingActions() {
        const bool busy = indexRequest_.isRunning();
        const bool serviceBusy = serviceRequest_.isRunning();
        const bool configured = !servicePolicy_.isEmpty();
        const bool paused = service_.value("paused").toBool();
        const bool running = service_.value("running").toBool();
        const bool external = service_.value("external_worker").toBool();
        const bool fixed = servicePolicy_.value("scheduler").toString() == "fixed";
        const bool catchUpActive = catchUpUntil_ > QDateTime::currentMSecsSinceEpoch();
        QStringList counts;
        counts << QString("%1 of %2 moments ready").arg(ready_).arg(totalFrames_);
        if (pending_ > 0) counts << QString("%1 pending").arg(pending_);
        if (failed_ > 0) counts << QString("%1 failed").arg(failed_);
        if (disabled_ > 0) counts << QString("%1 indexing disabled").arg(disabled_);
        if (legacy_) counts << "Original indexing status not recorded";
        status_->setText(counts.join("  ·  "));
        indexProgress_->setValue(totalFrames_ > 0 ? int(1000. * ready_ / totalFrames_) : 0);
        indexProgress_->setVisible(totalFrames_ > 0 && !legacy_);
        processMoment_->setVisible(!legacy_ && selected_.ocrState == "pending");
        catchUp_->setVisible(!legacy_ && pending_ > 0);
        processMoment_->setEnabled(!legacy_ && selected_.ocrState == "pending" && !busy);
        catchUp_->setEnabled(!legacy_ && pending_ > 0 && !busy && !catchUpActive && !paused && !fixed);
        catchUp_->setToolTip(fixed ? "This history uses a fixed CPU allowance; catch-up requires adaptive scheduling"
            : "Request two minutes of faster indexing when resources permit");
        copyWorkerCommand_->setVisible(pending_ > 0 && !indexerRunning_ && !configured &&
            service_.value("policy_error").toString().isEmpty());
        copyWorkerCommand_->setToolTip(workerCommand());
        serviceAction_->setVisible(configured && !legacy_);
        serviceAction_->setText(paused ? "Resume" : running ? "Pause" : "Start indexing");
        serviceAction_->setProperty("action", paused ? "resume" : running ? "pause" : "start");
        serviceAction_->setEnabled(!serviceBusy);
        stopService_->setVisible(!legacy_ && (running || service_.value("enabled").toBool()));
        stopService_->setEnabled(!serviceBusy);
        QStringList messages;
        if (serviceBusy) messages << "Updating background indexing…";
        else if (busy) messages << "Saving indexing request…";
        else if (!requestMessage_.isEmpty()) messages << requestMessage_;
        if (priorityPending_ > 0) messages << QString("%1 pending moments are prioritized.").arg(priorityPending_);
        if (catchUpActive) messages << "Catch-up requested until " + QDateTime::fromMSecsSinceEpoch(catchUpUntil_).toString("HH:mm:ss") + ".";
        priorityStatus_->setText(messages.join(' '));
        priorityStatus_->setVisible(!messages.isEmpty());
        const QString state = service_.value("state").toString();
        QString activity;
        if (state == "error") {
            const QString problem = service_.value("error").toString();
            activity = "Indexing needs attention: " + (problem.isEmpty()
                ? service_.value("reason").toString("Check pending and failed moments.") : problem);
        }
        else if (configured && !running && !indexerRunning_ && !service_.value("enabled").toBool())
            activity = "Background indexing is stopped.";
        else if (paused) activity = external ? "Background service paused; another worker is still indexing."
            : service_.value("worker_running").toBool() ? "Pausing background indexing…"
            : "Paused. Saved moments will wait until you resume.";
        else if (external) activity = "Another worker is indexing this history.";
        else if (state == "pressure") activity = "Working gently while the computer is busy.";
        else if (state == "requested") activity = "Catching up on requested moments.";
        else if (state == "idle" && pending_ > 0) activity = "Catching up while the computer is idle.";
        else if (running && pending_ == 0 && failed_ > 0) activity = "No pending work. Failed moments still need attention.";
        else if (running && pending_ == 0 && disabled_ > 0) activity = "No pending work. Some moments have indexing disabled.";
        else if (running && pending_ == 0) activity = "Up to date. Background indexing is ready for new moments.";
        else if (state == "waiting" && running) activity = "Waiting to start the next indexing pass.";
        else if (state == "unknown" && indexerRunning_)
            activity = service_.value("effective_cpu_percent").isDouble()
                ? "Working gently; activity signals are unavailable."
                : "Indexing; waiting for a fresh activity update.";
        else if (indexerRunning_) activity = "Processing in the background.";
        else if (running) activity = "Background indexing is starting.";
        else if (pending_ > 0 || configured) activity = "Background indexing is stopped.";
        QStringList details;
        if (!activity.isEmpty()) details << activity;
        const auto budget = service_.value("effective_cpu_percent");
        const qint64 policyAge = service_.value("policy_age_ms").toInteger(-1);
        if (budget.isDouble() && policyAge >= 0 && policyAge < 15000 && indexerRunning_)
            details << (budget.toDouble() == 0 ? "OCR has no pacing cap."
                : QString("CPU allowance: %1% of one core.").arg(budget.toDouble(), 0, 'g', 3));
        const auto resources = service_.value("worker_policy").toObject().value("resources").toObject();
        if (indexerRunning_ && policyAge >= 0 && policyAge < 15000) {
            if (resources.value("enforced").toBool())
                details << QString("Worker ceiling: %1% of one core; low priority.")
                    .arg(resources.value("effective_cpu_percent").toDouble(), 0, 'g', 3);
            else if (resources.value("state").toString() == "unavailable")
                details << "Worker ceiling unavailable; OCR pacing and low priority remain active.";
        }
        if (pending_ > 0) details << "Oldest waiting moment: " + elapsedDescription(oldestPendingMs_) + ".";
        workerHint_->setText(details.join(' '));
        workerHint_->setToolTip(service_.value("recovery").toString());
        workerHint_->setVisible(!details.isEmpty());
    }

    void requestService(const QString& action) {
        if (closing_->load() || serviceRequest_.isRunning() || (servicePolicy_.isEmpty() && action != "stop")) return;
        const QString directory = directory_;
        const auto closing = closing_;
        setProperty("serviceRequestInFlight", true);
        serviceRequest_.setFuture(QtConcurrent::run([directory, action, closing] {
            ServiceControlResult result;
            result.action = action;
            try { if (!closing->load()) result.status = controlIndexService(directory, action); }
            catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
        updateIndexingActions();
    }

    void requestMoment(bool automatic) {
        if (closing_->load() || !isVisible() || legacy_ || selected_.id <= 0 || selected_.ocrState != "pending") return;
        if (automatic && requestedMoments_.contains(selected_.id)) return;
        if (indexRequest_.isRunning()) return;
        dwellTimer_.stop();
        const QString directory = directory_;
        const qint64 id = selected_.id;
        const auto closing = closing_;
        setProperty("indexingRequestInFlight", true);
        indexRequest_.setFuture(QtConcurrent::run([directory, id, closing] {
            IndexingRequestResult result;
            result.frameId = id;
            try { if (!closing->load()) result.changed = requestIndexing(directory, id, 15); }
            catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
        updateIndexingActions();
    }

    void requestCatchUpNow() {
        if (closing_->load() || !catchUp_->isEnabled() || indexRequest_.isRunning()) return;
        const QString directory = directory_;
        const auto closing = closing_;
        setProperty("indexingRequestInFlight", true);
        indexRequest_.setFuture(QtConcurrent::run([directory, closing] {
            IndexingRequestResult result;
            result.catchUp = true;
            try { if (!closing->load()) result.changed = requestCatchUp(directory, 120); }
            catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
        updateIndexingActions();
    }

    void cancelSeek() {
        ++seekRevision_;
        seekPending_ = false;
        seekTimer_.stop();
    }

    void seekTime(qint64 timestamp) {
        seekPending_ = true;
        seekTarget_ = timestamp;
        ++seekRevision_;
        ++selectionRevision_;
        dwellTimer_.stop();
        seekTimer_.start();
    }

    void startSeek() {
        if (closing_->load() || seeker_.isRunning() || !seekPending_) return;
        seekPending_ = false;
        const QString directory = directory_;
        const auto revision = seekRevision_;
        const auto target = seekTarget_;
        const auto closing = closing_;
        seeker_.setFuture(QtConcurrent::run([directory, revision, target, closing] {
            SeekResult result; result.revision = revision;
            try { if (!closing->load()) result.frame = frameNearTimestamp(directory, target); }
            catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
    }

    void startHighlights() {
        highlighted_ = {};
        evidence_->setHighlights({});
        setProperty("highlightCount", 0);
        if (closing_->load() || highlightReader_.isRunning() || selected_.id <= 0 || completedQuery_.trimmed().isEmpty()) return;
        const QString directory = directory_, query = completedQuery_;
        const qint64 id = selected_.id;
        const auto closing = closing_;
        highlightReader_.setFuture(QtConcurrent::run([directory, id, query, closing] {
            HighlightResult result; result.id = id; result.query = query;
            try { if (!closing->load()) result.matches = matchingTextLines(directory, id, query, SearchMode::PrefixLastToken); }
            catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
            return result;
        }));
    }

    void updateMatchReadout() {
        if (completedQuery_.trimmed().isEmpty()) return;
        const auto found = std::find_if(matches_.cbegin(), matches_.cend(), [this](const auto& frame) { return frame.id == selected_.id; });
        if (found != matches_.cend()) {
            const auto ordinal = pageOffset_ + std::distance(matches_.cbegin(), found) + 1;
            resultsHeading_->setText(QString("%1 / %2 matches").arg(ordinal).arg(totalMatches_));
        } else {
            resultsHeading_->setText(QString("%1 matches").arg(totalMatches_));
        }
        const qint64 ordinal = pageOffset_ + std::max(0, results_->currentRow());
        previousMatch_->setEnabled(totalMatches_ > 0 && ordinal > 0);
        nextMatch_->setEnabled(totalMatches_ > 0 && ordinal + 1 < totalMatches_);
    }

    void changePage(int direction) {
        if (property("historyLoading").toBool() && !requestedHistory_.preserve) return;
        if (completedQuery_.trimmed().isEmpty() || totalMatches_ <= 0) return;
        const auto target = pageOffset_ + direction * 100;
        if (target < 0 || target >= totalMatches_) return;
        search(false, direction < 0 ? 99 : 0, target);
    }

    void stepMatch(int direction) {
        if (property("historyLoading").toBool() && !requestedHistory_.preserve) return;
        if (matches_.isEmpty()) return;
        const int target = results_->currentRow() + direction;
        if (target < 0 && pageOffset_ > 0) { changePage(-1); return; }
        if (target >= matches_.size() && pageOffset_ + matches_.size() < totalMatches_) { changePage(1); return; }
        results_->setCurrentRow(std::clamp(target, 0, int(matches_.size()) - 1));
        results_->scrollToItem(results_->currentItem());
        openResult();
    }

    void stepTime(int direction) {
        cancelSeek();
        const auto neighbor = direction < 0 ? previousFrame_ : nextFrame_;
        if (neighbor) openFrame(*neighbor);
    }

    TimelineView* timeline_ = nullptr;
    TimelineOverview overview_;
    QWidget* details_ = nullptr;
    QWidget* help_ = nullptr;
    QWidget* matchReadout_ = nullptr;
    QPushButton* helpToggle_ = nullptr;
    QPushButton* previousMatch_ = nullptr;
    QPushButton* nextMatch_ = nullptr;
    QProgressBar* indexProgress_ = nullptr;
    qint64 ready_ = 0, totalFrames_ = 0, totalMatches_ = 0, pageOffset_ = 0;
    QPushButton* detailsToggle_ = nullptr;
    QTimer seekTimer_;
    quint64 seekRevision_ = 0;
    qint64 seekTarget_ = 0;
    bool seekPending_ = false;
    QFutureWatcher<SeekResult> seeker_;
    QFutureWatcher<HighlightResult> highlightReader_;
    HighlightResult highlighted_;
    QString directory_;
    std::shared_ptr<std::atomic_bool> closing_ = std::make_shared<std::atomic_bool>(false);
    QString completedQuery_;
    qint64 pending_ = 0, failed_ = 0, disabled_ = 0, priorityPending_ = 0, catchUpUntil_ = 0;
    quint64 historyGeneration_ = 0, selectionRevision_ = 0;
    bool legacy_ = false;
    bool indexerRunning_ = false;
    qint64 oldestPendingMs_ = 0;
    QJsonObject service_, servicePolicy_;
    QString requestMessage_;
    QSet<qint64> requestedMoments_;
    HistorySnapshot requestedHistory_;
    QLineEdit* query_ = nullptr;
    QListWidget* results_ = nullptr;
    QLabel* resultsHeading_ = nullptr;
    QLabel* recorded_ = nullptr;
    QLabel* indexState_ = nullptr;
    QLabel* copyStatus_ = nullptr;
    QLabel* mediaStatus_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* priorityStatus_ = nullptr;
    QLabel* workerHint_ = nullptr;
    EvidenceView* evidence_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* processMoment_ = nullptr;
    QPushButton* catchUp_ = nullptr;
    QPushButton* copyWorkerCommand_ = nullptr;
    QPushButton* serviceAction_ = nullptr;
    QPushButton* stopService_ = nullptr;
    QTimer searchTimer_;
    QTimer refreshTimer_;
    QTimer dwellTimer_;
    QTimer copyNoticeTimer_;
    QFutureWatcher<DecodedFrame> decoder_;
    QFutureWatcher<HistorySnapshot> historyReader_;
    QFutureWatcher<TimelineNeighbors> neighborReader_;
    QFutureWatcher<IndexingRequestResult> indexRequest_;
    QFutureWatcher<ServiceControlResult> serviceRequest_;
    QVector<FrameRecord> matches_;
    std::optional<FrameRecord> previousFrame_;
    std::optional<FrameRecord> nextFrame_;
    FrameRecord selected_;
};

}  // namespace

std::unique_ptr<QWidget> createViewer(const QString& datasetDirectory) {
    return std::make_unique<Viewer>(datasetDirectory);
}

int showViewer(const QString& datasetDirectory) {
    auto viewer = createViewer(datasetDirectory);
    viewer->show();
    return QApplication::exec();
}

}  // namespace replay
