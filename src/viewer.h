#pragma once

#include <QString>
#include <memory>

class QWidget;

namespace replay {

// Browses the named local dataset and can persist bounded indexing requests.
// Explicit controls manage saved-history indexing independently of the window.
// Opening this widget alone starts no capture, remote fetch, or index worker.
// Factory is separate from the event loop for native keyboard-path tests.
std::unique_ptr<QWidget> createViewer(const QString& datasetDirectory);
int showViewer(const QString& datasetDirectory);

}  // namespace replay
