#!/usr/bin/env python3
"""Exercise capture against synthetic pixels in a private nested compositor.

Hyprland's Wayland backend briefly opens its own window on the current desktop.
We disable that output, verify only REPLAY-TEST remains, then start the fixture.
Every capture targets that private output; the host desktop is never captured.
All compositor configuration, IPC, D-Bus and XDG state are temporary. No packages,
services or user settings are changed. Run from the repository root after build.
"""

import argparse
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time


def stop(process):
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        pass
    finally:
        # The group can outlive dbus-run-session, even after its leader exits.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=3)


def run(args, env, timeout=5):
    process = subprocess.Popen(args, env=env, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
        if process.returncode:
            raise RuntimeError(f"{args[0]} exited {process.returncode}: {stderr[-2000:]}")
        return stdout
    finally:
        stop(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/replay")
    parser.add_argument("--dir", required=True, help="New result directory")
    parser.add_argument("--backend", choices=["native", "grim"], default="native")
    parser.add_argument("--codec", choices=["webp", "h264", "hevc", "h264-vaapi", "hevc-vaapi"], default="webp")
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--interval", type=float, default=1)
    parser.add_argument("--fixture-interval", type=float, default=1)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--transform", type=int, choices=range(8), default=0)
    parser.add_argument("--capture-only", action="store_true")
    parser.add_argument("--no-ocr", action="store_true")
    parser.add_argument("--ocr-mode", choices=["full", "incremental"])
    parser.add_argument("--ocr-cpu-percent", type=float)
    parser.add_argument("--workload", choices=["mixed", "editing"])
    parser.add_argument("--indexing", choices=["sync", "deferred"])
    parser.add_argument("--pending-frames", type=int)
    parser.add_argument("--pending-mib", type=float)
    parser.add_argument("--drain-seconds", type=float, default=10)
    parser.add_argument("--measure", action="store_true", help="Sample only the recorder process tree; excludes the private compositor/fixture")
    options = parser.parse_args()
    if options.ocr_cpu_percent is not None and not (options.ocr_cpu_percent == 0 or 1 <= options.ocr_cpu_percent <= 100):
        parser.error("OCR CPU percent must be 0 or 1–100")
    if not 0 <= options.drain_seconds <= 60:
        parser.error("drain seconds must be 0–60")
    if not 1 <= options.duration <= 60 or not .25 <= options.interval <= 60:
        parser.error("duration must be 1–60 seconds; interval must be 0.25–60 seconds")
    if not .25 <= options.fixture_interval <= 60:
        parser.error("fixture interval must be 0.25–60 seconds")
    if not 320 <= options.width <= 3840 or not 240 <= options.height <= 2160:
        parser.error("synthetic headless dimensions must be 320×240 through 3840×2160")
    binary = str(Path(options.binary).resolve(strict=True))
    result_dir = Path(options.dir).resolve()
    if result_dir.exists():
        parser.error("result directory already exists")
    for tool in ["Hyprland", "hyprctl", "dbus-run-session"]:
        if not shutil.which(tool):
            parser.error(f"{tool} is required; this script does not install it")
    if not os.environ.get("WAYLAND_DISPLAY") or not os.environ.get("XDG_RUNTIME_DIR"):
        parser.error("a parent Wayland session is required for the nested rendering backend")
    parent = Path(os.environ["WAYLAND_DISPLAY"])
    if not parent.is_absolute():
        parent = Path(os.environ["XDG_RUNTIME_DIR"]) / parent
    if not parent.is_socket():
        parser.error("parent WAYLAND_DISPLAY does not identify a socket")
    result_dir.mkdir(parents=True, mode=0o700)
    compositor = fixture = None
    report = {"data_origin": "synthetic-private-wayland-output", "host_screen_captured": False,
              "measurement_scope": "Nested compositor functional check; not a foreground-impact benchmark",
              "passed": False}

    # Short path matters: Hyprland's signature consumes most sockaddr_un space.
    with tempfile.TemporaryDirectory(prefix="rp-") as temporary:
        base = Path(temporary)
        for name in ["config", "data", "cache", "state"]:
            (base / name).mkdir(mode=0o700)
        config = base / "hyprland.conf"
        config.write_text("""monitor = , 1280x720@30, auto, 1
misc {
 disable_hyprland_logo = true
 disable_splash_rendering = true
 force_default_wallpaper = 0
}
animations {
 enabled = false
}
decoration {
 blur {
  enabled = false
 }
 shadow {
  enabled = false
 }
}
xwayland {
 enabled = false
}
""")
        env = os.environ.copy()
        env.update(WAYLAND_DISPLAY=str(parent), XDG_RUNTIME_DIR=str(base),
                   XDG_CONFIG_HOME=str(base / "config"), XDG_DATA_HOME=str(base / "data"),
                   XDG_CACHE_HOME=str(base / "cache"), XDG_STATE_HOME=str(base / "state"),
                   LIBSEAT_BACKEND="replay-disabled", AQ_DRM_DEVICES="/dev/null",
                   HYPRLAND_NO_SD_VARS="1", HYPRLAND_NO_SD_NOTIFY="1",
                   HYPRLAND_NO_CRASHREPORTER="1", QT_QPA_PLATFORM="wayland",
                   QT_WAYLAND_DISABLE_WINDOWDECORATION="1", OMP_THREAD_LIMIT="1")
        for key in ["HYPRLAND_INSTANCE_SIGNATURE", "NOTIFY_SOCKET", "WAYLAND_SOCKET", "DISPLAY", "DBUS_SESSION_BUS_ADDRESS"]:
            env.pop(key, None)
        try:
            with (base / "compositor.log").open("w") as log:
                compositor = subprocess.Popen(
                    ["dbus-run-session", "--", "Hyprland", "--config", str(config)],
                    env=env, stdout=log, stderr=log, start_new_session=True)
                startup_deadline = time.monotonic() + 10
                while time.monotonic() < startup_deadline:
                    sockets = list(base.glob("hypr/*/.socket.sock"))
                    displays = [p for p in base.glob("wayland-*") if p.is_socket()]
                    if sockets and displays:
                        break
                    if compositor.poll() is not None:
                        raise RuntimeError("Private compositor exited during startup")
                    time.sleep(.05)
                else:
                    raise RuntimeError("Private compositor startup exceeded 10 seconds")
                env["HYPRLAND_INSTANCE_SIGNATURE"] = sockets[0].parent.name
                env["WAYLAND_DISPLAY"] = str(displays[0])
                run(["hyprctl", "output", "create", "headless", "REPLAY-TEST"], env)
                run(["hyprctl", "keyword", "monitor",
                     f"REPLAY-TEST,{options.width}x{options.height}@30,0x0,1,transform,{options.transform}"], env)
                run(["hyprctl", "keyword", "monitor", "WAYLAND-1,disable"], env)
                ready_deadline = time.monotonic() + 5
                while time.monotonic() < ready_deadline:
                    monitors = json.loads(run(["hyprctl", "-j", "monitors"], env))
                    if (len(monitors) == 1 and monitors[0]["name"] == "REPLAY-TEST"
                            and monitors[0]["width"] == options.width
                            and monitors[0]["height"] == options.height
                            and monitors[0]["transform"] == options.transform):
                        break
                    time.sleep(.05)
                else:
                    raise RuntimeError("Private output isolation/dimensions could not be verified")
                # Hyprland startup warnings have animated expiry bars; remove
                # only these private test notifications before static checks.
                run(["hyprctl", "dismissnotify", "-1"], env)
                report["output"] = {key: monitors[0][key] for key in ["name", "width", "height", "scale", "transform"]}
                fixture_width, fixture_height = options.width, options.height
                if options.transform % 2:
                    fixture_width, fixture_height = fixture_height, fixture_width
                with (base / "fixture.log").open("w") as fixture_log:
                    fixture_command = [binary, "fixture", "--interval", str(options.fixture_interval),
                         "--duration", str(options.duration + 8), "--width", str(fixture_width),
                         "--height", str(fixture_height)]
                    if options.workload is not None:
                        fixture_command += ["--workload", options.workload]
                    fixture = subprocess.Popen(fixture_command, env=env, stdout=fixture_log,
                        stderr=fixture_log, start_new_session=True)
                    fixture_deadline = time.monotonic() + 5
                    while time.monotonic() < fixture_deadline:
                        clients = json.loads(run(["hyprctl", "-j", "clients"], env))
                        if any(c.get("pid") == fixture.pid and c.get("mapped") for c in clients):
                            break
                        if fixture.poll() is not None:
                            raise RuntimeError("Synthetic fixture exited before mapping")
                        time.sleep(.05)
                    else:
                        raise RuntimeError("Synthetic fixture did not map")
                    time.sleep(.15)
                    command = [binary, "record", "--dir", str(result_dir / "dataset"),
                               "--output", "REPLAY-TEST", "--backend", options.backend,
                               "--codec", options.codec, "--duration", str(options.duration),
                               "--interval", str(options.interval), "--max-mib", "256"]
                    if options.capture_only:
                        command.append("--capture-only")
                    if options.no_ocr:
                        command.append("--no-ocr")
                    if options.ocr_mode is not None:
                        command += ["--ocr-mode", options.ocr_mode]
                    if options.ocr_cpu_percent is not None:
                        command += ["--ocr-cpu-percent", str(options.ocr_cpu_percent)]
                    if options.indexing is not None:
                        command += ["--indexing", options.indexing, "--drain-seconds", str(options.drain_seconds)]
                    if options.pending_frames is not None:
                        command += ["--pending-frames", str(options.pending_frames)]
                    if options.pending_mib is not None:
                        command += ["--pending-mib", str(options.pending_mib)]
                    report["command"] = command
                    if options.measure:
                        from measure import measure
                        measured = measure(["nice", "-n", "10", *command], timeout=options.duration + options.drain_seconds + 40,
                                           env=env, dataset=result_dir / "dataset", binary=binary)
                        report["measurement"] = measured
                        if measured["returncode"] != 0 or measured["result"] is None:
                            raise RuntimeError("Measured recorder failed: " + measured["stderr_tail"])
                        report["recording"] = measured["result"]
                    else:
                        report["recording"] = json.loads(run(command, env, timeout=options.duration + options.drain_seconds + 15))
                    if report["recording"].get("capture_timeouts", 0):
                        raise RuntimeError("Capture timeouts occurred; inspect result JSON")
                    observed_slots = report["recording"]["samples_attempted"] + report["recording"]["missed_schedule_slots"]
                    if observed_slots > math.ceil(options.duration / options.interval):
                        raise RuntimeError("Recorder counted schedule slots beyond the requested duration")
                    if not options.capture_only:
                        frames = json.loads(run([binary, "list", "--dir", str(result_dir / "dataset")], env))
                        if not frames:
                            raise RuntimeError("No recorded frames")
                        report["frame_count"] = len(frames)
                        if options.indexing == "deferred":
                            report["indexing_at_return"] = json.loads(run([binary, "status", "--dir", str(result_dir / "dataset")], env))
                        run([binary, "extract", "--dir", str(result_dir / "dataset"),
                             "--id", str(frames[0]["id"]), "--out", str(result_dir / "first-frame.png")], env)
                        if not options.no_ocr:
                            hits = json.loads(run([binary, "search", "Patrick", "invoice",
                                                   "--dir", str(result_dir / "dataset")], env))
                            report["patrick_invoice_hits"] = len(hits)
                            if not hits and not report.get("indexing_at_return", {}).get("pending", 0):
                                raise RuntimeError("Synthetic Patrick/invoice screen was not searchable")
            report["passed"] = True
        except Exception as error:
            report["passed"] = False
            report["error"] = str(error)
            raise
        finally:
            try:
                stop(fixture)
            finally:
                stop(compositor)
            for name in ["compositor.log", "fixture.log"]:
                if (base / name).exists():
                    with (base / name).open("rb") as log:
                        log.seek(max(0, log.seek(0, 2) - 32768))
                        (result_dir / name).write_bytes(log.read(32768))
            (result_dir / "check.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    os.umask(0o077)
    main()
