#include "capture.h"

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include <wayland-client.h>

#include <QTransform>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <limits>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace replay {
namespace {
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr uint32_t maxDimension = 8192;
constexpr uint64_t maxPixels = 16ULL * 1024 * 1024;

Deadline deadlineFor(int timeoutMs) {
    if (timeoutMs < 1 || timeoutMs > 30000)
        throw std::runtime_error("Capture timeout must be between 1 and 30000 ms");
    return Clock::now() + std::chrono::milliseconds(timeoutMs);
}

QImage::Format imageFormat(uint32_t format) {
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888: return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888: return QImage::Format_ARGB32;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    case WL_SHM_FORMAT_XBGR8888: return QImage::Format_RGBX8888;
    case WL_SHM_FORMAT_ABGR8888: return QImage::Format_RGBA8888;
#endif
    default: return QImage::Format_Invalid;
    }
}
} // namespace

struct WaylandCapture::Impl {
    struct Output {
        wl_output* proxy = nullptr;
        uint32_t global = 0;
        QString name;
        bool removed = false;
    };
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_shm* shm = nullptr;
    ext_output_image_capture_source_manager_v1* sourceManager = nullptr;
    ext_image_copy_capture_manager_v1* captureManager = nullptr;
    ext_image_capture_source_v1* source = nullptr;
    ext_image_copy_capture_session_v1* session = nullptr;
    ext_image_copy_capture_frame_v1* frame = nullptr;
    std::vector<std::unique_ptr<Output>> outputs;
    Output* selected = nullptr;
    wl_buffer* buffer = nullptr;
    void* pixels = MAP_FAILED;
    size_t byteCount = 0;
    uint32_t bufferWidth = 0, bufferHeight = 0, bufferFormat = 0;
    uint32_t width = 0, height = 0, format = std::numeric_limits<uint32_t>::max();
    uint32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
    bool constraintsDone = false, ready = false, stopped = false;
    QString failure;

    ~Impl() {
        finishSession();
        releaseBuffer();
        if (source) ext_image_capture_source_v1_destroy(source);
        if (captureManager) ext_image_copy_capture_manager_v1_destroy(captureManager);
        if (sourceManager) ext_output_image_capture_source_manager_v1_destroy(sourceManager);
        if (shm) wl_shm_destroy(shm);
        for (const auto& output : outputs) {
            if (wl_output_get_version(output->proxy) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
                wl_output_release(output->proxy);
            else wl_output_destroy(output->proxy);
        }
        if (registry) wl_registry_destroy(registry);
        if (display) {
            wl_display_flush(display);
            wl_display_disconnect(display);
        }
    }

    void failIfNeeded() {
        if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
        if (stopped || (selected && selected->removed))
            throw std::runtime_error("Capture output stopped or was removed");
        if (wl_display_get_error(display))
            throw std::runtime_error("Wayland connection failed");
    }

    void waitUntil(const std::function<bool()>& complete, Deadline deadline) {
        for (;;) {
            if (wl_display_dispatch_pending(display) < 0)
                throw std::runtime_error("Wayland dispatch failed");
            failIfNeeded();
            if (complete()) return;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0) throw CaptureTimeout("Wayland capture timed out; no new observation was recorded");
            if (wl_display_prepare_read(display) != 0) continue;
            short events = POLLIN;
            if (wl_display_flush(display) < 0) {
                if (errno != EAGAIN) {
                    wl_display_cancel_read(display);
                    throw std::runtime_error("Wayland flush failed");
                }
                events |= POLLOUT;
            }
            pollfd fd{wl_display_get_fd(display), events, 0};
            const int result = poll(&fd, 1, static_cast<int>(remaining));
            if (result < 0) {
                wl_display_cancel_read(display);
                if (errno == EINTR) continue;
                throw std::runtime_error("Wayland poll failed");
            }
            if (result == 0) {
                wl_display_cancel_read(display);
                throw CaptureTimeout("Wayland capture timed out; no new observation was recorded");
            }
            if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                wl_display_cancel_read(display);
                throw std::runtime_error("Wayland compositor disconnected");
            }
            if (fd.revents & POLLIN) {
                if (wl_display_read_events(display) < 0)
                    throw std::runtime_error("Wayland read failed");
            } else wl_display_cancel_read(display);
        }
    }

    void sync(Deadline deadline) {
        bool done = false;
        wl_callback* callback = wl_display_sync(display);
        if (!callback) throw std::runtime_error("Cannot allocate Wayland sync callback");
        static const wl_callback_listener listener{
            [](void* data, wl_callback*, uint32_t) { *static_cast<bool*>(data) = true; }
        };
        wl_callback_add_listener(callback, &listener, &done);
        try { waitUntil([&] { return done; }, deadline); }
        catch (...) { wl_callback_destroy(callback); throw; }
        wl_callback_destroy(callback);
    }

    void connect(int timeoutMs) {
        const auto deadline = deadlineFor(timeoutMs);
        display = wl_display_connect(nullptr);
        if (!display) throw std::runtime_error("Cannot connect to WAYLAND_DISPLAY");
        registry = wl_display_get_registry(display);
        if (!registry) throw std::runtime_error("Cannot get Wayland registry");
        static const wl_registry_listener listener{
            [](void* data, wl_registry* reg, uint32_t name, const char* interface, uint32_t version) {
                auto* self = static_cast<Impl*>(data);
                if (std::strcmp(interface, wl_shm_interface.name) == 0) {
                    self->shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
                    static const wl_shm_listener shmListener{
                        [](void*, wl_shm*, uint32_t) {}
                    };
                    wl_shm_add_listener(self->shm, &shmListener, self);
                } else if (std::strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
                    self->sourceManager = static_cast<ext_output_image_capture_source_manager_v1*>(
                        wl_registry_bind(reg, name, &ext_output_image_capture_source_manager_v1_interface, 1));
                } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
                    self->captureManager = static_cast<ext_image_copy_capture_manager_v1*>(
                        wl_registry_bind(reg, name, &ext_image_copy_capture_manager_v1_interface, 1));
                } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
                    auto output = std::make_unique<Output>();
                    output->global = name;
                    output->name = QStringLiteral("output-%1").arg(name);
                    output->proxy = static_cast<wl_output*>(wl_registry_bind(reg, name, &wl_output_interface, std::min(version, 4U)));
                    static const wl_output_listener outputListener{
                        [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
                        [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {},
                        [](void*, wl_output*) {},
                        [](void*, wl_output*, int32_t) {},
                        [](void* object, wl_output*, const char* value) { static_cast<Output*>(object)->name = QString::fromUtf8(value); },
                        [](void*, wl_output*, const char*) {}
                    };
                    wl_output_add_listener(output->proxy, &outputListener, output.get());
                    self->outputs.push_back(std::move(output));
                }
            },
            [](void* data, wl_registry*, uint32_t name) {
                for (const auto& output : static_cast<Impl*>(data)->outputs)
                    if (output->global == name) output->removed = true;
            }
        };
        wl_registry_add_listener(registry, &listener, this);
        sync(deadline);
        sync(deadline); // Output name events follow the registry bindings.
    }

    void finishSession() noexcept {
        if (frame) ext_image_copy_capture_frame_v1_destroy(frame);
        frame = nullptr;
        if (session) ext_image_copy_capture_session_v1_destroy(session);
        session = nullptr;
        if (display) wl_display_flush(display);
    }

    void releaseBuffer() noexcept {
        if (buffer) wl_buffer_destroy(buffer);
        buffer = nullptr;
        if (pixels != MAP_FAILED) munmap(pixels, byteCount);
        pixels = MAP_FAILED;
        byteCount = 0;
    }

    void ensureBuffer() {
        if (!width || !height || width > maxDimension || height > maxDimension || uint64_t(width) * height > maxPixels)
            throw std::runtime_error("Capture exceeds limit: 8192 pixels per side and 16 megapixels total");
        if (imageFormat(format) == QImage::Format_Invalid)
            throw std::runtime_error("Compositor did not advertise a supported 32-bit SHM format");
        if (buffer && bufferWidth == width && bufferHeight == height && bufferFormat == format) return;
        releaseBuffer();
        const size_t bytes = size_t(width) * height * 4;
        const int fd = memfd_create("omarchy-replay-capture", MFD_CLOEXEC);
        if (fd < 0) throw std::runtime_error("Cannot allocate capture memfd");
        if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            close(fd);
            throw std::runtime_error("Cannot size capture memfd");
        }
        pixels = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pixels == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("Cannot map capture buffer");
        }
        byteCount = bytes;
        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(bytes));
        close(fd);
        if (!pool) throw std::runtime_error("Cannot create Wayland SHM pool");
        buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, format);
        wl_shm_pool_destroy(pool);
        if (!buffer) throw std::runtime_error("Cannot create Wayland SHM buffer");
        bufferWidth = width;
        bufferHeight = height;
        bufferFormat = format;
    }

    QImage capture(int timeoutMs) {
        const auto deadline = deadlineFor(timeoutMs);
        if (session || frame) throw std::runtime_error("A capture is already in progress");
        failIfNeeded();
        constraintsDone = ready = stopped = false;
        failure.clear();
        width = height = 0;
        format = std::numeric_limits<uint32_t>::max();
        transform = WL_OUTPUT_TRANSFORM_NORMAL;
        session = ext_image_copy_capture_manager_v1_create_session(captureManager, source, 0);
        if (!session) throw std::runtime_error("Cannot create capture session");
        static const ext_image_copy_capture_session_v1_listener sessionListener{
            [](void* data, ext_image_copy_capture_session_v1*, uint32_t w, uint32_t h) {
                auto* self = static_cast<Impl*>(data); self->width = w; self->height = h;
            },
            [](void* data, ext_image_copy_capture_session_v1*, uint32_t value) {
                auto* self = static_cast<Impl*>(data);
                if (imageFormat(value) != QImage::Format_Invalid &&
                    (self->format == std::numeric_limits<uint32_t>::max() || value == WL_SHM_FORMAT_XRGB8888)) self->format = value;
            },
            [](void*, ext_image_copy_capture_session_v1*, wl_array*) {},
            [](void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {},
            [](void* data, ext_image_copy_capture_session_v1*) { static_cast<Impl*>(data)->constraintsDone = true; },
            [](void* data, ext_image_copy_capture_session_v1*) { static_cast<Impl*>(data)->stopped = true; }
        };
        ext_image_copy_capture_session_v1_add_listener(session, &sessionListener, this);
        try {
            waitUntil([&] { return constraintsDone; }, deadline);
            ensureBuffer();
            frame = ext_image_copy_capture_session_v1_create_frame(session);
            if (!frame) throw std::runtime_error("Cannot create capture frame");
            static const ext_image_copy_capture_frame_v1_listener frameListener{
                [](void* data, ext_image_copy_capture_frame_v1*, uint32_t value) { static_cast<Impl*>(data)->transform = value; },
                [](void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {},
                [](void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {},
                [](void* data, ext_image_copy_capture_frame_v1*) { static_cast<Impl*>(data)->ready = true; },
                [](void* data, ext_image_copy_capture_frame_v1*, uint32_t reason) {
                    static_cast<Impl*>(data)->failure = QStringLiteral("Capture failed (protocol reason %1)").arg(reason);
                }
            };
            ext_image_copy_capture_frame_v1_add_listener(frame, &frameListener, this);
            ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
            ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, width, height);
            ext_image_copy_capture_frame_v1_capture(frame);
            waitUntil([&] { return ready; }, deadline);
            if (transform > WL_OUTPUT_TRANSFORM_FLIPPED_270)
                throw std::runtime_error("Unknown capture buffer transform");
            const QImage view(static_cast<const uchar*>(pixels), bufferWidth, bufferHeight, bufferWidth * 4, imageFormat(bufferFormat));
            QTransform undoTransform;
            // The compositor reports the transform applied to these pixels;
            // invert it to recover the orientation the user saw.
            if (transform >= WL_OUTPUT_TRANSFORM_FLIPPED) undoTransform.scale(-1, 1);
            undoTransform.rotate(90 * (transform % 4));
            // Return the recorder's canonical format in owned storage. Converting
            // directly avoids keeping an RGB snapshot plus an RGBA copy alive
            // throughout OCR. Same-format conversion alone would borrow SHM.
            QImage result;
            if (transform == WL_OUTPUT_TRANSFORM_NORMAL)
                result = view.format() == QImage::Format_RGBA8888 ? view.copy() : view.convertToFormat(QImage::Format_RGBA8888);
            else
                result = view.transformed(undoTransform).convertToFormat(QImage::Format_RGBA8888);
            if (result.isNull()) throw std::runtime_error("Cannot copy captured pixels");
            finishSession();
            return result;
        } catch (...) {
            finishSession();
            throw;
        }
    }
};

WaylandCapture::WaylandCapture(QString outputName) : impl_(std::make_unique<Impl>()) {
    if (outputName.isEmpty()) throw std::runtime_error("Explicit output name is required");
    impl_->connect(3000);
    if (!impl_->shm || !impl_->sourceManager || !impl_->captureManager)
        throw std::runtime_error("Compositor lacks ext-image-copy-capture, output capture sources, or SHM");
    for (const auto& output : impl_->outputs)
        if (!output->removed && output->name == outputName) impl_->selected = output.get();
    if (!impl_->selected) throw std::runtime_error("Requested Wayland output was not found");
    impl_->source = ext_output_image_capture_source_manager_v1_create_source(impl_->sourceManager, impl_->selected->proxy);
    if (!impl_->source) throw std::runtime_error("Cannot create output capture source");
}

WaylandCapture::~WaylandCapture() = default;

QImage WaylandCapture::capture(int timeoutMs) { return impl_->capture(timeoutMs); }

QStringList WaylandCapture::outputs(int timeoutMs) {
    Impl connection;
    connection.connect(timeoutMs);
    QStringList names;
    for (const auto& output : connection.outputs)
        if (!output->removed) names.push_back(output->name);
    return names;
}

} // namespace replay
