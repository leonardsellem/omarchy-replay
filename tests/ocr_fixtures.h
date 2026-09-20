#pragma once

#include <QFont>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <array>

// Fixed visible-text truth, independent of the OCR implementation. These images
// contain no applications or personal data. Render one at a time to bound RAM.
namespace ocr_fixture {

constexpr int count = 12;

inline QString name(int index) {
    static const std::array<const char *, count> names{
        "initial", "invoice-digit", "small-identifier", "deleted-line",
        "added-line", "notification-added", "notification-removed", "scroll-start",
        "scroll-moved", "full-scene", "dimension-change", "blank"};
    return QString::fromLatin1(names.at(index));
}

inline QStringList visibleTerms(int index) {
    if (index < 7) {
        QStringList terms{"Patrick", index == 0 ? "XYZ-1042" : "XYZ-1043",
                          index < 2 ? "EDGE-7F3" : "EDGE-7F8"};
        if (index < 3) terms << "VANISHED";
        if (index >= 4) terms << "INSERTED";
        if (index == 5) terms << "NOTICE-732";
        return terms;
    }
    if (index == 7) return {"ALDER", "BIRCH", "CEDAR", "MAPLE", "PINE"};
    if (index == 8) return {"CEDAR", "MAPLE", "PINE", "WILLOW", "ASPEN"};
    if (index < 11) {
        QStringList terms{"Omakase", "JUNIPER", "RESET-930"};
        if (index == 10) terms << "RESIZED";
        return terms;
    }
    return {};
}

inline QSize frameSize(int index, QSize size) {
    return index >= 10 ? size - QSize(160, 90) : size;
}

inline void text(QPainter &painter, int x, int y, const QString &value,
                 int pixels = 28, bool monospace = false) {
    QFont font(monospace ? "DejaVu Sans Mono" : "DejaVu Sans");
    font.setPixelSize(pixels);
    painter.setFont(font);
    painter.drawText(x, y, value);
}

inline QImage frame(int index, QSize size) {
    size = frameSize(index, size);
    QImage image(size, QImage::Format_RGBA8888);
    const bool dark = index == 9 || index == 10;
    image.fill(dark ? QColor("#141b24") : Qt::white);
    if (index == 11) return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(size.width() / 1920.0, size.height() / 1080.0);
    painter.setPen(dark ? QColor("#f2f4f8") : QColor("#161c23"));
    text(painter, 72, 86, "SYNTHETIC OCR CORRECTNESS FIXTURE", 24);

    if (index < 7) {
        text(painter, 96, 190, "Patrick / fictional workshop invoice", 34);
        text(painter, 96, 275, index == 0 ? "Invoice XYZ-1042" : "Invoice XYZ-1043", 30);
        text(painter, 96, 350, "Total EUR 1240.00 / delivery Friday", 28);
        text(painter, 96, 425,
             index < 2 ? "16 px reference EDGE-7F3 / retry pending"
                       : "16 px reference EDGE-7F8 / retry pending", 16, true);
        if (index < 3) text(painter, 96, 510, "Remove this line: VANISHED", 28);
        text(painter, 96, 595, "Keep this unchanged paragraph available in search.", 28);
        if (index >= 4) text(painter, 96, 680, "New delivery note: INSERTED", 28);
        if (index == 5) {
            painter.fillRect(QRect(1230, 835, 600, 165), QColor("#edf2f7"));
            text(painter, 1270, 895, "NOTICE-732", 28);
            text(painter, 1270, 950, "The sample is ready.", 25);
        }
    } else if (index == 7 || index == 8) {
        text(painter, 96, 190, "Fictional delivery notes", 34);
        painter.save();
        painter.setClipRect(QRect(80, 240, 1760, 420));
        const QStringList rows{"ALDER", "BIRCH", "CEDAR", "MAPLE", "PINE", "WILLOW", "ASPEN", "ELM"};
        const int offset = index == 7 ? 0 : 2;
        for (int row = 0; row < rows.size(); ++row)
            text(painter, 96, 300 + (row - offset) * 80,
                 "Delivery reference " + rows[row] + " / sample packing list", 28);
        painter.restore();
    } else {
        text(painter, 96, 190, "Terminal / synthetic content only", 34);
        text(painter, 96, 285, "Omakase", 32, true);
        text(painter, 96, 370, "Project JUNIPER / completed", 28, true);
        text(painter, 96, 455, "Reference RESET-930", 28, true);
        if (index == 10) text(painter, 96, 540, "RESIZED display", 28, true);
    }
    return image;
}

} // namespace ocr_fixture
