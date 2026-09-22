#include <QCryptographicHash>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <webp/decode.h>
#include <webp/encode.h>
#include <sys/resource.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Standalone, opt-in experiment. It neither links Replay nor opens an archive.
// Run each measurement under an external timeout/resource observer. Rendering,
// input loading, output writes, hashes and equality checks are outside timings.
namespace {
constexpr int width = 3840, height = 2160;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void emitJson(const QJsonObject &object) {
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << '\n';
}
double cpuMs() {
    rusage usage{};
    require(getrusage(RUSAGE_SELF, &usage) == 0, "Cannot read process CPU time");
    return usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0 +
           usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
}
QString version(int value) {
    return QString("%1.%2.%3").arg(value >> 16).arg((value >> 8) & 255).arg(value & 255);
}
QString hash(const QImage &image) {
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromRawData(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()),
        QCryptographicHash::Sha256).toHex());
}
bool equal(const QImage &a, const QImage &b) {
    return a.format() == QImage::Format_RGBA8888 && b.format() == a.format() &&
           a.size() == b.size() && a.bytesPerLine() == b.bytesPerLine() &&
           std::memcmp(a.constBits(), b.constBits(), a.sizeInBytes()) == 0;
}
quint32 nextRandom(quint32 &state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
QFont font(int pixels, bool mono = false) {
    QFont result(mono ? "DejaVu Sans Mono" : "DejaVu Sans");
    result.setPixelSize(pixels);
    return result;
}
void text(QPainter &p, int x, int y, const QString &value, int pixels = 18, bool mono = false) {
    p.setFont(font(pixels, mono)); p.drawText(x, y, value);
}
QImage dense(int scene) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(QColor("#13191f"));
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(QColor("#cbd5df"));
    text(p, 28, 31, "SYNTHETIC SCREEN / LOSSLESS WEBP EXPERIMENT", 17, true);
    text(p, width - 280, 31, QString("14:26:%1").arg(10 + scene * 5), 17, true);
    for (int column = 0; column < 3; ++column) {
        const int x = 16 + 1272 * column;
        const QColor panel = column == 2 ? QColor("#f3f5f7") : QColor("#1b242e");
        const QColor foreground = column == 2 ? QColor("#20303e") : QColor("#d4deea");
        p.fillRect(x, 48, 1256, 2096, panel);
        p.fillRect(x, 48, 1256, 44, column == 2 ? QColor("#d8e1ea") : QColor("#263441"));
        p.setPen(foreground);
        text(p, x + 20, 78, QStringList{"Terminal / fictional build output", "Editor / fictional source", "Browser / fictional project notes"}[column], 18);
        quint32 random = 0x51fd3b9a + column;
        const int pixels = 14 + column * 2;
        const int lineHeight = pixels + 10;
        const int rows = 1980 / lineHeight;
        const int scroll = scene == 2 ? 7 : 0;
        for (int skipped = 0; skipped < scroll; ++skipped) nextRandom(random);
        for (int row = 0; row < rows; ++row) {
            const int n = row + scroll;
            const auto id = QString::number(nextRandom(random), 16).rightJustified(8, '0');
            QString line;
            if (column == 0)
                line = QString("%1  build component_%2  checksum=%3  elapsed=%4ms  passed")
                    .arg(n + 1, 4, 10, QChar('0')).arg((n * 17) % 251).arg(id).arg(14 + n % 57);
            else if (column == 1)
                line = QString("%1  %2const result_%3 = await archive.lookup({ key: '%4', limit: %5 });")
                    .arg(n + 1, 3).arg(n % 3 ? "    " : "").arg(n % 29).arg(id).arg(5 + n % 13);
            else
                line = QString("%1. Review item %2: local planning notes and delivery reference %3.")
                    .arg(n + 1).arg(200 + n * 11).arg(id.left(6));
            if (row == 9 && scene == 1) line = "Updated reference: KITE-1043. Revised amount: 7,412.50. Small visible edit.";
            p.setPen(n % 7 == 0 ? (column == 2 ? QColor("#34657c") : QColor("#8bc2ac")) : foreground);
            text(p, x + 20, 122 + row * lineHeight, line, pixels, column != 2);
        }
        p.fillRect(x + 1248, 110, 3, 440, QColor("#657785"));
    }
    if (scene == 1) p.fillRect(57, 423, 9, 2, QColor("#eeeeee"));
    return image;
}
QImage mixed() {
    QImage image = dense(0);
    quint32 random = 0xb3a48d17;
    // A deterministic image-like panel: gradients, soft shapes, edges and mild
    // noise, without pretending to substitute for a real photographic corpus.
    for (int y = 160; y < height - 100; ++y) {
        auto *row = image.scanLine(y);
        for (int x = 1390; x < width - 80; ++x) {
            const double u = double(x - 1390) / 2370, v = double(y - 160) / 1900;
            const double wave = std::sin(u * 15 + std::cos(v * 11) * 2) * 24;
            const double mountain = 0.53 + std::sin(u * 6) * 0.1 + std::sin(u * 19) * 0.035;
            const int noise = int(nextRandom(random) % 15) - 7;
            const bool ground = v > mountain;
            row[x * 4] = uchar(std::clamp(int(ground ? 40 + 65 * u + wave : 69 + 95 * v) + noise, 0, 255));
            row[x * 4 + 1] = uchar(std::clamp(int(ground ? 84 + 76 * v + wave : 132 + 55 * v) + noise, 0, 255));
            row[x * 4 + 2] = uchar(std::clamp(int(ground ? 66 + 29 * u : 202 + 25 * v) + noise, 0, 255));
            row[x * 4 + 3] = 255;
        }
    }
    QPainter p(&image);
    p.fillRect(1430, 1870, 1180, 130, QColor("#152533"));
    p.setPen(QColor("#f3f6fa"));
    text(p, 1460, 1912, "Procedural visual panel / synthetic pixels only", 24);
    text(p, 1460, 1955, "Native 3840 x 2160 with small text outside the visual panel", 18);
    return image;
}
QImage entropy() {
    QImage image(width, height, QImage::Format_RGBA8888);
    quint32 random = 0xc05ddcaf;
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = nextRandom(random);
            row[x * 4] = pixel & 255; row[x * 4 + 1] = (pixel >> 8) & 255;
            row[x * 4 + 2] = (pixel >> 16) & 255; row[x * 4 + 3] = 255;
        }
    }
    return image;
}
QImage alphaProbe() {
    QImage image(257, 193, QImage::Format_RGBA8888);
    quint32 random = 0x1cc04732;
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = nextRandom(random);
            row[x * 4] = pixel & 255; row[x * 4 + 1] = (pixel >> 8) & 255;
            row[x * 4 + 2] = (pixel >> 16) & 255;
            row[x * 4 + 3] = x % 3 == 0 ? 0 : x % 3 == 1 ? 127 : 255;
        }
    }
    return image;
}
void writeNew(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Output already exists or is not writable");
    require(file.write(bytes) == bytes.size(), "Output write failed");
    require(file.flush(), "Output flush failed");
}
void generate(const QString &path) {
    require(!path.isEmpty() && !QFileInfo::exists(path), "Generation requires a new directory");
    require(QDir().mkpath(path), "Cannot create fixture directory");
    QJsonArray fixtures;
    for (int i = 0; i < 6; ++i) {
        const QString name = QStringList{"dense-base", "dense-edit", "dense-scroll", "mixed-visual", "entropy", "alpha-probe"}[i];
        const QImage image = i < 3 ? dense(i) : i == 3 ? mixed() : i == 4 ? entropy() : alphaProbe();
        const QString filename = name + ".png";
        const QString output = QDir(path).filePath(filename);
        require(image.save(output, "PNG"), "Cannot save fixture PNG");
        const QImage loaded = QImage(output).convertToFormat(QImage::Format_RGBA8888);
        require(equal(image, loaded), "PNG round trip changed fixture pixels");
        fixtures.append(QJsonObject{{"name", name}, {"file", filename}, {"width", image.width()},
                                   {"height", image.height()}, {"rgba_sha256", hash(image)},
                                   {"png_bytes", QFileInfo(output).size()}, {"png_exact", true}});
    }
    QJsonObject manifest{{"schema", 1}, {"synthetic", true}, {"qt_version", qVersion()},
        {"font_mono", QFontInfo(font(14, true)).family()}, {"font_ui", QFontInfo(font(18)).family()},
        {"fixtures", fixtures},
        {"determinism_note", "Seeded pixels and fixed text; text rasterization depends on the recorded Qt/fonts/runtime."}};
    writeNew(QDir(path).filePath("manifest.json"), QJsonDocument(manifest).toJson());
    emitJson(manifest);
}
QByteArray encode(const QImage &image, int method, float quality, const QString &hint = "default") {
    WebPConfig config;
    WebPPicture picture;
    WebPMemoryWriter writer;
    require(WebPConfigInit(&config) && WebPPictureInit(&picture), "WebP initialization failed");
    config.lossless = 1; config.quality = quality; config.method = method;
    config.exact = 1; config.near_lossless = 100; config.thread_level = 0;
    config.image_hint = hint == "graph" ? WEBP_HINT_GRAPH : WEBP_HINT_DEFAULT;
    require(WebPValidateConfig(&config), "Invalid WebP configuration");
    picture.use_argb = 1; picture.width = image.width(); picture.height = image.height();
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite; picture.custom_ptr = &writer;
    const bool success = WebPPictureImportRGBA(&picture, image.constBits(), image.bytesPerLine()) &&
                         WebPEncode(&config, &picture);
    QByteArray result;
    if (success) result = QByteArray(reinterpret_cast<const char *>(writer.mem), writer.size);
    WebPPictureFree(&picture); WebPMemoryWriterClear(&writer);
    require(success, "Lossless WebP encoding failed");
    return result;
}
QImage decode(const QByteArray &bytes) {
    WebPBitstreamFeatures features{};
    require(WebPGetFeatures(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(), &features) == VP8_STATUS_OK,
            "Cannot read WebP features");
    require(features.format == 2, "Output is not lossless WebP");
    require(features.width > 0 && features.height > 0 &&
            qint64(features.width) * features.height <= 32LL * 1024 * 1024, "WebP dimensions exceed experiment bounds");
    QImage image(features.width, features.height, QImage::Format_RGBA8888);
    require(!image.isNull(), "Decode allocation failed");
    require(WebPDecodeRGBAInto(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(),
                              image.bits(), image.sizeInBytes(), image.bytesPerLine()), "WebP decoding failed");
    return image;
}
struct Timing { double wall = 0, cpu = 0; };
template <typename Function> auto timed(Function function, Timing &timing) {
    const double startCpu = cpuMs();
    QElapsedTimer timer; timer.start();
    auto value = function();
    timing.wall = timer.nsecsElapsed() / 1e6;
    timing.cpu = cpuMs() - startCpu;
    return value;
}
void measure(const QString &input, const QString &output, const QString &mode, int method, float quality, int repeats, const QString &hint) {
    require(!input.isEmpty() && QFileInfo(input).isFile(), "Input must be an existing PNG");
    require(QFileInfo(input).size() <= 128LL * 1024 * 1024, "Input file exceeds 128 MiB");
    require(!output.isEmpty() && !QFileInfo::exists(output), "Output must be a new WebP path");
    QImageReader reader(input, "PNG");
    reader.setAutoTransform(false);
    const QSize dimensions = reader.size();
    require(dimensions.width() > 0 && dimensions.height() > 0 &&
            qint64(dimensions.width()) * dimensions.height() <= 32LL * 1024 * 1024,
            "Input dimensions exceed experiment bounds");
    const QImage original = reader.read().convertToFormat(QImage::Format_RGBA8888);
    require(!original.isNull() && original.size() == dimensions, "Cannot load input PNG");
    const QString inputHash = hash(original);
    QByteArray baseline;
    if (mode == "reencode") {
        baseline = encode(original, 0, 0);
        require(equal(original, decode(baseline)), "Current-format baseline changed pixels");
    }
    QJsonArray iterations;
    QByteArray result;
    for (int i = 0; i < repeats; ++i) {
        Timing decodeSource, encodeCandidate, decodeOutput;
        const QImage source = mode == "reencode" ? timed([&] { return decode(baseline); }, decodeSource) : original;
        require(equal(original, source), "Re-encode input changed pixels");
        result = timed([&] { return encode(source, method, quality, hint); }, encodeCandidate);
        const QImage decoded = timed([&] { return decode(result); }, decodeOutput);
        const bool exact = equal(original, decoded);
        const QString decodedHash = hash(decoded);
        iterations.append(QJsonObject{{"iteration", i + 1}, {"bytes", result.size()},
            {"source_decode_wall_ms", decodeSource.wall}, {"source_decode_cpu_ms", decodeSource.cpu},
            {"encode_wall_ms", encodeCandidate.wall}, {"encode_cpu_ms", encodeCandidate.cpu},
            {"output_decode_wall_ms", decodeOutput.wall}, {"output_decode_cpu_ms", decodeOutput.cpu},
            {"work_wall_ms", decodeSource.wall + encodeCandidate.wall},
            {"work_cpu_ms", decodeSource.cpu + encodeCandidate.cpu},
            {"rgba_sha256", decodedHash}, {"exact", exact}});
        require(exact && inputHash == decodedHash, "Candidate changed RGBA bytes");
    }
    writeNew(output, result);
    rusage usage{};
    require(getrusage(RUSAGE_SELF, &usage) == 0, "Cannot read peak RSS");
    emitJson({{"schema", 1}, {"mode", mode}, {"process_peak_rss_kib", qint64(usage.ru_maxrss)}, {"width", original.width()}, {"height", original.height()},
        {"input_rgba_sha256", inputHash}, {"baseline_bytes", mode == "reencode" ? QJsonValue(baseline.size()) : QJsonValue()},
        {"encoder_version", version(WebPGetEncoderVersion())}, {"decoder_version", version(WebPGetDecoderVersion())},
        {"config", QJsonObject{{"method", method}, {"quality", quality}, {"lossless", 1}, {"exact", 1},
                               {"near_lossless", 100}, {"thread_level", 0}, {"hint", hint}}},
        {"iterations", iterations}, {"all_exact", true}, {"output_bytes", result.size()},
        {"timing_note", "Encode includes RGBA import, output copy and encoder cleanup. Decode includes allocation. File I/O, fixture generation/loading, baseline preparation, hashing and equality checks are excluded. Output decode is verification/read cost and is excluded from work times."}});
}
} // namespace

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Synthetic lossless WebP experiment; no Replay service or archive access. Enforce a process timeout externally.");
    parser.addHelpOption();
    parser.addOptions({{"generate", "Generate corpus into a fresh directory", "directory"},
        {"input", "Input PNG", "file"}, {"output", "New output WebP path", "file"},
        {"mode", "direct or reencode (current method=0, quality=0 baseline)", "mode", "direct"},
        {"method", "WebP method, 0 through 6", "integer", "0"},
        {"quality", "Lossless effort, 0 through 100; never pixel quality", "number", "0"},
        {"hint", "Image hint: default or graph", "hint", "default"},
        {"repeats", "Iterations, 1 through 10", "integer", "1"}});
    parser.process(app);
    try {
        require(parser.positionalArguments().isEmpty(), "Positional arguments are not supported");
        if (parser.isSet("generate")) {
            for (const char *option : {"input", "output", "mode", "method", "quality", "repeats", "hint"})
                require(!parser.isSet(option), "Generation cannot be combined with measurement options");
            generate(parser.value("generate"));
        } else {
            bool methodOk = false, qualityOk = false, repeatsOk = false;
            const int method = parser.value("method").toInt(&methodOk);
            const float quality = parser.value("quality").toFloat(&qualityOk);
            const int repeats = parser.value("repeats").toInt(&repeatsOk);
            const QString mode = parser.value("mode");
            require(methodOk && method >= 0 && method <= 6, "Method must be 0 through 6");
            require(qualityOk && std::isfinite(quality) && quality >= 0 && quality <= 100, "Quality must be 0 through 100");
            require(repeatsOk && repeats >= 1 && repeats <= 10, "Repeats must be 1 through 10");
            require(mode == "direct" || mode == "reencode", "Mode must be direct or reencode");
            const QString hint = parser.value("hint");
            require(hint == "default" || hint == "graph", "Hint must be default or graph");
            measure(parser.value("input"), parser.value("output"), mode, method, quality, repeats, hint);
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
