#pragma once

#include <QImage>
#include <QString>
#include <atomic>
#include <memory>

namespace replay {

struct SelectionOcrResult {
    QString text;
    QString error;
    bool cancelled = false;
};

// Call off the GUI thread. Recognizes only these pixels, without archive access
// or clipboard writes. Each request owns and reaps its short-lived OCR process.
SelectionOcrResult recognizeSelection(const QImage &crop, const std::shared_ptr<std::atomic_bool> &cancel);

} // namespace replay
