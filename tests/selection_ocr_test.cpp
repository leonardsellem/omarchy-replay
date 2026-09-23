#include "selection_ocr.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <thread>
#include <unistd.h>

namespace {
class SavedEnvironment {
public:
    explicit SavedEnvironment(const char *name) : name_(name), set_(qEnvironmentVariableIsSet(name)), value_(qgetenv(name)) {}
    ~SavedEnvironment() { if (set_) qputenv(name_, value_); else qunsetenv(name_); }
private:
    const char *name_;
    bool set_;
    QByteArray value_;
};

class FakeTesseract {
public:
    QTemporaryDir directory;
    SavedEnvironment path{"PATH"}, pidEnvironment{"REPLAY_SELECTION_TEST_PID"}, argsEnvironment{"REPLAY_SELECTION_TEST_ARGS"};
    explicit FakeTesseract(const QByteArray &body) {
        QFile script(directory.filePath("tesseract"));
        if (!directory.isValid() || !script.open(QIODevice::WriteOnly)) qFatal("Cannot create synthetic OCR executable");
        const QByteArray contents = "#!/bin/sh\nprintf '%s' \"$$\" > \"$REPLAY_SELECTION_TEST_PID\"\n" + body;
        if (script.write(contents) != contents.size() || !script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
            qFatal("Cannot write synthetic OCR executable");
        script.close();
        qputenv("PATH", directory.path().toUtf8());
        qputenv("REPLAY_SELECTION_TEST_PID", directory.filePath("pid").toUtf8());
        qputenv("REPLAY_SELECTION_TEST_ARGS", directory.filePath("args").toUtf8());
    }
    qint64 pid() const {
        QFile file(directory.filePath("pid"));
        return file.open(QIODevice::ReadOnly) ? file.readAll().toLongLong() : 0;
    }
};

QImage blank() {
    QImage image(480, 120, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    return image;
}
}

class SelectionOcrTest final : public QObject {
    Q_OBJECT
    SavedEnvironment originalLanguage_{"OMARCHY_OCR_LANGS"};
private slots:
    void init() { qputenv("OMARCHY_OCR_LANGS", "eng"); }

    void nativeCropContainsSelectedTextOnly() {
        SavedEnvironment language{"OMARCHY_OCR_LANGS"}; qputenv("OMARCHY_OCR_LANGS", "eng");
        QImage screen(1000, 320, QImage::Format_RGBA8888); screen.fill(Qt::white);
        {
            QPainter painter(&screen); painter.setPen(Qt::black);
            QFont font("DejaVu Sans"); font.setPixelSize(36); painter.setFont(font);
            painter.drawText(32, 55, "OUTSIDE ABOVE");
            painter.drawText(32, 170, "SELECTED TEXT 4821");
            painter.drawText(720, 170, "OUTSIDE");
            painter.drawText(32, 285, "OUTSIDE BELOW");
        }
        const auto result = replay::recognizeSelection(screen.copy(QRect(16, 110, 610, 90)), {});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.cancelled);
        QCOMPARE(result.text, "SELECTED TEXT 4821");
    }

    void nativeBlankProducesNoText() {
        SavedEnvironment language{"OMARCHY_OCR_LANGS"}; qputenv("OMARCHY_OCR_LANGS", "eng");
        const auto result = replay::recognizeSelection(blank(), {});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.cancelled); QVERIFY(result.text.isEmpty());
    }

    void invalidImageAndLanguageFailWithoutStartingWorker() {
        FakeTesseract fake("exit 0\n");
        QVERIFY(!replay::recognizeSelection({}, {}).error.isEmpty());
        QImage oversized(16385, 1, QImage::Format_RGBA8888);
        QVERIFY(!replay::recognizeSelection(oversized, {}).error.isEmpty());
        SavedEnvironment language{"OMARCHY_OCR_LANGS"}; qputenv("OMARCHY_OCR_LANGS", "eng --invalid");
        QVERIFY(!replay::recognizeSelection(blank(), {}).error.isEmpty());
        QCOMPARE(fake.pid(), qint64(0));
        auto cancel = std::make_shared<std::atomic_bool>(true);
        const auto cancelled = replay::recognizeSelection({}, cancel);
        QVERIFY(cancelled.cancelled); QVERIFY(cancelled.text.isEmpty()); QVERIFY(cancelled.error.isEmpty());
        QCOMPARE(fake.pid(), qint64(0));
    }

    void workerUsesRequestedLanguagesAndOneThread() {
        FakeTesseract fake("printf '%s\\n' \"$@\" > \"$REPLAY_SELECTION_TEST_ARGS\"\n"
            "printf '%s\\n' \"$OMP_THREAD_LIMIT\" \"$OMP_NUM_THREADS\" >> \"$REPLAY_SELECTION_TEST_ARGS\"\n"
            "/bin/cat >/dev/null\nprintf 'selection only\\n'\n");
        SavedEnvironment language{"OMARCHY_OCR_LANGS"}; qputenv("OMARCHY_OCR_LANGS", "eng+deu");
        const auto result = replay::recognizeSelection(blank(), {});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.text, "selection only");
        QFile args(fake.directory.filePath("args")); QVERIFY(args.open(QIODevice::ReadOnly));
        QCOMPARE(args.readAll(), QByteArray("stdin\nstdout\n--psm\n6\n--oem\n1\n--dpi\n300\n-l\neng+deu\n-c\npreserve_interword_spaces=1\n1\n1\n"));
        QVERIFY(fake.pid() > 0);
        QCOMPARE(kill(pid_t(fake.pid()), 0), -1); QCOMPARE(errno, ESRCH);
    }

    void cancellationDiscardsPartialTextAndReapsWorker() {
        FakeTesseract fake("printf 'partial text must not escape'\nexec /bin/sleep 30\n");
        auto cancel = std::make_shared<std::atomic_bool>(false);
        std::thread cancellation([&] {
            for (int i = 0; i < 100 && fake.pid() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            cancel->store(true);
        });
        QElapsedTimer elapsed; elapsed.start();
        const auto result = replay::recognizeSelection(blank(), cancel);
        cancellation.join();
        QVERIFY(result.cancelled); QVERIFY(result.text.isEmpty()); QVERIFY(result.error.isEmpty());
        QVERIFY(elapsed.elapsed() < 2000);
        QVERIFY(fake.pid() > 0);
        QCOMPARE(kill(pid_t(fake.pid()), 0), -1); QCOMPARE(errno, ESRCH);
    }

    void failedWorkerDoesNotReturnPartialOutput() {
        FakeTesseract fake("printf 'do not copy partial text'\nprintf 'synthetic error' >&2\nexit 1\n");
        const auto result = replay::recognizeSelection(blank(), {});
        QVERIFY(result.text.isEmpty()); QVERIFY(!result.error.isEmpty()); QVERIFY(!result.cancelled);
    }

    void excessiveOutputIsRejectedAndWorkerReaped() {
        FakeTesseract fake("exec /usr/bin/head -c 1048577 /dev/zero\n");
        const auto result = replay::recognizeSelection(blank(), {});
        QVERIFY(result.text.isEmpty()); QVERIFY(result.error.contains("too much data")); QVERIFY(!result.cancelled);
        QVERIFY(fake.pid() > 0);
        QCOMPARE(kill(pid_t(fake.pid()), 0), -1); QCOMPARE(errno, ESRCH);
    }

    void missingExecutableIsReported() {
        SavedEnvironment path{"PATH"}; QTemporaryDir emptyPath;
        qputenv("PATH", emptyPath.path().toUtf8());
        const auto result = replay::recognizeSelection(blank(), {});
        QVERIFY(result.text.isEmpty()); QVERIFY(result.error.contains("Could not start")); QVERIFY(!result.cancelled);
    }

    void deadlineStopsAndReapsUnresponsiveWorker() {
        FakeTesseract fake("exec /bin/sleep 30\n");
        QElapsedTimer elapsed; elapsed.start();
        const auto result = replay::recognizeSelection(blank(), {});
        QVERIFY(result.text.isEmpty()); QVERIFY(result.error.contains("too long")); QVERIFY(!result.cancelled);
        QVERIFY(elapsed.elapsed() >= 9500 && elapsed.elapsed() < 12000);
        QVERIFY(fake.pid() > 0);
        QCOMPARE(kill(pid_t(fake.pid()), 0), -1); QCOMPARE(errno, ESRCH);
    }
};

QTEST_MAIN(SelectionOcrTest)
#include "selection_ocr_test.moc"
