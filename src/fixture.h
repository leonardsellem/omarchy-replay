#pragma once

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSize>

namespace replay {

// Deterministic, synthetic screens. Duplicate frames are pixel-identical.
int fixtureFrameCount();
QImage fixtureFrame(int index, QSize size = QSize(1920, 1080), bool editing = false);
QJsonObject fixtureGroundTruth(int index, bool editing = false);
QJsonArray fixtureGroundTruth();

// QApplication must already exist. Runs its event loop, then returns its exit code.
// A non-positive duration displays one complete sequence; longer runs repeat it.
int showFixture(int intervalMs = 1000, QSize size = QSize(1920, 1080),
                int durationMs = 0, bool editing = false);

}  // namespace replay
