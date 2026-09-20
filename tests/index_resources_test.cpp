#include "index_resources.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <csignal>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Environment {
    QString unit = "oma-replay-index-41-0123456789abcdef0123456789abcdef.service";
    QString manager = "/user.slice/user-1000.slice/user@1000.service";
    QString group = manager + "/background.slice/" + unit;
    QMap<QString, QByteArray> files;
    QVariantMap properties;
    int reads = 0;
    Environment() {
        properties = {{"MainPID", 42}, {"ControlGroup", group}, {"WatchdogUSec", qulonglong(10000000)},
                      {"WatchdogSignal", int(SIGKILL)}, {"NotifyAccess", "main"}};
        files["/proc/self/cgroup"] = ("0::" + group + '\n').toUtf8();
        set(group, "cpu.max", "60000 100000"); set(group, "cpu.weight", "10"); set(group, "pids.max", "64");
    }
    void set(const QString &path, const QString &name, const QByteArray &value) {
        files["/sys/fs/cgroup" + path + '/' + name] = value;
    }
    replay::IndexResources::Sources sources() {
        return {42,
            [this](const QString &path) -> std::optional<QByteArray> {
                ++reads;
                const auto it = files.constFind(path);
                if (it == files.cend()) return std::nullopt;
                return it.value();
            },
            [this](const QString &name) { ++reads; return properties.value(name); }};
    }
    QJsonObject verify(double ceiling = 60) {
        return replay::IndexResources(ceiling, unit, {}, sources()).statsJSON();
    }
};

void disabledAndFallback() {
    Environment env;
    const auto disabled = replay::IndexResources(0, {}, {}, env.sources()).statsJSON();
    require(disabled["state"] == "disabled" && !disabled["enforced"].toBool() && env.reads == 0,
            "disabled verification performed native work");
    for (double value : {-1.0, .5, 100.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { replay::IndexResources invalid(value, {}, {}, {}); } catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "invalid CPU ceiling was accepted");
    }
    const auto missing = replay::IndexResources(60, {}, "No session bus", env.sources()).statsJSON();
    require(missing["state"] == "unavailable" && missing["reason"] == "No session bus" && env.reads == 0,
            "fallback lost its explicit launch reason");
    require(missing["effective_cpu_percent"].isNull() && missing["scope"].toString().contains("Direct"),
            "fallback claimed independent enforcement");
}

void verifiedLimitsAndAncestors() {
    Environment env;
    env.set(env.manager, "cpu.max", "40000 100000");
    const auto snapshot = env.verify();
    require(snapshot["enforced"].toBool() && snapshot["state"] == "enforced", "valid service was not verified");
    require(snapshot["service_unit"] == env.unit && snapshot["cgroup"] == env.group, "unit identity was lost");
    require(snapshot["scope_cpu_percent"].toDouble() == 60 && snapshot["effective_cpu_percent"].toDouble() == 40,
            "shared tighter ancestor was not reported");
    require(snapshot["ancestor_cpu_limits"].toArray().size() == 1 && snapshot["cpu_weight"].toInt() == 10 &&
            snapshot["tasks_max"].toInt() == 64 && snapshot["watchdog_seconds"].toInt() == 10,
            "resource receipt omitted verified limits");
    env.set(env.group, "cpu.max", "30000 100000");
    require(env.verify()["effective_cpu_percent"].toDouble() == 30, "tighter existing worker cap was rejected");
    require(!env.verify(20)["enforced"].toBool(), "looser existing cap claimed a tighter requested ceiling");
}

void identityAndLifecycleFailures() {
    for (const auto &name : {QString("MainPID"), QString("ControlGroup"), QString("WatchdogUSec"),
                            QString("WatchdogSignal"), QString("NotifyAccess")}) {
        Environment env; env.properties.remove(name);
        require(!env.verify()["enforced"].toBool(), "missing service property was accepted");
    }
    for (const auto &property : QList<QPair<QString, QVariant>>{{"MainPID", 43}, {"ControlGroup", "/other"},
            {"WatchdogUSec", qulonglong(20000000)}, {"WatchdogSignal", int(SIGABRT)}, {"NotifyAccess", "all"}}) {
        Environment env; env.properties[property.first] = property.second;
        require(!env.verify()["enforced"].toBool(), "mismatched service property was accepted");
    }
    for (const auto &unit : {QString("other.service"), QString("oma-replay-index-41-bad.scope"), QString("../../escape.service")}) {
        Environment env; env.unit = unit;
        require(!env.verify()["enforced"].toBool() && env.reads == 0, "foreign unit was queried");
    }
    Environment env;
    auto sources = env.sources();
    sources.serviceProperty = [](const QString &) -> QVariant { throw std::runtime_error("No session bus"); };
    require(replay::IndexResources(60, env.unit, {}, sources).statsJSON()["reason"] == "No session bus",
            "service verification transport error was lost");
}

void kernelFailures() {
    for (const auto &quota : {QByteArray("max 100000"), QByteArray("80000 100000"), QByteArray("bad 100000"),
                              QByteArray("60000 0"), QByteArray("0 100000")}) {
        Environment env; env.set(env.group, "cpu.max", quota);
        const auto snapshot = env.verify();
        require(!snapshot["enforced"].toBool() && snapshot["effective_cpu_percent"].isNull(), "unverified quota was claimed");
    }
    for (const auto &setting : QList<QPair<QString, QByteArray>>{{"cpu.weight", "100"}, {"pids.max", "max"},
                                                               {"pids.max", "65"}, {"pids.max", "0"}}) {
        Environment env; env.set(env.group, setting.first, setting.second);
        require(!env.verify()["enforced"].toBool(), "unverified task/weight limit was claimed");
    }
    for (const auto &file : {QString("cpu.max"), QString("cpu.weight"), QString("pids.max")}) {
        Environment env; env.files.remove("/sys/fs/cgroup" + env.group + '/' + file);
        require(!env.verify()["enforced"].toBool(), "missing required kernel data was accepted");
    }
    for (const auto &path : {QString("/../../outside"), QString("relative"), QString("/user.slice//bad"), QString("/a\nb")}) {
        Environment env; env.properties["ControlGroup"] = path;
        require(!env.verify()["enforced"].toBool(), "invalid cgroup path was followed");
    }
    Environment malformed;
    malformed.files["/proc/self/cgroup"] += "0::/duplicate\n";
    require(!malformed.verify()["enforced"].toBool(), "ambiguous membership was accepted");
    require(replay::processStartTicks(getpid()) > 0 && replay::processStartTicks(0) == 0,
            "process identity was not read from proc start ticks");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        disabledAndFallback(); verifiedLimitsAndAncestors(); identityAndLifecycleFailures(); kernelFailures();
        std::cout << "index service resource checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
