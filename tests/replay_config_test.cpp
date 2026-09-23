#include "replay_config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

namespace {
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Cannot write config fixture");
}
QByteArray contents(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class Environment {
public:
    void set(const char* key, const QByteArray& value) {
        if (!saved_.contains(key)) saved_.insert(key, {qEnvironmentVariableIsSet(key), qgetenv(key)});
        qputenv(key, value);
    }
    ~Environment() {
        for (auto item = saved_.cbegin(); item != saved_.cend(); ++item)
            if (item.value().first) qputenv(item.key().constData(), item.value().second); else qunsetenv(item.key().constData());
    }
private:
    QMap<QByteArray, QPair<bool, QByteArray>> saved_;
};
}

class ReplayConfigTest final : public QObject {
    Q_OBJECT
private slots:
    void xdgDefaultsAreReadOnly() {
        QTemporaryDir directory;
        Environment environment;
        for (const auto* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            environment.set(name, directory.filePath(name).toUtf8());
        const auto paths = replay::replayPaths();
        QCOMPARE(paths.configFile, directory.filePath("XDG_CONFIG_HOME/omarchy-replay/config.toml"));
        QCOMPARE(paths.historyDirectory, directory.filePath("XDG_DATA_HOME/omarchy-replay/history"));
        QCOMPARE(paths.stateDirectory, directory.filePath("XDG_STATE_HOME/omarchy-replay"));
        QCOMPARE(paths.cacheDirectory, directory.filePath("XDG_CACHE_HOME/omarchy-replay"));
        QCOMPARE(paths.runtimeDirectory, directory.filePath("XDG_RUNTIME_DIR/omarchy-replay"));
        const auto document = replay::loadReplayConfig();
        QVERIFY(!document.exists); QVERIFY(document.original.isEmpty());
        QCOMPARE(document.config.retentionDays, 30); QCOMPARE(document.config.cpuCeilingPercent, 60.);
        QVERIFY(document.config.output.isEmpty()); QVERIFY(!document.config.loginStartup);
        QCOMPARE(document.config.excludedApps, replay::defaultAppExclusions());
        QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
        environment.set("XDG_CONFIG_HOME", "relative-is-not-xdg");
        QCOMPARE(replay::replayPaths().configFile, QDir::homePath() + "/.config/omarchy-replay/config.toml");
    }

    void exclusionDefaultsRespectExplicitAppLists() {
        QTemporaryDir directory;
        const QString path = directory.filePath("config.toml");
        for (const auto &source : {QByteArray(), QByteArray("[exclusions]\n")}) {
            write(path, source);
            const auto config = replay::loadReplayConfig(path).config;
            for (const auto &app : replay::privacyAppExclusions())
                QVERIFY2(config.excludedApps.contains(app), qPrintable(app));
            QVERIFY(config.excludedApps.contains("steam"));
            QVERIFY(config.excludedApps.contains("Steam"));
            auto optionalApps = replay::gamingAppExclusions() + replay::mediaAppExclusions();
            optionalApps.removeAll("steam"); optionalApps.removeAll("Steam");
            for (const auto &app : optionalApps)
                QVERIFY2(!config.excludedApps.contains(app), qPrintable(app));
        }
        for (const auto &apps : {QByteArray("[]"), QByteArray("['fixture.editor']")}) {
            write(path, "[exclusions]\napps=" + apps + "\n");
            const auto document = replay::loadReplayConfig(path);
            const QStringList expected = apps == "[]" ? QStringList() : QStringList{"fixture.editor"};
            QCOMPARE(document.config.excludedApps, expected);
            replay::saveReplayConfig(document.config, document.original, path);
            QCOMPARE(replay::loadReplayConfig(path).config.excludedApps, expected);
        }
    }

    void realTomlAndUnknownValuesSurviveSave() {
        QTemporaryDir directory;
        const QString path = directory.filePath("config.toml");
        write(path, R"TOML(
version_from_future = 9
[recording]
output = 'DP-3'
output_identity = 'synthetic serial'
interval_seconds = 2.5
future_option = { nested = [1, 2, 3] }
[storage]
retention_days = 45
max_disk_mib = 4096
min_free_mib = 256
[exclusions]
apps = ['org.example.Passwords', 'name#literal']
[[exclusions.windows]]
app_id = 'org.example.Browser'
title_regex = 'Private\s+window'
address = '0xABC12'
compositor_instance = 'synthetic-desktop-instance'
scope = 'output'
future_rule_value = true
[agent]
preferred = 'synthetic-agent'
)TOML");
        auto document = replay::loadReplayConfig(path);
        QVERIFY(document.exists);
        QCOMPARE(document.config.output, "DP-3"); QCOMPARE(document.config.intervalSeconds, 2.5);
        QCOMPARE(document.config.outputIdentity, "synthetic serial");
        QCOMPARE(document.config.excludedApps[1], "name#literal");
        QCOMPARE(document.config.excludedWindows[0].titleRegex, "Private\\s+window");
        QCOMPARE(document.config.excludedWindows[0].address, "0xabc12");
        document.config.intervalSeconds = 7.5;
        replay::saveReplayConfig(document.config, document.original, path);
        const auto reread = replay::loadReplayConfig(path);
        QCOMPARE(reread.config.intervalSeconds, 7.5); QCOMPARE(reread.config.retentionDays, 45);
        QCOMPARE(reread.config.excludedWindows[0].address, "0xabc12");
        QVERIFY(contents(path).contains("version_from_future = 9"));
        QVERIFY(contents(path).contains("future_option"));
        QVERIFY(contents(path).contains("future_rule_value = true"));
        QVERIFY(!(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
        auto changedRule = reread.config;
        changedRule.excludedWindows[0].titleRegex = "Different title";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::saveReplayConfig(changedRule, reread.original, path));
        QCOMPARE(contents(path), reread.original);
    }

    void customHistoryPathRoundTrip() {
        QTemporaryDir directory;
        const QString path = directory.filePath("config.toml");
        replay::ReplayConfig config;
        QCOMPARE(replay::replayHistoryDirectory(config), replay::replayPaths().historyDirectory);
        config.storageDirectory = directory.filePath("separate-disk-history");
        QVERIFY(QDir().mkdir(config.storageDirectory));
        replay::saveReplayConfig(config, {}, path);
        const auto loaded = replay::loadReplayConfig(path);
        QCOMPARE(loaded.config.storageDirectory, config.storageDirectory);
        QCOMPARE(replay::replayHistoryDirectory(loaded.config), config.storageDirectory);
        QVERIFY(QDir(config.storageDirectory).isEmpty());
        // A disconnected drive is valid configuration; availability is a runtime
        // gate so a restart cannot silently select the default archive instead.
        QVERIFY(QDir().rmdir(config.storageDirectory));
        QCOMPARE(replay::loadReplayConfig(path).config.storageDirectory, config.storageDirectory);
        for (const auto &invalid : QStringList{"relative/history", "/", "/tmp/history/", "/tmp/../history", "~/history"}) {
            auto changed = config; changed.storageDirectory = invalid;
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::saveReplayConfig(changed, loaded.original, path));
            QCOMPARE(contents(path), loaded.original);
        }
    }

    void invalidCurrentConfigUsesSavedHistoryWithoutWriting() {
        QTemporaryDir directory;
        Environment environment;
        for (const auto* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            environment.set(name, directory.filePath(name).toUtf8());
        const auto paths = replay::replayPaths();
        QVERIFY(QDir().mkpath(QFileInfo(paths.configFile).dir().absolutePath()));
        const QByteArray invalid = "[recording]\ninterval_seconds = 'invalid'\n";
        write(paths.configFile, invalid);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::resolveReplayConfig());
        QVERIFY(!QFileInfo::exists(paths.stateDirectory));
        QVERIFY(!QFileInfo::exists(paths.historyDirectory));
        QVERIFY(QDir().mkpath(paths.stateDirectory));
        const QString backup = paths.stateDirectory + "/last-valid-config.toml";
        const QString custom = directory.filePath("custom-history");
        const QByteArray valid = "[storage]\ndirectory = '" + custom.toUtf8() + "'\n";
        write(backup, valid);
        const auto resolved = replay::resolveReplayConfig();
        QVERIFY(resolved.usingLastValidConfig); QVERIFY(!resolved.configError.isEmpty());
        QCOMPARE(replay::replayHistoryDirectory(resolved.document.config), custom);
        QCOMPARE(resolved.document.original, valid);
        QCOMPARE(contents(paths.configFile), invalid); QCOMPARE(contents(backup), valid);
        QVERIFY(!QFileInfo::exists(paths.historyDirectory)); QVERIFY(!QFileInfo::exists(custom));
        write(paths.configFile, "[storage]\nretention_days = 7\n");
        const auto current = replay::resolveReplayConfig();
        QVERIFY(!current.usingLastValidConfig); QVERIFY(current.configError.isEmpty());
        QCOMPARE(current.document.config.retentionDays, 7);
        QCOMPARE(replay::replayHistoryDirectory(current.document.config), paths.historyDirectory);
        write(paths.configFile, invalid); write(backup, invalid);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::resolveReplayConfig());
        QCOMPARE(contents(paths.configFile), invalid); QCOMPARE(contents(backup), invalid);
    }

    void invalidSettingsDoNotReplaceSavedConfig() {
        QTemporaryDir directory;
        const QString path = directory.filePath("config.toml");
        const QVector<QByteArray> invalid{
            "[recording]\ninterval_seconds = 0\n", "[storage]\nretention_days = -1\n",
            "[indexing]\nactive_cpu_percent = 'fast'\n", "[indexing]\nidle_cpu_percent = nan\n",
            "[service]\nlogin_startup = 1\n", "[storage]\ndirectory = 5\n", "[exclusions]\napps = [4]\n",
            "[[exclusions.windows]]\ntitle_regex = '['\n", "[[exclusions.windows]]\napp_id = 'private'\nscope = 'focused'\n",
            "[[exclusions.windows]]\naddress = '0x123'\n",
            "[[exclusions.windows]]\naddress = '0x123'\ncompositor_instance = 'synthetic-instance'\n",
            "[recording\noutput = 'DP-1'\n"};
        for (const auto& bytes : invalid) {
            write(path, bytes);
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::loadReplayConfig(path));
            QCOMPARE(contents(path), bytes);
        }
        write(path, "[recording]\noutput = 'DP-3'\n");
        const auto valid = replay::loadReplayConfig(path);
        auto bad = valid.config; bad.intervalSeconds = std::numeric_limits<double>::infinity();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::saveReplayConfig(bad, valid.original, path));
        QCOMPARE(contents(path), valid.original);
        write(path, valid.original + "# changed by another writer\n");
        const auto changed = contents(path);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, replay::saveReplayConfig(valid.config, valid.original, path));
        QCOMPARE(contents(path), changed);
    }
};

QTEST_GUILESS_MAIN(ReplayConfigTest)
#include "replay_config_test.moc"
