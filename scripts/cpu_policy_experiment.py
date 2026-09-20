#!/usr/bin/env python3
"""Finite sequential CPU-policy comparison using copied, previously saved WebP frames.

No capture or installed service. Every transient unit is stopped on exit. Output
contains numeric evidence only; private images/text stay under the ignored run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import sqlite3
import subprocess
import time
import uuid

from measure import process_sample


ROOT = Path(__file__).resolve().parents[1]
POLICIES = {"cooperative10": (10, 100), "cooperative30": (30, 100),
            "quota30": (0, 30), "cooperative40": (40, 100),
            "balanced30": (30, 60), "balanced40": (40, 60)}


def save(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def readonly(directory):
    connection = sqlite3.connect((directory / "index.sqlite").as_uri() + "?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    return connection


def source_file(directory, relative):
    candidate = directory / relative
    path = candidate.resolve()
    if Path(relative).is_absolute() or candidate.is_symlink() or not path.is_relative_to(directory / "media") or not path.is_file():
        raise RuntimeError("Source image must be a regular file within its dataset")
    return path


def prepare(source, output, per_window, window_count=2):
    with readonly(source) as connection:
        rows = connection.execute("SELECT id,timestamp_ms,last_timestamp_ms,observation_count,"
            "segment_id,frame_index,path,codec,width,height,ocr_state FROM frames ORDER BY timestamp_ms,id").fetchall()
        if len(rows) < per_window * window_count * 2 or any(row["ocr_state"] != "ready" for row in rows):
            raise RuntimeError("Use a fully indexed source with enough frames for nonoverlapping windows")
        selected = []
        for window in range(window_count):
            start = (2 * window + 1) * len(rows) // (2 * window_count)
            selected += rows[start:start + per_window]
        schema = connection.execute("SELECT name,sql FROM sqlite_master WHERE sql IS NOT NULL "
                                    "AND type IN ('table','index') ORDER BY type='index'").fetchall()
        metadata = connection.execute("SELECT * FROM metadata").fetchall()
        ids = [row["id"] for row in selected]
        placeholders = ",".join("?" for _ in ids)
        observations = connection.execute(
            f"SELECT * FROM observations WHERE frame_id IN ({placeholders}) ORDER BY id", ids).fetchall()
    seed = output / "seed"
    (seed / "media").mkdir(parents=True)
    (seed / "staging").mkdir()
    manifest = []
    with sqlite3.connect(seed / "index.sqlite") as database:
        database.execute("PRAGMA journal_mode=WAL")
        for table in schema:
            if not table["name"].startswith(("frame_text_", "sqlite_")):
                database.execute(table["sql"])
        database.executemany("INSERT INTO metadata VALUES(?,?)", [tuple(row) for row in metadata])
        database.execute("INSERT OR REPLACE INTO metadata VALUES('max_disk_bytes','1073741824')")
        database.execute("INSERT OR REPLACE INTO metadata VALUES('archive_first','1')")
        for row in selected:
            if row["codec"] != "webp" or row["segment_id"] is not None:
                raise RuntimeError("Only independently stored WebP frames are supported")
            original = source_file(source, row["path"])
            relative = f"media/{row['id']}.webp"
            shutil.copyfile(original, seed / relative)
            original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
            if hashlib.sha256((seed / relative).read_bytes()).hexdigest() != original_hash:
                raise RuntimeError("Source copy hash mismatch")
            values = dict(row)
            values.update(path=relative, source_path=relative, source_bytes=original.stat().st_size,
                          text="", ocr_state="pending", ocr_error="")
            database.execute(f"INSERT INTO frames ({','.join(values)}) VALUES "
                             f"({','.join('?' for _ in values)})", list(values.values()))
            manifest.append({"id":row["id"], "timestamp_ms":row["timestamp_ms"],
                             "width":row["width"], "height":row["height"],
                             "source_path":row["path"], "sha256":original_hash})
        database.executemany("INSERT INTO observations VALUES(?,?,?)", [tuple(row) for row in observations])
        database.execute("INSERT INTO index_schedule(id) VALUES(1)")
    return seed, manifest


def unit_status(unit):
    result = subprocess.run(["systemctl", "--user", "show", unit,
        "--property=MainPID,ExecMainPID,ExecMainStatus,ActiveState,SubState,Result,ControlGroup,CPUUsageNSec"],
        capture_output=True, text=True, timeout=5, check=True)
    return dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)


def counters(path):
    try:
        return {parts[0]: int(parts[1]) for line in path.read_text().splitlines()
                if len(parts := line.split()) == 2 and parts[1].isdigit()}
    except OSError:
        return {}


def optional_text(path):
    try:
        return path.read_text()
    except OSError:
        return None


def outputs(directory):
    with readonly(directory) as database:
        return [tuple(row) for row in database.execute("SELECT f.id,f.text,g.lines_json "
            "FROM frames f LEFT JOIN frame_ocr_geometry g ON g.frame_id=f.id ORDER BY f.id")]


def run_pass(policy, source_seed, destination, timeout):
    shutil.copytree(source_seed, destination)
    cooperative, quota = POLICIES[policy]
    unit = "replay-cpu-experiment-" + uuid.uuid4().hex + ".service"
    command = ["systemd-run", "--user", "--quiet", "--unit=" + unit,
               "--property=Type=exec", "--property=RemainAfterExit=yes",
               "--property=Slice=background.slice", "--property=CPUWeight=10",
               "--property=CPUAccounting=yes", "--property=MemoryAccounting=yes",
               "--property=CPUQuota=" + str(quota) + "%", "--property=CPUQuotaPeriodSec=100ms",
               "--property=Nice=10", "--property=RuntimeMaxSec=" + str(timeout),
               "--property=TimeoutStopSec=5", "--property=KillMode=control-group",
               "--property=StandardOutput=file:" + str(destination / "worker-stdout.json"),
               "--property=StandardError=file:" + str(destination / "worker-stderr.log"),
               "--setenv=QT_QPA_PLATFORM=offscreen", "--setenv=OMP_THREAD_LIMIT=1",
               str(ROOT / "build/replay"), "index", "--dir", str(destination),
               "--scheduler", "fixed", "--ocr-mode", "incremental",
               "--ocr-cpu-percent", str(cooperative), "--ocr-max-wall-ms", "60000",
               "--ocr-max-height", "0", "--ocr-data-path", "/usr/share/tessdata", "--ocr-cpu-ceiling-percent", "0", "--no-ocr-reuse"]
    started = time.monotonic()
    peak_pss, peak_rss, samples, last_tick = 0, 0, [], -1
    started_unit = False
    event_probe = subprocess.Popen([str(ROOT / "build/foreground_event_probe"), str(timeout)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        # The service is a child of systemd, NOT of the systemd-run wrapper.
        # Sample its actual PID/cgroup and stop its owned unit in finally.
        started_unit = True
        subprocess.run(command, capture_output=True, timeout=10, check=True)
        state = unit_status(unit)
        pid = int(state["MainPID"])
        if not state["ControlGroup"] or not state["ControlGroup"].endswith('/' + unit):
            raise RuntimeError("Cannot identify the owned worker cgroup")
        group = Path("/sys/fs/cgroup") / state["ControlGroup"].lstrip("/")
        group_settings = {name:(group / name).read_text().strip() if (group / name).exists() else None
                          for name in ("cpu.max", "cpu.weight", "cpuset.cpus.effective")}
        group_settings["process_cpu_affinity"] = sorted(os.sched_getaffinity(pid))
        if group_settings["cpu.max"] != f"{quota * 1000} 100000" or group_settings["cpu.weight"] != "10":
            raise RuntimeError("Kernel resource controls did not match the requested policy")
        ancestor_limits = []
        for ancestor in group.parents:
            if not ancestor.is_relative_to('/sys/fs/cgroup'):
                break
            ancestor_limits.append({"path":str(ancestor),
                "cpu_max":(ancestor / "cpu.max").read_text().strip() if (ancestor / "cpu.max").exists() else None,
                "cpu_weight":(ancestor / "cpu.weight").read_text().strip() if (ancestor / "cpu.weight").exists() else None})
        identity = process_sample(pid) if pid else None
        if not identity:
            raise RuntimeError("Worker exited before process identity could be sampled")
        next_progress = 30
        while True:
            elapsed = time.monotonic() - started
            if elapsed > timeout:
                raise TimeoutError("Finite indexing comparison exceeded its deadline")
            sample = process_sample(pid)
            if sample is None or sample["start"] != identity["start"]:
                break
            peak_pss = max(peak_pss, sample["pss"])
            peak_rss = max(peak_rss, sample["rss"])
            if int(elapsed) != last_tick:
                last_tick = int(elapsed)
                samples.append({"seconds":elapsed,"process":sample,
                    "group_cpu":counters(group / "cpu.stat"),
                    "global_cpu_pressure":Path("/proc/pressure/cpu").read_text(),
                    "group_cpu_pressure":optional_text(group / "cpu.pressure")})
            if elapsed >= next_progress:
                print(json.dumps({"event":"progress","policy":policy,"seconds":round(elapsed)}), flush=True)
                next_progress += 30
            time.sleep(0.1)
        # systemd retains CPUUsageNSec after the empty cgroup is removed.
        # Last sampled cgroup throttle counters remain lower bounds.
        state = unit_status(unit)
        stats = json.loads((destination / "index-run.json").read_text())
        if state["Result"] != "success" or state["ExecMainStatus"] != "0":
            raise RuntimeError("Index worker did not exit successfully")
        with readonly(source_seed) as seed_database:
            expected = seed_database.execute("SELECT COUNT(*) FROM frames").fetchone()[0]
        if (stats["processed"] != expected or stats["indexing"]["ready"] != expected
                or stats["failed_jobs"] or stats["canceled_jobs"] or stats["indexing"]["pending"]
                or stats["interrupted"] or stats["canceled"] or stats["ocr_budget_deadlines"]):
            raise RuntimeError("A comparison left failed or unfinished text")
        result = {"policy":policy,"cooperative_percent":cooperative,"whole_unit_quota_percent":quota,
                  "group_weight":10,"wall_seconds_including_launch":time.monotonic()-started,
                  "worker_stats":stats,"last_sampled_group_cpu":samples[-1]["group_cpu"] if samples else {},
                  "group_cpu_seconds":int(state["CPUUsageNSec"]) / 1e9,
                  "group_settings":group_settings,"ancestor_limits":ancestor_limits,
                  "peak_worker_pss_mib":peak_pss / 2**20,"peak_worker_rss_mib":peak_rss / 2**20,
                  "unit_result":state,"sample_interval_ms":100}
        event_probe.terminate()
        probe_stdout, _ = event_probe.communicate(timeout=3)
        if event_probe.returncode != 0:
            raise RuntimeError("Foreground event probe failed")
        result["foreground_event_probe"] = json.loads(probe_stdout)
        save(destination / "resource-samples.json",samples)
        save(destination / "measurement.json",result)
        return result
    finally:
        try:
            if event_probe.poll() is None:
                event_probe.kill()
            event_probe.communicate(timeout=3)
        finally:
            if started_unit:
                try:
                    subprocess.run(["systemctl", "--user", "stop", unit], capture_output=True, timeout=10)
                except subprocess.TimeoutExpired:
                    subprocess.run(["systemctl", "--user", "kill", "--kill-whom=all", "--signal=KILL", unit],
                                   capture_output=True, timeout=5)
                finally:
                    remaining = subprocess.run(["systemctl", "--user", "is-active", unit], capture_output=True, timeout=5)
                    if remaining.returncode not in (3, 4) or remaining.stdout.strip() not in (b"inactive", b"failed", b"unknown"):
                        raise RuntimeError("Could not positively verify owned experiment unit shutdown")
                    subprocess.run(["systemctl", "--user", "reset-failed", unit], capture_output=True, timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--frames-per-window", type=int, default=4)
    parser.add_argument("--window-count", type=int, default=2)
    parser.add_argument("--policies", nargs="+", choices=POLICIES, default=["cooperative10", "cooperative30", "quota30"])
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    def stop_requested(_signal, _frame):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, stop_requested)
    source, output = args.source_dir.resolve(), args.out.resolve()
    if not output.is_relative_to(ROOT / "runs") or output.exists():
        parser.error("--out must be a new directory under this project's ignored runs/")
    if not 1 <= args.window_count <= 8 or not 1 <= args.frames_per_window <= 12 or not 60 <= args.timeout <= 900:
        parser.error("Use 1–12 frames per window and a 60–900 second per-pass deadline")
    os.umask(0o077)
    output.mkdir(parents=True)
    report = {"completed":False,"source_dataset":str(source),"passes":[],
        "design":"Same copied images, model, original resolution and order; selected policies then reversed order.",
        "limits":["Selected saved-frame subset, not all-day or other-hardware proof.",
            "No active capture or video encoding. Qt foreground event-loop timing measures scheduling, not real input/paint/compositor latency.",
            "All policies share low-weight background cgroup and Nice=10; this does not measure priority's foreground benefit.",
            "Cooperative allowance covers OCR callbacks; kernel quota covers all worker CPU.",
            "Text/geometry equality is regression agreement, not independent OCR accuracy.",
            "Warm filesystem/model caches; no cache flushing. Sampling may miss memory peaks.",
            "Cgroup throttle counters are last-sampled lower bounds; systemd CPUUsageNSec is final unit CPU.",
            "CPU-seconds are work; elapsed time includes throttling/contention. No desktop-latency claim."]}
    save(output / "report.json",report)
    try:
        seed, manifest = prepare(source, output, args.frames_per_window, args.window_count)
        report["input_frames"] = manifest
        report["binary_sha256"] = hashlib.sha256((ROOT / "build/replay").read_bytes()).hexdigest()
        model = Path('/usr/share/tessdata/eng.traineddata')
        report["installed_english_model_sha256"] = hashlib.sha256(model.read_bytes()).hexdigest() if model.is_file() else None
        report["foreground_baseline_before"] = json.loads(subprocess.check_output(
            [str(ROOT / "build/foreground_event_probe"), "10"], text=True, timeout=15))
        order = args.policies + list(reversed(args.policies))
        report["execution_order"] = order
        baseline = None
        for index, policy in enumerate(order):
            print(json.dumps({"event":"begin","pass":index+1,"policy":policy,"frames":len(manifest)}),flush=True)
            destination = output / f"pass-{index+1}-{policy}"
            result = run_pass(policy, seed, destination, args.timeout)
            current = outputs(destination)
            if baseline is None:
                baseline = current
            result["text_and_geometry_equal_baseline"] = sum(a == b for a,b in zip(baseline,current))
            if len(current) != len(manifest) or result["text_and_geometry_equal_baseline"] != len(manifest):
                report["quality_disagreement"] = True
            report["passes"].append(result)
            save(output / "report.json",report)
            print(json.dumps({"event":"complete","pass":index+1,"policy":policy,
                "elapsed_seconds":result["worker_stats"]["elapsed_seconds"],
                "cpu_seconds":result["worker_stats"]["self_cpu_seconds"],
                "equal_frames":result["text_and_geometry_equal_baseline"]}),flush=True)
        report["foreground_baseline_after"] = json.loads(subprocess.check_output(
            [str(ROOT / "build/foreground_event_probe"), "10"], text=True, timeout=15))
        report["original_images_unchanged"] = all(hashlib.sha256(
            source_file(source, frame["source_path"]).read_bytes()).hexdigest() == frame["sha256"]
            for frame in manifest)
        report["binary_unchanged"] = hashlib.sha256((ROOT / "build/replay").read_bytes()).hexdigest() == report["binary_sha256"]
        report["installed_english_model_unchanged"] = (hashlib.sha256(model.read_bytes()).hexdigest() ==
            report["installed_english_model_sha256"]) if model.is_file() else None
        if not all(report.get(key) is True for key in
                   ("original_images_unchanged", "binary_unchanged", "installed_english_model_unchanged")):
            raise RuntimeError("Comparison inputs changed during the run")
        report["completed"] = True
    except (Exception, KeyboardInterrupt) as exception:
        report["failure_type"] = type(exception).__name__
        report["private_failure_detail"] = str(exception)
        print("Experiment stopped; private diagnostics remain in the local run.",flush=True)
    finally:
        save(output / "report.json",report)
    return 0 if report["completed"] and not report.get("quality_disagreement") else 1


if __name__ == "__main__":
    raise SystemExit(main())
