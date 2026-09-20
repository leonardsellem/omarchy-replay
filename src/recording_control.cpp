#include "recording_service.h"
#include "replay_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace replay {
namespace {
void fail(const char *message) { throw std::runtime_error(message); }

void privateDirectory(const QString &path) {
    if (QFileInfo(path).isSymLink() || !QDir().mkpath(path)) fail("Cannot prepare Replay controls");
    struct stat metadata{};
    if (::lstat(QFile::encodeName(path).constData(), &metadata) != 0 ||
        !S_ISDIR(metadata.st_mode) || metadata.st_uid != ::geteuid() ||
        !QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        fail("Replay control directories must be private and owned by the current user");
}

void checkPrivateFile(int descriptor) {
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_uid != ::geteuid() || metadata.st_nlink != 1 || (metadata.st_mode & 0077) != 0)
        fail("Replay control state must be a private, owned regular file");
}

QJsonObject savedState() {
    const auto name = QFile::encodeName(replayPaths().stateDirectory + "/recording.json");
    const int descriptor = ::open(name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) {
        if (errno == ENOENT) return {};
        fail("Cannot read Replay recording state");
    }
    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor); fail("Cannot read Replay recording state");
    }
    checkPrivateFile(descriptor);
    const auto bytes = file.read(128 * 1024 + 1);
    const auto document = QJsonDocument::fromJson(bytes);
    if (bytes.size() > 128 * 1024 || !document.isObject()) fail("Invalid Replay recording state");
    const auto state = document.object();
    if (state.contains("intent") && (!state.value("intent").isString() ||
        !QStringList{"running", "paused", "stopped"}.contains(state.value("intent").toString())))
        fail("Invalid saved recording intent");
    if (state.contains("indexing_paused") && !state.value("indexing_paused").isBool())
        fail("Invalid saved indexing intent");
    return state;
}

QJsonObject offlineStatus(const QJsonObject &state) {
    auto result = QJsonObject{{"available", true}, {"running", false}, {"intent", state.value("intent").toString("stopped")},
            {"indexing_paused", state.value("indexing_paused").toBool()}, {"state", "offline"},
            {"reason", "Recording service is offline."}};
    try { result["history_directory"] = replayHistoryDirectory(loadReplayConfig().config); }
    catch (const std::exception &error) { result["config_error"] = QString::fromUtf8(error.what()).left(500); }
    return result;
}

// Share the daemon's exact inode and flock protocol. Never remove this file:
// replacing it would let two processes believe that they own the coordinator.
class ControlLease {
    QFile file;
public:
    bool acquired = false;
    explicit ControlLease(const QString &directory) {
        privateDirectory(directory);
        const auto name = QFile::encodeName(directory + "/coordinator.lock");
        const int descriptor = ::open(name.constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        if (descriptor < 0) fail("Cannot open Replay coordinator lock");
        if (!file.open(descriptor, QIODevice::ReadWrite, QFileDevice::AutoCloseHandle)) {
            ::close(descriptor); fail("Cannot open Replay coordinator lock");
        }
        checkPrivateFile(descriptor);
        acquired = ::flock(descriptor, LOCK_EX | LOCK_NB) == 0;
        if (!acquired && errno != EWOULDBLOCK && errno != EAGAIN) fail("Cannot lock Replay recording controls");
    }
};

void setStartupHold(bool hold) {
    const QString path = replayPaths().runtimeDirectory + "/hold-capture-on-start";
    struct stat metadata{};
    const auto name = QFile::encodeName(path);
    if (::lstat(name.constData(), &metadata) == 0) {
        if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || metadata.st_nlink != 1)
            fail("Invalid Replay startup capture hold");
        if (!hold && !QFile::remove(path)) fail("Cannot clear Replay startup capture hold");
    } else if (errno != ENOENT) fail("Cannot inspect Replay startup capture hold");
    if (!hold) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write("pause\n") != 6 || !file.commit()) fail("Cannot hold capture while starting Replay controls");
}

void saveState(const QJsonObject &state) {
    const auto directory = replayPaths().stateDirectory;
    privateDirectory(directory);
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    if (bytes.size() > 128 * 1024) fail("Replay recording state is too large");
    QSaveFile file(directory + "/recording.json");
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) fail("Cannot save Replay recording intent");
    if (file.write(bytes) != bytes.size() || !file.flush() || ::fsync(file.handle()) != 0 || !file.commit())
        fail("Cannot commit Replay recording intent");
    const int descriptor = ::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) fail("Cannot sync Replay recording intent");
    const int result = ::fsync(descriptor); ::close(descriptor);
    if (result != 0) fail("Cannot sync Replay recording intent");
}

QJsonObject request(const QString &action, const QJsonObject &arguments) {
    QLocalSocket socket;
    socket.connectToServer(replayPaths().runtimeDirectory + "/control.sock");
    if (!socket.waitForConnected(250)) return {};
    QJsonObject payload = arguments; payload["action"] = action;
    socket.write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(500)) throw std::runtime_error("Replay control request could not be sent");
    QElapsedTimer deadline; deadline.start(); QByteArray bytes;
    while (!bytes.contains('\n') && deadline.elapsed() < 8000) {
        socket.waitForReadyRead(100); bytes += socket.readAll();
        if (bytes.size() > 128 * 1024) throw std::runtime_error("Replay control response is too large");
        if (socket.state() == QLocalSocket::UnconnectedState) break;
    }
    const auto document = QJsonDocument::fromJson(bytes.trimmed());
    if (!document.isObject()) throw std::runtime_error("Replay coordinator did not return a valid response");
    const auto result = document.object();
    if (result.isEmpty()) fail("Replay coordinator returned an empty response");
    if (result.contains("error")) throw std::runtime_error(result.value("error").toString().toStdString());
    return result;
}

void startCoordinator() {
    const auto paths = replayPaths();
    const QString unit = QFileInfo(paths.configFile).dir().absolutePath() + "/../systemd/user/omarchy-replay.service";
    if (QFileInfo::exists(unit)) {
        QProcess service;
        service.start("systemctl", {"--user", "start", "omarchy-replay.service"});
        if (!service.waitForFinished(5000) || service.exitCode() != 0)
            throw std::runtime_error("Could not start omarchy-replay.service; inspect its user journal");
    } else {
        privateDirectory(paths.stateDirectory);
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath()); child.setArguments({"daemon", "run"});
        child.setStandardInputFile(QProcess::nullDevice()); child.setStandardOutputFile(QProcess::nullDevice());
        child.setStandardErrorFile(QProcess::nullDevice());
        child.setWorkingDirectory(paths.stateDirectory);
        if (!child.startDetached()) throw std::runtime_error("Could not start Replay coordinator");
    }
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 5000) {
        if (!request("status", {}).isEmpty()) return;
        QThread::msleep(50);
    }
    throw std::runtime_error("Replay coordinator did not become ready; inspect configuration and service logs");
}
} // namespace

QJsonObject recordingServiceStatus() {
    const auto result = request("status", {});
    if (!result.isEmpty()) return result;
    return offlineStatus(savedState());
}

QJsonObject controlRecordingService(const QString &action, const QJsonObject &arguments) {
    const QStringList allowed{"start", "pause", "resume", "stop", "shutdown", "index-pause", "index-resume", "reload", "delete-recent"};
    if (!allowed.contains(action)) throw std::runtime_error("Unknown recording control");
    QElapsedTimer deadline; deadline.start();
    while (true) {
        // Only a failed connection is safe to retry. A request that was sent but
        // lost its reply throws, so destructive controls cannot run twice.
        const auto result = request(action, arguments);
        if (!result.isEmpty()) return result;
        ControlLease lease(replayPaths().runtimeDirectory);
        if (lease.acquired) {
            if (action == "pause" || action == "stop" || action == "shutdown") {
                auto state = savedState();
                state["intent"] = action == "pause" ? "paused" : "stopped";
                saveState(state);
                setStartupHold(false);
                return offlineStatus(state);
            }
            // Prepare this while startup is excluded, then release the lease
            // before launching. Even a competing startup must consume the hold
            // before it can restore saved recording intent.
            setStartupHold(action != "start" && action != "resume");
            break;
        }
        if (deadline.elapsed() >= 5000) fail("Replay coordinator is starting or busy; the recording intent was not changed");
        QThread::msleep(50);
    }
    startCoordinator();
    const auto result = request(action, arguments);
    if (result.isEmpty()) throw std::runtime_error("Replay coordinator stopped before handling the request");
    return result;
}
} // namespace replay
