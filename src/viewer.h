#pragma once

#include "selection_ocr.h"

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <memory>

class QWidget;

namespace replay {

// Browses the named local dataset and can persist bounded indexing requests.
// Explicit controls manage saved-history indexing independently of the window.
// Opening this widget alone starts no capture, remote fetch, or index worker.
// Factory is separate from the event loop for native keyboard-path tests.
struct ViewerServiceHooks {
    std::function<QJsonObject()> recordingStatus;
    std::function<QJsonObject(const QString&, const QJsonObject&)> recordingControl;
    // Bounded, read-only display metadata. Injected in synthetic UI tests.
    std::function<QJsonArray()> displays;
    // Runs off the GUI thread; synthetic tests can control completion/cancellation.
    std::function<SelectionOcrResult(const QImage&, const std::shared_ptr<std::atomic_bool>&)> selectionOcr;
};
std::unique_ptr<QWidget> createViewer(const QString& datasetDirectory, ViewerServiceHooks services = {});
int showViewer(const QString& datasetDirectory, bool settings = false);

}  // namespace replay
