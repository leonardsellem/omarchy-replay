#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <vector>

// A foreground-priority event-loop scheduling probe. It does not measure real
// input, painting, or compositor latency and never opens a window.
namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
double quantile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[size_t((values.size() - 1) * fraction)];
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool valid = false;
    const int duration = app.arguments().value(1).toInt(&valid);
    if (!valid || duration < 1 || duration > 900) return 2;
    std::signal(SIGTERM, stop); std::signal(SIGINT, stop);
    QElapsedTimer clock; clock.start();
    QTimer timer; timer.setTimerType(Qt::PreciseTimer); timer.setInterval(16);
    std::vector<double> gaps, dispatch;
    qint64 previous = 0;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        const qint64 now = clock.nsecsElapsed();
        if (previous) gaps.push_back(std::max(0.0, (now - previous) / 1e6 - 16.0));
        previous = now;
        QMetaObject::invokeMethod(&app, [&, now] { dispatch.push_back((clock.nsecsElapsed() - now) / 1e6); }, Qt::QueuedConnection);
        if (stopped || clock.elapsed() >= duration * 1000) app.quit();
    });
    timer.start(); app.exec();
    const QJsonObject result{{"elapsed_seconds", clock.elapsed() / 1000.0},
        {"samples", qint64(gaps.size())}, {"timer_lateness_p95_ms", quantile(gaps, .95)},
        {"timer_lateness_p99_ms", quantile(gaps, .99)}, {"timer_lateness_max_ms", quantile(gaps, 1)},
        {"queued_dispatch_p99_ms", quantile(dispatch, .99)},
        {"scope", "Foreground-priority Qt event loop; no window, input, paint or compositor timing"}};
    const auto bytes = QJsonDocument(result).toJson();
    std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
}
