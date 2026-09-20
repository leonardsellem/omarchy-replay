#include "ocr_fixtures.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

// Export the existing correctness fixtures and a fixed physical-font-size
// layout. All engines receive the same lossless pixels and independent truth.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2 || QFile::exists(argv[1])) return 2;
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    if (!QDir().mkpath(directory.path())) return 2;
    QJsonArray jobs;
    auto save = [&](const QImage& image, const QString& suite, int index, const QStringList& terms) {
        const QString filename = QString("%1-%2.png").arg(suite).arg(index, 2, 10, QChar('0'));
        if (!image.save(directory.filePath(filename))) return false;
        jobs.append(QJsonObject{{"path", filename}, {"suite", suite}, {"source_index", index},
            {"width", image.width()}, {"height", image.height()}, {"expected", QJsonArray::fromStringList(terms)}});
        return true;
    };
    for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160)}) {
        const QString suite = size.height() == 1080 ? "synthetic1080" : "synthetic4k";
        for (int i = 0; i < ocr_fixture::count; ++i)
            if (!save(ocr_fixture::frame(i, size), suite, i, ocr_fixture::visibleTerms(i))) return 3;
    }
    for (int scene = 0; scene < 3; ++scene) {
        QImage image(QSize(3840, 2160), QImage::Format_RGBA8888);
        image.fill(QColor("#e8edf2"));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QStringList truth{scene == 0 ? "KITE-1042" : "KITE-1043",
            scene < 2 ? "MESA-731" : "MESA-739", scene < 2 ? "POND-628" : "POND-629",
            "BOLT-482", "FERN-953", "LAKE-276"};
        const QStringList prose{"delivery checklist review the quantities and attach the receipt",
            "workspace draft notes saved locally for the afternoon review",
            "sample build completed the next step is to inspect the result",
            "shipping schedule updated after the warehouse confirmation",
            "customer reference details remain available in the local record",
            "change request reviewed and prepared for the next release"};
        for (int column = 0; column < 3; ++column) {
            const int x = 32 + column * 1264, pixels = std::array{14, 18, 24}[column], lineHeight = pixels + 12;
            const int rowCount = (2010 - 110) / lineHeight;
            const bool dark = column == 1;
            painter.fillRect(QRect(x, 32, 1232, 2096), dark ? QColor("#17202a") : Qt::white);
            painter.setPen(dark ? QColor("#eef2f5") : QColor("#17202a"));
            ocr_fixture::text(painter, x + 24, 72, "SYNTHETIC LOCAL OCR EXPERIMENT", 24);
            for (int row = 0; row < rowCount; ++row) {
                const int documentRow = row + (scene == 2 ? 3 : 0);
                QString line = QString("%1  %2").arg(documentRow + 1, 3, 10, QChar('0')).arg(prose[(documentRow + column) % prose.size()]);
                if (row == 5) line = "Invoice reference " + truth[column];
                if (row == rowCount - 5) line = "Dispatch reference " + truth[column + 3];
                ocr_fixture::text(painter, x + 24, 110 + row * lineHeight, line, pixels, true);
            }
        }
        painter.end();
        if (!save(image, "dense4k", scene, truth)) return 3;
    }
    QFile manifest(directory.filePath("manifest.json"));
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(QJsonDocument(jobs).toJson()) < 0) return 4;
    QTextStream(stdout) << "Exported " << jobs.size() << " synthetic images\n";
}
