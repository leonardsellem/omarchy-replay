#include "index_resources.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusUnixFileDescriptor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
struct UnitProperty { QString name; QVariant value; };
using UnitProperties = QList<UnitProperty>;
struct AuxiliaryUnit { QString name; UnitProperties properties; };
using AuxiliaryUnits = QList<AuxiliaryUnit>;
struct ExecCommand { QString path; QStringList arguments; bool ignoreFailure = false; };
using ExecCommands = QList<ExecCommand>;
QDBusArgument &operator<<(QDBusArgument &out, const ExecCommand &command) {
    out.beginStructure(); out << command.path << command.arguments << command.ignoreFailure; out.endStructure(); return out;
}
const QDBusArgument &operator>>(const QDBusArgument &in, ExecCommand &command) {
    in.beginStructure(); in >> command.path >> command.arguments >> command.ignoreFailure; in.endStructure(); return in;
}

QDBusArgument &operator<<(QDBusArgument &out, const UnitProperty &property) {
    out.beginStructure(); out << property.name << QDBusVariant(property.value); out.endStructure(); return out;
}
const QDBusArgument &operator>>(const QDBusArgument &in, UnitProperty &property) {
    QDBusVariant value;
    in.beginStructure(); in >> property.name >> value; in.endStructure(); property.value = value.variant(); return in;
}
QDBusArgument &operator<<(QDBusArgument &out, const AuxiliaryUnit &unit) {
    out.beginStructure(); out << unit.name << unit.properties; out.endStructure(); return out;
}
const QDBusArgument &operator>>(const QDBusArgument &in, AuxiliaryUnit &unit) {
    in.beginStructure(); in >> unit.name >> unit.properties; in.endStructure(); return in;
}
} // namespace
Q_DECLARE_METATYPE(UnitProperty)
Q_DECLARE_METATYPE(UnitProperties)
Q_DECLARE_METATYPE(AuxiliaryUnit)
Q_DECLARE_METATYPE(AuxiliaryUnits)
Q_DECLARE_METATYPE(ExecCommand)
Q_DECLARE_METATYPE(ExecCommands)

namespace replay {
namespace {
constexpr auto CgroupRoot = "/sys/fs/cgroup";
constexpr int BusTimeoutMs = 500;

[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

QString checkedPath(const QString &path) {
    if (!path.startsWith('/') || path.contains(QChar('\0')) || path.contains('\n') || path.contains('\r') ||
        path.size() > 4096 || QDir::cleanPath(path) != path)
        fail("Invalid cgroup path");
    for (const auto &part : path.split('/'))
        if (part == "." || part == "..") fail("Invalid cgroup path component");
    return path;
}

QString groupFile(const QString &group, const QString &file) {
    return QString::fromLatin1(CgroupRoot) + (group == "/" ? QString() : checkedPath(group)) + '/' + file;
}

QByteArray required(const IndexResources::Sources &sources, const QString &path) {
    const auto value = sources.readFile(path);
    if (!value) fail("Required resource data is unavailable: " + path);
    return value->trimmed();
}

QString currentGroup(const IndexResources::Sources &sources) {
    QString group;
    for (const auto &line : required(sources, "/proc/self/cgroup").split('\n')) {
        if (!line.startsWith("0::")) continue;
        if (!group.isEmpty()) fail("Ambiguous unified cgroup membership");
        group = checkedPath(QString::fromUtf8(line.mid(3)));
    }
    if (group.isEmpty()) fail("Unified cgroup v2 membership is unavailable");
    return group;
}

struct CpuMaximum { std::optional<double> percent; quint64 period = 0; };
CpuMaximum cpuMaximum(const QByteArray &data) {
    const auto parts = data.simplified().split(' ');
    if (parts.size() != 2) fail("Malformed cpu.max");
    bool ok = false;
    const auto period = parts[1].toULongLong(&ok);
    if (!ok || !period) fail("Malformed CPU quota period");
    if (parts[0] == "max") return {std::nullopt, period};
    const auto quota = parts[0].toULongLong(&ok);
    if (!ok || !quota) fail("Malformed CPU quota");
    return {100.0 * double(quota) / double(period), period};
}

QString parentGroup(const QString &group) {
    const auto pos = group.lastIndexOf('/');
    return pos <= 0 ? "/" : group.left(pos);
}

QDBusMessage callManager(const QDBusConnection &bus, const QString &path, const QString &interface,
                         const QString &method, const QVariantList &arguments) {
    auto request = QDBusMessage::createMethodCall("org.freedesktop.systemd1", path, interface, method);
    request.setAutoStartService(false);
    request.setArguments(arguments);
    const auto reply = bus.call(request, QDBus::Block, BusTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage)
        fail("User resource service unavailable: " + reply.errorName().left(160) + ": " + reply.errorMessage().left(256));
    return reply;
}

} // namespace

QJsonObject IndexResources::statsJSON() const { return stats_; }

namespace {
QString unitPath(const QDBusConnection &bus, const QString &unit) {
    const auto reply = callManager(bus, "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "GetUnit", {unit});
    if (reply.arguments().size() != 1) fail("Invalid systemd unit response");
    return qvariant_cast<QDBusObjectPath>(reply.arguments().first()).path();
}
QVariant unitProperty(const QDBusConnection &bus, const QString &path, const QString &interface, const QString &key) {
    const auto reply = callManager(bus, path, "org.freedesktop.DBus.Properties", "Get", {interface, key});
    if (reply.arguments().size() != 1) fail("Invalid systemd property response");
    return qvariant_cast<QDBusVariant>(reply.arguments().first()).variant();
}
IndexResources::Sources nativeSources(const QString &unit) {
    IndexResources::Sources sources;
    sources.pid = getpid();
    sources.readFile = [](const QString &path) -> std::optional<QByteArray> {
        const QFileInfo info(path);
        if (!info.exists()) return std::nullopt;
        if (path != "/proc/self/cgroup" &&
            !info.canonicalFilePath().startsWith(QString::fromLatin1(CgroupRoot) + '/'))
            fail("Resource path escapes cgroup filesystem");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) fail("Cannot read resource data: " + path);
        const auto data = file.read(16385);
        if (data.size() > 16384 || file.error() != QFile::NoError) fail("Resource data exceeds read bounds: " + path);
        return data;
    };
    sources.serviceProperty = [unit, path = QString()](const QString &key) mutable {
        const auto bus = QDBusConnection::sessionBus();
        if (path.isEmpty()) path = unitPath(bus, unit);
        return unitProperty(bus, path, "org.freedesktop.systemd1.Service", key);
    };
    return sources;
}
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) close(value); }
    void reset() { if (value >= 0) close(value); value = -1; }
    Fd(const Fd &) = delete;
};
bool unitGone(const QDBusConnection &bus, const QString &unit) {
    auto request = QDBusMessage::createMethodCall("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                                "org.freedesktop.systemd1.Manager", "GetUnit");
    request.setAutoStartService(false); request.setArguments({unit});
    const auto reply = bus.call(request, QDBus::Block, BusTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage &&
        reply.errorName() == "org.freedesktop.systemd1.NoSuchUnit") return true;
    if (reply.type() != QDBusMessage::ReplyMessage) fail("Cannot establish managed worker unit state");
    return false;
}
void stopUnit(const QDBusConnection &bus, const QString &unit) {
    if (unitGone(bus, unit)) return;
    try {
        if (unitProperty(bus, unitPath(bus, unit), "org.freedesktop.systemd1.Unit", "Description").toString() !=
            "Replay background text indexing: " + unit)
            fail("Managed worker unit identity changed; refusing to stop it");
    } catch (...) {
        if (unitGone(bus, unit)) return; // Completion raced the identity lookup.
        throw;
    }
    auto request = QDBusMessage::createMethodCall("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                                "org.freedesktop.systemd1.Manager", "StopUnit");
    request.setAutoStartService(false); request.setArguments({unit, "replace"});
    const auto reply = bus.call(request, QDBus::Block, BusTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage && reply.errorName() != "org.freedesktop.systemd1.NoSuchUnit")
        fail("Could not stop the owned managed index unit");
}
void cleanupUnit(const QDBusConnection &bus, const QString &unit) {
    stopUnit(bus, unit);
    QElapsedTimer clock; clock.start();
    while (!unitGone(bus, unit)) {
        if (clock.elapsed() >= 5000) fail("Managed worker cleanup could not be verified; direct fallback is blocked");
        QThread::msleep(25);
    }
}
}

quint64 processStartTicks(qint64 pid) {
    if (pid <= 0) return 0;
    QFile file(QString("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const auto data = file.read(8192);
    const auto fields = data.mid(data.lastIndexOf(')') + 2).simplified().split(' ');
    return fields.size() > 19 ? fields[19].toULongLong() : 0;
}

IndexResources::IndexResources(double ceiling, const QString &serviceUnit, const QString &unavailableReason)
    : IndexResources(ceiling, serviceUnit, unavailableReason, nativeSources(serviceUnit)) {}

IndexResources::IndexResources(double ceiling, const QString &serviceUnit, const QString &unavailableReason, Sources sources) {
    if (!std::isfinite(ceiling) || ceiling < 0 || ceiling > 100 || (ceiling > 0 && ceiling < 1))
        throw std::invalid_argument("Index CPU ceiling must be 0 or 1..100 percent of one CPU");
    stats_ = {{"requested_cpu_percent", ceiling}, {"state", ceiling == 0 ? "disabled" : "unavailable"},
              {"enforced", false}, {"effective_cpu_percent", QJsonValue::Null}, {"verified_at_ms", QJsonValue::Null}};
    stats_["scope"] = serviceUnit.isEmpty()
        ? "Direct index worker in its caller's inherited resource group"
        : "Independent Replay index service and descendants; shared user-slice ancestor limits still apply";
    if (!ceiling) return;
    stats_["state"] = "unavailable";
    try {
        if (serviceUnit.isEmpty()) fail(unavailableReason.isEmpty() ? "Managed index service is unavailable" : unavailableReason);
        if (!QRegularExpression("^oma-replay-index-[0-9]+-[a-f0-9]{32}\\.service$").match(serviceUnit).hasMatch())
            fail("Invalid managed index unit name");
        if (sources.pid <= 0 || !sources.readFile || !sources.serviceProperty) fail("Incomplete worker verification sources");
        if (sources.serviceProperty("MainPID").toLongLong() != sources.pid)
            fail("Managed index unit does not own this worker");
        if (sources.serviceProperty("WatchdogUSec").toULongLong() != 10000000 ||
            sources.serviceProperty("WatchdogSignal").toInt() != SIGKILL ||
            sources.serviceProperty("NotifyAccess").toString() != "main")
            fail("Managed index freeze protection was not configured as requested");
        const auto target = checkedPath(sources.serviceProperty("ControlGroup").toString());
        if (currentGroup(sources) != target) fail("Worker cgroup does not match its managed service");
        const auto maximum = cpuMaximum(required(sources, groupFile(target, "cpu.max")));
        const auto weight = required(sources, groupFile(target, "cpu.weight")).toULongLong();
        const auto tasks = required(sources, groupFile(target, "pids.max")).toULongLong();
        if (!maximum.percent || *maximum.percent > ceiling + .001 || weight != 10 || tasks == 0 || tasks > 64)
            fail("Managed worker resource limits were not enforced as requested");
        double effective = *maximum.percent;
        QJsonArray ancestors;
        QString group = parentGroup(target);
        for (int depth = 0; ; ++depth, group = parentGroup(group)) {
            if (depth >= 64) fail("Cgroup ancestry exceeds verification bounds");
            if (const auto data = sources.readFile(groupFile(group, "cpu.max")))
                if (const auto limit = cpuMaximum(*data).percent) {
                    effective = std::min(effective, *limit);
                    ancestors.append(QJsonObject{{"cgroup", group}, {"cpu_percent", *limit}});
                }
            if (group == "/") break;
        }
        stats_["state"] = "enforced"; stats_["enforced"] = true;
        stats_["service_unit"] = serviceUnit; stats_["cgroup"] = target;
        stats_["cpu_weight"] = qint64(weight); stats_["tasks_max"] = qint64(tasks);
        stats_["watchdog_seconds"] = 10; stats_["watchdog_signal"] = "SIGKILL";
        stats_["scope_cpu_percent"] = *maximum.percent;
        stats_["quota_period_usec"] = qint64(maximum.period);
        stats_["effective_cpu_percent"] = effective; stats_["ancestor_cpu_limits"] = ancestors;
        stats_["verified_at_ms"] = QDateTime::currentMSecsSinceEpoch();
    } catch (const std::exception &error) {
        stats_["reason"] = QString::fromUtf8(error.what()).left(512);
        stats_["fallback"] = "Configured cooperative OCR pacing and existing process priority remain in effect";
    }
}

struct IndexOwnerGuard::Impl {
    Fd owner, finished, notify;
    std::thread watcher;
    ~Impl() {
        const uint64_t done = 1;
        if (finished.value >= 0) (void)write(finished.value, &done, sizeof(done));
        if (watcher.joinable()) watcher.join();
    }
};
IndexOwnerGuard::IndexOwnerGuard(qint64 ownerPid, quint64 startTicks) : impl_(std::make_unique<Impl>()) {
    if (ownerPid <= 1 || ownerPid == getpid() || !startTicks || processStartTicks(ownerPid) != startTicks)
        fail("Managed index controller identity is unavailable");
    impl_->owner.value = int(syscall(SYS_pidfd_open, ownerPid, 0));
    impl_->finished.value = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (impl_->owner.value < 0 || impl_->finished.value < 0 || processStartTicks(ownerPid) != startTicks)
        fail("Managed index controller lifetime cannot be monitored");
    const auto notifyPath = qgetenv("NOTIFY_SOCKET");
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    if ((notifyPath.isEmpty() || (notifyPath[0] != '/' && notifyPath[0] != '@')) ||
        notifyPath.size() >= qsizetype(sizeof(address.sun_path)) ||
        qEnvironmentVariable("WATCHDOG_USEC").toULongLong() != 10000000 ||
        qEnvironmentVariable("WATCHDOG_PID").toLongLong() != getpid())
        fail("Managed index watchdog transport is unavailable");
    std::memcpy(address.sun_path, notifyPath.constData(), notifyPath.size());
    if (notifyPath[0] == '@') address.sun_path[0] = '\0';
    impl_->notify.value = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    const auto addressSize = socklen_t(offsetof(sockaddr_un, sun_path) + notifyPath.size() + (notifyPath[0] == '/' ? 1 : 0));
    if (impl_->notify.value < 0 || connect(impl_->notify.value, reinterpret_cast<sockaddr *>(&address), addressSize) != 0 ||
        send(impl_->notify.value, "WATCHDOG=1", 10, MSG_NOSIGNAL) != 10)
        fail("Cannot connect managed index watchdog transport");
    pollfd initial{impl_->owner.value, POLLIN, 0};
    if (poll(&initial, 1, 0) != 0) fail("Managed index controller has exited");
    auto *state = impl_.get();
    state->watcher = std::thread([state] {
        pollfd descriptors[]{{state->owner.value, POLLIN, 0}, {state->finished.value, POLLIN, 0}};
        for (;;) {
            const int status = poll(descriptors, 2, 2000);
            if (status < 0 && errno == EINTR) continue;
            if (descriptors[1].revents) return;
            if (status != 0 || send(state->notify.value, "WATCHDOG=1", 10, MSG_NOSIGNAL) != 10) break;
        }
        // Also fail closed on an unexpected pidfd polling error.
        kill(getpid(), SIGTERM);
        pollfd done{state->finished.value, POLLIN, 0};
        QElapsedTimer grace; grace.start();
        while (grace.elapsed() < 3000) {
            const int result = poll(&done, 1, int(3000 - grace.elapsed()));
            if (result > 0) return;
            if (result == 0 || errno != EINTR) break;
        }
        kill(getpid(), SIGKILL);
    });
}
IndexOwnerGuard::~IndexOwnerGuard() = default;

ManagedIndexResult runManagedIndex(const QStringList &arguments, double ceiling,
                                  const std::function<bool()> &stopRequested) {
    ManagedIndexResult result;
    const auto bus = QDBusConnection::sessionBus();
    const QString unit = QString("oma-replay-index-%1-%2.service").arg(getpid()).arg(QUuid::createUuid().toString(QUuid::Id128));
    bool startAttempted = false;
    Fd outRead, outWrite, errRead, errWrite;
    try {
        if (!bus.isConnected()) fail("User session bus is unavailable");
        if (!(bus.connectionCapabilities() & QDBusConnection::UnixFileDescriptorPassing)) fail("User bus cannot carry private worker pipes");
        Fd pidfd(int(syscall(SYS_pidfd_open, getpid(), 0)));
        if (pidfd.value < 0) fail("Kernel pidfd lifetime monitoring is unavailable");
        // Verify manager access before a start request may become ambiguous.
        (void)unitPath(bus, "-.slice");
        int descriptors[2];
        if (pipe2(descriptors, O_CLOEXEC) != 0) fail("Cannot create private worker output pipe");
        outRead.value = descriptors[0]; outWrite.value = descriptors[1];
        if (pipe2(descriptors, O_CLOEXEC) != 0) fail("Cannot create private worker error pipe");
        errRead.value = descriptors[0]; errWrite.value = descriptors[1];
        if (fcntl(outRead.value, F_SETFL, O_NONBLOCK) || fcntl(errRead.value, F_SETFL, O_NONBLOCK))
            fail("Cannot configure bounded worker output reads");
        qDBusRegisterMetaType<UnitProperty>(); qDBusRegisterMetaType<UnitProperties>();
        qDBusRegisterMetaType<AuxiliaryUnit>(); qDBusRegisterMetaType<AuxiliaryUnits>();
        qDBusRegisterMetaType<ExecCommand>(); qDBusRegisterMetaType<ExecCommands>();
        const QString executable = QCoreApplication::applicationFilePath();
        QStringList workerArguments{executable}; workerArguments << arguments;
        workerArguments << "--resource-unit" << unit << "--owner-pid" << QString::number(getpid())
                        << "--owner-start-ticks" << QString::number(processStartTicks(getpid()));
        // These preserve local OCR/model and compositor behavior without copying
        // arbitrary caller secrets into transient unit environment properties.
        QStringList environment, unset;
        const auto current = QProcessEnvironment::systemEnvironment();
        for (const auto *key : {"HOME", "PATH", "LANG", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", "TESSDATA_PREFIX",
                               "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DBUS_SESSION_BUS_ADDRESS"}) {
            if (current.contains(key)) environment << QString(key) + '=' + current.value(key);
            else unset << key;
        }
        environment << "OMP_THREAD_LIMIT=1" << "QT_QPA_PLATFORM=offscreen";
        UnitProperties properties{
            {"Description", "Replay background text indexing: " + unit}, {"Slice", "background.slice"},
            {"Type", "exec"}, {"ExecStart", QVariant::fromValue(ExecCommands{{executable, workerArguments, false}})},
            {"Environment", environment}, {"UnsetEnvironment", unset}, {"WorkingDirectory", QDir::currentPath()},
            {"StandardInput", "null"},
            {"StandardOutputFileDescriptor", QVariant::fromValue(QDBusUnixFileDescriptor(outWrite.value))},
            {"StandardErrorFileDescriptor", QVariant::fromValue(QDBusUnixFileDescriptor(errWrite.value))},
            {"CPUAccounting", true}, {"CPUWeight", QVariant::fromValue(quint64(10))},
            {"CPUQuotaPerSecUSec", QVariant::fromValue(quint64(std::floor(ceiling * 10000)))},
            {"CPUQuotaPeriodUSec", QVariant::fromValue(quint64(100000))},
            {"TasksMax", QVariant::fromValue(quint64(64))}, {"Nice", 10}, {"UMask", uint(0077)},
            {"WatchdogUSec", QVariant::fromValue(quint64(10000000))}, {"WatchdogSignal", int(SIGKILL)}, {"NotifyAccess", "main"},
            {"KillMode", "control-group"}, {"TimeoutStartUSec", QVariant::fromValue(quint64(5000000))},
            {"TimeoutStopUSec", QVariant::fromValue(quint64(3000000))}, {"CollectMode", "inactive-or-failed"}
        };
        if (stopRequested()) fail("Managed index launch was canceled");
        startAttempted = true;
        callManager(bus, "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "StartTransientUnit",
                    {unit, "fail", QVariant::fromValue(properties), QVariant::fromValue(AuxiliaryUnits{})});
        // Drop every local write end, including the descriptor copies in variants.
        properties.clear(); outWrite.reset(); errWrite.reset();
        result.launched = true;
        bool stopping = false;
        QElapsedTimer shutdown; QElapsedTimer health; health.start();
        const auto readPipe = [](Fd &fd, QByteArray &buffer, int maximum) {
            char bytes[8192];
            for (int reads = 0; reads < 32; ++reads) {
                const auto count = read(fd.value, bytes, sizeof(bytes));
                if (count > 0) { buffer.append(bytes, count); if (buffer.size() > maximum) buffer = buffer.right(maximum); }
                else if (count == 0) { fd.reset(); return; }
                else if (errno != EINTR) return;
            }
        };
        while (outRead.value >= 0 || errRead.value >= 0) {
            if (stopRequested() && !stopping) { stopUnit(bus, unit); stopping = true; shutdown.start(); }
            if (stopping && shutdown.elapsed() > 5000) fail("Managed index shutdown exceeded its bound");
            pollfd descriptors[]{{outRead.value, POLLIN, 0}, {errRead.value, POLLIN, 0}};
            poll(descriptors, 2, 100);
            if (outRead.value >= 0) readPipe(outRead, result.output, 1024 * 1024);
            if (errRead.value >= 0) readPipe(errRead, result.errors, 8192);
            if (health.elapsed() > 2000) { (void)unitGone(bus, unit); health.restart(); }
        }
        cleanupUnit(bus, unit);
        const auto report = QJsonDocument::fromJson(result.output);
        result.exitCode = report.isObject() ? report.object().value("worker_exit_code").toInt(1) : 1;
        return result;
    } catch (const std::exception &error) {
        result.unavailableReason = QString::fromUtf8(error.what()).left(512);
        if (startAttempted) {
            // A timed-out D-Bus request can have succeeded. Never run a fallback
            // worker alongside a unit whose termination cannot be established.
            cleanupUnit(bus, unit);
            if (result.launched) {
                result.errors += "Managed index controller: " + result.unavailableReason.toUtf8() + '\n';
                return result;
            }
        }
        return result;
    }
}
} // namespace replay
