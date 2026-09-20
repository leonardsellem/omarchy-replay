#include "activity_signals.h"

#include "ext-idle-notify-v1-client-protocol.h"
#include <wayland-client.h>

#include <QFile>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace replay {
namespace {
using Clock = std::chrono::steady_clock;
constexpr qint64 secondUs = 1000000;
constexpr qsizetype maxPressureBytes = 4096;
constexpr qsizetype maxCpuStatBytes = 4096;

qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

struct PressureReading {
    double avg10 = 0;
    quint64 total = 0;
};

std::optional<PressureReading> parsePressure(const QByteArray& input) {
    if (input.isEmpty() || input.size() > maxPressureBytes) return {};
    std::optional<PressureReading> result;
    for (const auto& line : input.split('\n')) {
        const auto fields = line.simplified().split(' ');
        if (fields.isEmpty() || fields[0] != "some") continue;
        if (result || fields.size() != 5) return {};
        PressureReading reading;
        unsigned seen = 0;
        for (qsizetype i = 1; i < fields.size(); ++i) {
            const auto pair = fields[i].split('=');
            if (pair.size() != 2 || pair[1].isEmpty()) return {};
            unsigned bit = 0;
            if (pair[0] == "total") {
                bit = 8;
                if (!std::all_of(pair[1].begin(), pair[1].end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
                bool ok = false;
                reading.total = pair[1].toULongLong(&ok);
                if (!ok) return {};
            } else {
                if (pair[0] == "avg10") bit = 1;
                else if (pair[0] == "avg60") bit = 2;
                else if (pair[0] == "avg300") bit = 4;
                else return {};
                bool ok = false;
                const double value = pair[1].toDouble(&ok);
                if (!ok || !std::isfinite(value) || value < 0 || value > 100) return {};
                if (bit == 1) reading.avg10 = value;
            }
            if (seen & bit) return {};
            seen |= bit;
        }
        if (seen != 15) return {};
        result = reading;
    }
    return result;
}

QByteArray readPressure() {
    QFile file(QStringLiteral("/proc/pressure/cpu"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto text = file.read(maxPressureBytes + 1);
    return file.error() == QFileDevice::NoError ? text : QByteArray{};
}

using CpuReading = std::array<quint64, 8>;

std::optional<CpuReading> parseCpuStat(const QByteArray &input) {
    if (input.isEmpty() || input.size() > maxCpuStatBytes) return {};
    const auto fields = input.left(input.indexOf('\n') < 0 ? input.size() : input.indexOf('\n')).simplified().split(' ');
    if (fields.size() < 9 || fields.size() > 11 || fields[0] != "cpu") return {};
    CpuReading reading{};
    quint64 total = 0;
    for (qsizetype i = 1; i < fields.size(); ++i) {
        if (fields[i].isEmpty() || !std::all_of(fields[i].begin(), fields[i].end(),
                [](char c) { return c >= '0' && c <= '9'; })) return {};
        bool ok = false;
        const quint64 value = fields[i].toULongLong(&ok);
        if (!ok) return {};
        // guest and guest_nice are already included in user and nice.
        if (i <= 8) {
            if (value > std::numeric_limits<quint64>::max() - total) return {};
            total += value;
            reading[i - 1] = value;
        }
    }
    return reading;
}

QByteArray readCpuStat() {
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto text = file.readLine(maxCpuStatBytes + 1);
    return file.error() == QFileDevice::NoError ? text : QByteArray{};
}

// Own connection: never consume WAYLAND_SOCKET, which may belong to capture or
// a toolkit. Nonblocking connect keeps a full compositor backlog from delaying
// startup; the two protocol syncs share a single one-second deadline.
wl_display* connectDisplay() {
    QByteArray path = qgetenv("WAYLAND_DISPLAY");
    if (path.isEmpty()) path = "wayland-0";
    if (!path.startsWith('/')) {
        const auto runtime = qgetenv("XDG_RUNTIME_DIR");
        if (runtime.isEmpty()) return nullptr;
        path = runtime + '/' + path;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path))) return nullptr;
    std::memcpy(address.sun_path, path.constData(), static_cast<size_t>(path.size()));
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return nullptr;
    // AF_UNIX nonblocking connect completes immediately or reports EAGAIN if
    // the listen backlog is full. Treat all failures conservatively as unknown.
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return nullptr;
    }
    return wl_display_connect_to_fd(fd); // Takes ownership, including on failure.
}

struct IdleMonitor {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    ext_idle_notifier_v1* notifier = nullptr;
    ext_idle_notification_v1* notification = nullptr;
    uint32_t seatGlobal = 0, notifierGlobal = 0;
    bool removed = false, idle = false, initialized = false;
    QString reason = QStringLiteral("unavailable");

    explicit IdleMonitor(int idleSeconds) {
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        display = connectDisplay();
        if (!display) return;
        registry = wl_display_get_registry(display);
        if (!registry) { stop("registry-failed"); return; }
        static const wl_registry_listener registryListener{
            [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
                auto& self = *static_cast<IdleMonitor*>(data);
                if (!version) return;
                if (!self.seat && std::strcmp(interface, wl_seat_interface.name) == 0) {
                    self.seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
                    self.seatGlobal = name;
                    static const wl_seat_listener listener{
                        [](void*, wl_seat*, uint32_t) {},
                        [](void*, wl_seat*, const char*) {}
                    };
                    if (self.seat) wl_seat_add_listener(self.seat, &listener, &self);
                } else if (!self.notifier && std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
                    self.notifier = static_cast<ext_idle_notifier_v1*>(
                        wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1));
                    self.notifierGlobal = name;
                }
            },
            [](void* data, wl_registry*, uint32_t name) {
                auto& self = *static_cast<IdleMonitor*>(data);
                if (name == self.seatGlobal || name == self.notifierGlobal) self.removed = true;
            }
        };
        wl_registry_add_listener(registry, &registryListener, this);
        if (!sync(deadline)) { stop("initialization-failed"); return; }
        if (!seat || !notifier) { stop("protocol-unavailable"); return; }
        notification = ext_idle_notifier_v1_get_idle_notification(notifier, static_cast<uint32_t>(idleSeconds) * 1000, seat);
        if (!notification) { stop("notification-failed"); return; }
        static const ext_idle_notification_v1_listener notificationListener{
            [](void* data, ext_idle_notification_v1*) { static_cast<IdleMonitor*>(data)->idle = true; },
            [](void* data, ext_idle_notification_v1*) { static_cast<IdleMonitor*>(data)->idle = false; }
        };
        ext_idle_notification_v1_add_listener(notification, &notificationListener, this);
        if (!sync(deadline)) { stop("initialization-failed"); return; }
        initialized = true;
        reason = QStringLiteral("connected");
    }

    ~IdleMonitor() { disconnect(); }

    void disconnect() {
        // Local destruction avoids sending requests to removed globals or a
        // failed connection. Disconnect releases all server-side resources.
        if (notification) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(notification));
        if (notifier) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(notifier));
        if (seat) wl_seat_destroy(seat);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
        notification = nullptr;
        notifier = nullptr;
        seat = nullptr;
        registry = nullptr;
        display = nullptr;
        initialized = false;
        idle = false;
    }

    void stop(const char* why) { reason = QString::fromLatin1(why); disconnect(); }

    // One bounded socket read; never use wl_display_dispatch/roundtrip, which
    // can block. This connection is used only by this sampler's calling thread.
    bool pump(int timeoutMs) {
        if (wl_display_dispatch_pending(display) < 0 || removed || wl_display_get_error(display)) return false;
        if (wl_display_prepare_read(display) != 0) return false;
        short events = POLLIN;
        if (wl_display_flush(display) < 0) {
            if (errno != EAGAIN) { wl_display_cancel_read(display); return false; }
            events |= POLLOUT;
        }
        pollfd descriptor{wl_display_get_fd(display), events, 0};
        const int result = poll(&descriptor, 1, timeoutMs);
        if (result < 0 || descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            wl_display_cancel_read(display);
            return result < 0 && errno == EINTR;
        }
        if (descriptor.revents & POLLIN) {
            if (wl_display_read_events(display) < 0) return false;
        } else wl_display_cancel_read(display);
        return wl_display_dispatch_pending(display) >= 0 && !removed && !wl_display_get_error(display);
    }

    bool sync(Clock::time_point deadline) {
        bool done = false;
        auto* callback = wl_display_sync(display);
        if (!callback) return false;
        static const wl_callback_listener listener{
            [](void* data, wl_callback*, uint32_t) { *static_cast<bool*>(data) = true; }
        };
        wl_callback_add_listener(callback, &listener, &done);
        bool ok = true;
        while (!done && ok) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0) { ok = false; break; }
            ok = pump(static_cast<int>(remaining));
        }
        wl_callback_destroy(callback);
        return ok;
    }

    std::optional<bool> sample() {
        if (!initialized) return {};
        if (!pump(0)) { stop(removed ? "global-removed" : "disconnected"); return {}; }
        return idle;
    }
};
} // namespace

struct ActivitySignals::Impl {
    int idleSeconds;
    Sources sources;
    std::unique_ptr<IdleMonitor> nativeIdle;
    ActivitySnapshot value;
    std::optional<PressureReading> previousPressure;
    std::optional<CpuReading> previousCpu;
    qint64 lastReadUs = -1;
    quint64 samples = 0, pressureReads = 0, pressureFailures = 0, pressureDeltas = 0, pressureFallbacks = 0;
    quint64 cpuReads = 0, cpuFailures = 0, cpuDeltas = 0, cpuBaselines = 0, cpuCounterResets = 0;

    Impl(int seconds, Sources input, bool native) : idleSeconds(seconds), sources(std::move(input)) {
        if (seconds < 1 || seconds > 86400) throw std::invalid_argument("Idle timeout must be between 1 and 86400 seconds");
        if (native) {
            nativeIdle = std::make_unique<IdleMonitor>(seconds);
            sources = {monotonicUs, readPressure, [this] { return nativeIdle->sample(); }, readCpuStat};
        }
    }

    ActivitySnapshot sample() {
        ++samples;
        value.idleKnown = false;
        value.idle = false;
        try {
            const auto idle = sources.idle ? sources.idle() : std::optional<bool>{};
            value.idleKnown = idle.has_value();
            value.idle = idle.value_or(false);
        } catch (...) {} // Source failure must not turn into an idle indication.

        qint64 now = -1;
        try { if (sources.monotonicMicroseconds) now = sources.monotonicMicroseconds(); } catch (...) {}
        if (now < 0 || (lastReadUs >= 0 && now < lastReadUs)) {
            value.pressureKnown = false;
            value.cpuPressurePercent = 0;
            value.cpuBusyKnown = false;
            value.cpuBusyPercent = 0;
            previousPressure.reset();
            previousCpu.reset();
            lastReadUs = -1;
            return value;
        }
        if (lastReadUs >= 0 && now - lastReadUs < secondUs) return value;
        const auto elapsedUs = lastReadUs >= 0 ? now - lastReadUs : 0;
        lastReadUs = now;
        ++pressureReads;
        std::optional<PressureReading> current;
        try { if (sources.cpuPressure) current = parsePressure(sources.cpuPressure()); } catch (...) {}
        value.pressureKnown = current.has_value();
        value.cpuPressurePercent = 0;
        if (current) {
            if (previousPressure && elapsedUs > 0 && current->total >= previousPressure->total) {
                // PSI updates are batched by the kernel; cap small boundary
                // overshoots to the physical 0..100% stall-time range.
                value.cpuPressurePercent = std::min(100.0, 100.0 * static_cast<double>(current->total - previousPressure->total) / elapsedUs);
                ++pressureDeltas;
            } else {
                value.cpuPressurePercent = current->avg10;
                ++pressureFallbacks;
            }
        } else ++pressureFailures;
        previousPressure = current;

        ++cpuReads;
        std::optional<CpuReading> cpu;
        try { if (sources.cpuStat) cpu = parseCpuStat(sources.cpuStat()); } catch (...) {}
        value.cpuBusyKnown = false;
        value.cpuBusyPercent = 0;
        if (!cpu) ++cpuFailures;
        else if (!previousCpu) ++cpuBaselines;
        else {
            quint64 total = 0, idle = 0;
            bool reset = false;
            for (size_t i = 0; i < cpu->size(); ++i) {
                if ((*cpu)[i] < (*previousCpu)[i]) { reset = true; break; }
                const auto delta = (*cpu)[i] - (*previousCpu)[i];
                total += delta;
                if (i == 3 || i == 4) idle += delta;
            }
            if (reset) ++cpuCounterResets;
            else if (total) {
                value.cpuBusyKnown = true;
                value.cpuBusyPercent = 100.0 * double(total - idle) / double(total);
                ++cpuDeltas;
            }
        }
        previousCpu = cpu;
        return value;
    }
};

ActivitySignals::ActivitySignals(int idleSeconds) : impl_(std::make_unique<Impl>(idleSeconds, Sources{}, true)) {}
ActivitySignals::ActivitySignals(int idleSeconds, Sources sources) : impl_(std::make_unique<Impl>(idleSeconds, std::move(sources), false)) {}
ActivitySignals::~ActivitySignals() = default;
ActivitySnapshot ActivitySignals::sample() { return impl_->sample(); }

QJsonObject ActivitySignals::statsJSON() const {
    const auto& p = *impl_;
    return {{"samples", static_cast<qint64>(p.samples)}, {"idle_seconds", p.idleSeconds},
            {"idle_known", p.value.idleKnown}, {"idle", p.value.idle},
            {"idle_source", p.nativeIdle ? p.nativeIdle->reason : QStringLiteral("injected")},
            {"pressure_known", p.value.pressureKnown}, {"cpu_pressure_percent", p.value.cpuPressurePercent},
            {"pressure_reads", static_cast<qint64>(p.pressureReads)},
            {"pressure_failures", static_cast<qint64>(p.pressureFailures)},
            {"pressure_delta_samples", static_cast<qint64>(p.pressureDeltas)},
            {"pressure_avg10_samples", static_cast<qint64>(p.pressureFallbacks)},
            {"cpu_busy_known", p.value.cpuBusyKnown}, {"cpu_busy_percent", p.value.cpuBusyPercent},
            {"cpu_busy_reads", static_cast<qint64>(p.cpuReads)},
            {"cpu_busy_failures", static_cast<qint64>(p.cpuFailures)},
            {"cpu_busy_delta_samples", static_cast<qint64>(p.cpuDeltas)},
            {"cpu_busy_baseline_samples", static_cast<qint64>(p.cpuBaselines)},
            {"cpu_busy_counter_resets", static_cast<qint64>(p.cpuCounterResets)}};
}

} // namespace replay
