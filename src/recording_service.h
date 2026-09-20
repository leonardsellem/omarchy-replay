#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace replay {

// Local, bounded calls. Status never starts recording or a coordinator.
QJsonObject recordingServiceStatus();
QJsonObject controlRecordingService(const QString &action, const QJsonObject &arguments = {});

// One coordinator for the XDG shared history. Synthetic mode is an explicit
// test command, never a saved setting or a production fallback.
int runRecordingService(const std::function<bool()> &stopRequested, bool synthetic = false,
                        const QString &syntheticEnvironment = {});
int recordingCommand(const QStringList &arguments, const std::function<bool()> &stopRequested);

} // namespace replay
