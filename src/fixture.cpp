#include "fixture.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <array>

namespace replay {
namespace {

constexpr std::array<int, 16> scenes{0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9, 9, 10, 10};

int normalizedIndex(int index) {
    const int count = static_cast<int>(scenes.size());
    return ((index % count) + count) % count;
}

void text(QPainter& p, int x, int y, const QString& value, int pixels = 26,
          bool bold = false, bool mono = false) {
    QFont font(mono ? "DejaVu Sans Mono" : "DejaVu Sans");
    font.setPixelSize(pixels);
    font.setBold(bold);
    p.setFont(font);
    p.drawText(x, y, value);
}

void chrome(QPainter& p, const QString& title, bool dark = false) {
    p.fillRect(QRect(0, 0, 1920, 1080), dark ? QColor("#131922") : QColor("#eef0ed"));
    p.fillRect(QRect(0, 0, 1920, 70), dark ? QColor("#222b38") : QColor("#dce2dd"));
    p.setPen(dark ? QColor("#9fdbbd") : QColor("#1b5541"));
    text(p, 48, 45, "OMARCHY REPLAY  /  SYNTHETIC TEST SCREEN", 22, true);
    p.setPen(dark ? QColor("#e9eef4") : QColor("#202b26"));
    text(p, 80, 140, title, 38, true);
    p.setPen(dark ? QColor("#a5b1bf") : QColor("#52655a"));
    text(p, 48, 1040, "Entirely fictional content. No personal applications or data are shown.", 20);
}

QStringList tokensForScene(int scene) {
    switch (scene) {
    case 0: return {"Patrick", "XYZ-1042", "Northwind"};
    case 1: return {"Patrick", "XYZ-1043", "Northwind"};
    case 2: return {"Omakase", "cedar", "fixtures"};
    case 3: return {"Omakase", "juniper", "completed"};
    case 4: return {"SCROLLMARK-ALPHA", "delivery", "reference"};
    case 5: return {"SCROLLMARK-OMEGA", "collection", "Friday"};
    case 6: return {"SMALLTEXT-LIGHT", "INV-8041", "EUR"};
    case 7: return {"SMALLTEXT-DARK", "EDGE-7F3", "retry"};
    case 8: return {"SMALLTEXT-DARK", "EDGE-7F8", "retry"};
    case 9: return {"NOTICE-732", "Patrick", "ready"};
    default: return {"FINISH-204", "Synthetic", "complete"};
    }
}

class FixtureWindow final : public QWidget {
public:
    explicit FixtureWindow(QSize sourceSize, bool editing) : sourceSize_(sourceSize), editing_(editing) {
        setWindowTitle("Omarchy Replay — synthetic fixture");
        setObjectName("syntheticFixture");
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        resize(sourceSize_);
        frame_ = fixtureFrame(0, sourceSize_, editing_);
    }

    void setFrame(int index) {
        frame_ = fixtureFrame(index, sourceSize_, editing_);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        const QSize fitted = frame_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - fitted.width()) / 2,
                                  (height() - fitted.height()) / 2), fitted);
        p.drawImage(target, frame_);
    }

private:
    QSize sourceSize_;
    bool editing_;
    QImage frame_;
};

}  // namespace

int fixtureFrameCount() { return static_cast<int>(scenes.size()); }

QImage fixtureFrame(int index, QSize size, bool editing) {
    if (size.width() < 1 || size.height() < 1) return {};
    const int scene = editing ? normalizedIndex(index) % 2 : scenes[normalizedIndex(index)];
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(size.width() / 1920.0, size.height() / 1080.0);

    if (scene <= 1) {
        chrome(p, "Messages  /  Northwind workshop");
        p.fillRect(QRect(80, 195, 1760, 740), QColor("#ffffff"));
        p.setPen(QColor("#1c3027"));
        text(p, 125, 265, "Patrick", 32, true);
        text(p, 125, 320, "Could you send the xyz invoice for the workshop?", 29);
        text(p, 125, 400, "You", 32, true);
        const QString invoice = scene == 0 ? "XYZ-1042" : "XYZ-1043";
        text(p, 125, 455, "Here is invoice " + invoice + ". The total is EUR 1,240.00.", 29);
        text(p, 125, 535, "Patrick", 32, true);
        text(p, 125, 590, "Thanks. Please use purchase order NORTH-81.", 29);
        p.setPen(QColor("#486655"));
        text(p, 125, 735, "Attachment: " + invoice + ".pdf", 27, true);
        text(p, 125, 790, "Fictional invoice — no payment or message will be sent.", 22);
    } else if (scene <= 3) {
        chrome(p, "Terminal", true);
        p.setPen(QColor("#a9e2b9"));
        const QString project = scene == 2 ? "cedar" : "juniper";
        text(p, 80, 235, "demo@fixture:/demo/" + project + "$", 27, true, true);
        p.setPen(QColor("#e3e9f0"));
        text(p, 80, 310, "Omakase", 34, true, true);
        text(p, 80, 380, "Reviewing synthetic project " + project, 27, false, true);
        text(p, 80, 445, "[pass] fixtures contain no personal data", 26, false, true);
        text(p, 80, 500, "[pass] keyboard navigation completed", 26, false, true);
        text(p, 80, 555, "[pass] invoice parser accepts XYZ-1043", 26, false, true);
        p.setPen(QColor("#94a6bb"));
        text(p, 80, 695, "This is drawn text, not an executing terminal.", 24, false, true);
    } else if (scene <= 5) {
        chrome(p, "Workshop notes  /  scroll fixture");
        p.fillRect(QRect(80, 200, 1710, 735), Qt::white);
        p.setPen(QColor("#263a30"));
        const int firstLine = scene == 4 ? 1 : 9;
        text(p, 120, 265, scene == 4 ? "SCROLLMARK-ALPHA" : "SCROLLMARK-OMEGA", 30, true);
        const QStringList lines{
            "Confirm the delivery address before printing the reference sheet.",
            "Northwind workshop: all example names and numbers are fictional.",
            "Keep the invoice reference visible in the handover notes.",
            "The collection window is Friday afternoon after the demonstration.",
            "Review the packing list with Patrick when the sample is ready.",
            "Record the completed checklist and return to the design notes.",
            "No account, network connection, or external service is involved."};
        for (int i = 0; i < lines.size(); ++i) {
            text(p, 120, 350 + i * 72,
                 QString("%1. %2").arg(firstLine + i, 2, 10, QChar('0')).arg(lines[i]), 26);
        }
        p.fillRect(QRect(1808, 200, 10, 735), QColor("#cbd5cd"));
        p.fillRect(QRect(1808, scene == 4 ? 200 : 630, 10, 250), QColor("#5f7a69"));
    } else if (scene <= 8) {
        const bool dark = scene != 6;
        chrome(p, "Small text accuracy fixture", dark);
        p.setPen(dark ? QColor("#edf1f5") : QColor("#1c2921"));
        text(p, 80, 250, dark ? "SMALLTEXT-DARK" : "SMALLTEXT-LIGHT", 30, true);
        text(p, 80, 335, "Keep the small identifiers searchable without changing their digits.", 25);
        text(p, 80, 420, "24 px: invoice INV-8041, total EUR 128.50", 24, false, true);
        text(p, 80, 475, "20 px: customer reference CEDAR-19 / retry pending", 20, false, true);
        text(p, 80, 530, "16 px: " + QString(scene == 8 ? "EDGE-7F8" : "EDGE-7F3") + " / retry 03 / latency 24ms", 16, false, true);
        text(p, 80, 575, "14 px: case-sensitive token Ax9K2 and punctuation : / - _", 14, false, true);
        p.setPen(dark ? QColor("#a1c9ff") : QColor("#355f8c"));
        text(p, 80, 680, "Colored text: blue-reference-602", 20, false, true);
    } else if (scene == 9) {
        chrome(p, "Synthetic notification fixture");
        p.setPen(QColor("#25392e"));
        text(p, 80, 280, "The fictional workspace remains visible behind this notice.", 28);
        p.fillRect(QRect(990, 375, 820, 285), QColor("#ffffff"));
        p.setPen(QColor("#1b5541"));
        text(p, 1030, 440, "NOTICE-732", 29, true);
        p.setPen(QColor("#25392e"));
        text(p, 1030, 510, "Patrick: the sample is ready.", 28);
        text(p, 1030, 578, "This notice is drawn into the fixture image.", 23);
    } else {
        chrome(p, "Synthetic sequence complete");
        p.setPen(QColor("#25392e"));
        text(p, 80, 280, "FINISH-204", 34, true);
        text(p, 80, 360, "Search for Patrick, XYZ-1043, Omakase, or SCROLLMARK-OMEGA.", 29);
        text(p, 80, 430, "Every screen in this dataset was generated for the feasibility test.", 27);
    }
    p.end();
    return image;
}

QJsonObject fixtureGroundTruth(int index, bool editing) {
    const int normalized = normalizedIndex(index);
    const int scene = editing ? normalized % 2 : scenes[normalized];
    QJsonArray tokens;
    for (const QString& token : tokensForScene(scene)) tokens.append(token);
    QJsonObject result{{"frame", normalized}, {"scene", scene}, {"synthetic", true},
                       {"tokens", tokens}};
    if (!editing && normalized > 0 && scenes[normalized - 1] == scene)
        result.insert("duplicateOf", normalized - 1);
    return result;
}

QJsonArray fixtureGroundTruth() {
    QJsonArray result;
    for (int i = 0; i < fixtureFrameCount(); ++i) result.append(fixtureGroundTruth(i));
    return result;
}

int showFixture(int intervalMs, QSize size, int durationMs, bool editing) {
    intervalMs = std::max(1, intervalMs);
    if (durationMs <= 0) durationMs = intervalMs * fixtureFrameCount();
    FixtureWindow window(size, editing);
    window.showFullScreen();
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        window.setFrame(static_cast<int>(elapsed.elapsed() / intervalMs));
    });
    timer.start(intervalMs);
    QTimer::singleShot(durationMs, &window, &QWidget::close);
    return QApplication::exec();
}

}  // namespace replay
