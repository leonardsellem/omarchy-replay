#include "replay_config.h"
#include "viewer.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMutex>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
class IsolatedEnvironment {
public:
    explicit IsolatedEnvironment(const QString& root) {
        for (const auto* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            set(name, QDir(root).filePath(name).toUtf8());
        set("PATH", QDir(root).filePath("bin").toUtf8());
    }
    ~IsolatedEnvironment() {
        for (auto value = saved_.cbegin(); value != saved_.cend(); ++value)
            if (value->first) qputenv(value.key().constData(), value->second); else qunsetenv(value.key().constData());
    }
private:
    void set(const QByteArray& name, const QByteArray& value) {
        saved_.insert(name, {qEnvironmentVariableIsSet(name.constData()), qgetenv(name.constData())});
        qputenv(name.constData(), value);
    }
    QMap<QByteArray, QPair<bool, QByteArray>> saved_;
};

bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

bool fakeRecorder(const QString& root) {
    const QString bin = QDir(root).filePath("bin");
    if (!QDir().mkpath(bin)) return false;
    const QString executable = QDir(bin).filePath("omarchy-meeting-recorder");
    // Detection must only inspect this file, never execute a recorder.
    return writeFile(executable, "#!/bin/sh\nexit 97\n") &&
        QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

struct FakeRecording {
    QMutex mutex;
    QStringList actions;
    QJsonObject status{{"running", true}, {"intent", "stopped"}, {"state", "stopped"},
                       {"indexing", false}, {"indexing_paused", true}};
    replay::ViewerServiceHooks hooks() {
        replay::ViewerServiceHooks result;
        result.recordingStatus = [this] { QMutexLocker guard(&mutex); return status; };
        result.recordingControl = [this](const QString& action, const QJsonObject&) {
            QMutexLocker guard(&mutex);
            actions.append(action);
            return status;
        };
        result.displays = [] { return QJsonArray{QJsonObject{{"name", "SYNTHETIC-1"},
            {"model", "Synthetic display"}, {"width", 1920}, {"height", 1080}}}; };
        return result;
    }
    QStringList calls() { QMutexLocker guard(&mutex); return actions; }
};

int meetingTab(QTabWidget* tabs) {
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->widget(i)->objectName() == "meetingSettingsScroll") return i;
    return -1;
}

void inspectSettings(QWidget* viewer, const std::function<void(QDialog*)>& inspect) {
    bool inspected = false, timedOut = false;
    QTimer watchdog;
    watchdog.setInterval(5000);
    QObject::connect(&watchdog, &QTimer::timeout, viewer, [&] {
        timedOut = true;
        if (auto* dialog = viewer->findChild<QDialog*>("replaySettings")) dialog->reject();
    });
    watchdog.start();
    QTimer::singleShot(0, viewer, [&] {
        auto* dialog = viewer->findChild<QDialog*>("replaySettings");
        QVERIFY(dialog);
        const auto close = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
        inspected = true;
        inspect(dialog);
    });
    auto* button = viewer->findChild<QPushButton*>("openReplaySettings");
    QVERIFY(button);
    button->click();
    watchdog.stop();
    QVERIFY(inspected);
    QVERIFY(!timedOut);
}

replay::ReplayConfigDocument initializeConfig() {
    auto document = replay::loadReplayConfig();
    document.config.output = "SYNTHETIC-1";
    replay::saveReplayConfig(document.config, document.original);
    return replay::loadReplayConfig();
}
} // namespace

class MeetingSettingsTest : public QObject {
    Q_OBJECT
private slots:
    void absentRecorderDoesNotOfferFreshIntegration() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        IsolatedEnvironment environment(temporary.path());
        QVERIFY(!replay::meetingRecorderAvailable());
        const auto document = initializeConfig();
        FakeRecording service;
        auto viewer = replay::createViewer(replay::replayHistoryDirectory(document.config), service.hooks());
        viewer->show();
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        inspectSettings(viewer.get(), [&](QDialog* dialog) {
            auto* tabs = dialog->findChild<QTabWidget*>("settingsTabs");
            QVERIFY(tabs);
            const int tab = meetingTab(tabs);
            QVERIFY(tab >= 0);
            QVERIFY(!tabs->isTabVisible(tab));
            auto* enabled = dialog->findChild<QCheckBox*>("settingMeetingsEnabled");
            QVERIFY(enabled);
            QVERIFY(!enabled->isChecked());
            QVERIFY(!enabled->isEnabled());
        });
        QCOMPARE(replay::loadReplayConfig().original, document.original);
        QVERIFY(service.calls().isEmpty());
        viewer->close();
    }

    void detectedRecorderIsOptionalAndSavingOnlyReloads() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        IsolatedEnvironment environment(temporary.path());
        QVERIFY(fakeRecorder(temporary.path()));
        QVERIFY(replay::meetingRecorderAvailable());
        const auto document = initializeConfig();
        const QString meetings = temporary.filePath("Synthetic meetings");
        QVERIFY(QDir().mkpath(meetings));
        FakeRecording service;
        auto viewer = replay::createViewer(replay::replayHistoryDirectory(document.config), service.hooks());
        viewer->show();
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        inspectSettings(viewer.get(), [&](QDialog* dialog) {
            auto* tabs = dialog->findChild<QTabWidget*>("settingsTabs");
            const int tab = meetingTab(tabs);
            QVERIFY(tab >= 0);
            QVERIFY(tabs->isTabVisible(tab));
            tabs->setCurrentIndex(tab);
            auto* enabled = dialog->findChild<QCheckBox*>("settingMeetingsEnabled");
            QVERIFY(!enabled->isChecked());
            QVERIFY(enabled->isEnabled());
            QCOMPARE(dialog->findChild<QLabel*>("meetingIntegrationStatus")->text(), "Meeting Recorder detected.");
            auto* directory = dialog->findChild<QLineEdit*>("settingMeetingsDirectory");
            directory->setText(meetings);
            enabled->setFocus();
            QTest::keyClick(enabled, Qt::Key_Space);
            QVERIFY(enabled->isChecked());
            const QString proofDirectory = qEnvironmentVariable("REPLAY_MEETING_PROOF_DIR");
            if (!proofDirectory.isEmpty()) {
                QVERIFY(QDir().mkpath(proofDirectory));
                dialog->resize(900, 650);
                QCoreApplication::processEvents();
                QVERIFY(dialog->grab().save(QDir(proofDirectory).filePath("settings.png")));
            }
            dialog->findChild<QDialogButtonBox*>("settingsButtons")->button(QDialogButtonBox::Save)->click();
            QTRY_VERIFY(!dialog->isVisible());
        });
        QTRY_COMPARE(service.calls(), QStringList{"reload"});
        const auto saved = replay::loadReplayConfig();
        QVERIFY(saved.config.meetingsEnabled);
        QCOMPARE(saved.config.meetingsDirectory, meetings);
        QVERIFY(!saved.config.loginStartup);
        QCOMPARE(service.status.value("intent").toString(), "stopped");
        QVERIFY(service.status.value("indexing_paused").toBool());
        QVERIFY(!QFile::exists(QDir(replay::replayHistoryDirectory(saved.config)).filePath("index.sqlite")));
        inspectSettings(viewer.get(), [&](QDialog* dialog) {
            QVERIFY(dialog->findChild<QCheckBox*>("settingMeetingsEnabled")->isChecked());
            QCOMPARE(dialog->findChild<QLineEdit*>("settingMeetingsDirectory")->text(), meetings);
        });
        QCOMPARE(service.calls(), QStringList{"reload"});
        viewer->close();
    }

    void removedRecorderStillAllowsDisablingIntegration() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        IsolatedEnvironment environment(temporary.path());
        QVERIFY(fakeRecorder(temporary.path()));
        auto document = initializeConfig();
        document.config.meetingsEnabled = true;
        document.config.meetingsDirectory = temporary.filePath("Synthetic meetings");
        replay::saveReplayConfig(document.config, document.original);
        QVERIFY(QFile::remove(temporary.filePath("bin/omarchy-meeting-recorder")));
        QVERIFY(!replay::meetingRecorderAvailable());
        FakeRecording service;
        auto viewer = replay::createViewer(replay::replayHistoryDirectory(document.config), service.hooks());
        viewer->show();
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        inspectSettings(viewer.get(), [&](QDialog* dialog) {
            auto* tabs = dialog->findChild<QTabWidget*>("settingsTabs");
            const int tab = meetingTab(tabs);
            QVERIFY(tab >= 0);
            QVERIFY(tabs->isTabVisible(tab));
            tabs->setCurrentIndex(tab);
            auto* enabled = dialog->findChild<QCheckBox*>("settingMeetingsEnabled");
            QVERIFY(enabled->isChecked());
            QVERIFY(enabled->isEnabled());
            QVERIFY(dialog->findChild<QLabel*>("meetingIntegrationStatus")->text().contains("unavailable"));
            enabled->setChecked(false);
            dialog->findChild<QDialogButtonBox*>("settingsButtons")->button(QDialogButtonBox::Save)->click();
            QTRY_VERIFY(!dialog->isVisible());
        });
        QTRY_COMPARE(service.calls(), QStringList{"reload"});
        QVERIFY(!replay::loadReplayConfig().config.meetingsEnabled);
        QCOMPARE(service.status.value("intent").toString(), "stopped");
        viewer->close();
    }

    void concurrentConfigEditIsPreservedAndDoesNotReload() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        IsolatedEnvironment environment(temporary.path());
        QVERIFY(fakeRecorder(temporary.path()));
        const auto document = initializeConfig();
        FakeRecording service;
        auto viewer = replay::createViewer(replay::replayHistoryDirectory(document.config), service.hooks());
        viewer->show();
        QTRY_VERIFY(!viewer->property("historyLoading").toBool());
        const QByteArray concurrent = document.original + "\n# Synthetic concurrent edit\n";
        inspectSettings(viewer.get(), [&](QDialog* dialog) {
            dialog->findChild<QCheckBox*>("settingMeetingsEnabled")->setChecked(true);
            QVERIFY(writeFile(replay::replayPaths().configFile, concurrent));
            auto* save = dialog->findChild<QDialogButtonBox*>("settingsButtons")->button(QDialogButtonBox::Save);
            save->click();
            auto* error = dialog->findChild<QLabel*>("settingsError");
            QTRY_VERIFY(error->text().contains("settings changed on disk"));
            QVERIFY(dialog->isVisible());
            QVERIFY(save->isEnabled());
        });
        QCOMPARE(replay::loadReplayConfig().original, concurrent);
        QVERIFY(!replay::loadReplayConfig().config.meetingsEnabled);
        QVERIFY(service.calls().isEmpty());
        viewer->close();
    }
};

QTEST_MAIN(MeetingSettingsTest)
#include "meeting_settings_test.moc"
