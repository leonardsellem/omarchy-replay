#pragma once

#include <QImage>
#include <QStringList>
#include <memory>
#include <stdexcept>

namespace replay {

class CaptureTimeout : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A single-threaded client of the current WAYLAND_DISPLAY. The caller must
// explicitly select one output. Cursor compositing is not requested; capture
// sources are monitor compositions, never off-screen windows.
class WaylandCapture {
public:
    explicit WaylandCapture(QString outputName);
    ~WaylandCapture();
    WaylandCapture(const WaylandCapture&) = delete;
    WaylandCapture& operator=(const WaylandCapture&) = delete;

    // Owns the returned pixels; later captures cannot overwrite them. The
    // connection and one SHM buffer persist. A fresh protocol session per sample
    // avoids the protocol's indefinite wait for damage after its first frame.
    QImage capture(int timeoutMs = 3000);
    static QStringList outputs(int timeoutMs = 3000);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace replay
