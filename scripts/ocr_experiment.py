#!/usr/bin/env python3
"""Sequential offline OCR comparisons; no capture and no recognized text in reports."""
import argparse
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import tempfile
import time


def memory_sample(pid):
    try:
        values = {}
        for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
            key, _, value = line.partition(":")
            if key in ("Rss", "Pss"):
                values[key.lower() + "_mib"] = int(value.split()[0]) / 1024
        return values
    except (OSError, ValueError, IndexError):
        return {}


def measured_run(command, timeout, sample_ms):
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", OMP_THREAD_LIMIT="1")
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               env=environment, start_new_session=True)
    selector = selectors.DefaultSelector()
    for pipe in (process.stdout, process.stderr):
        os.set_blocking(pipe.fileno(), False)
        selector.register(pipe, selectors.EVENT_READ)
    pending = bytearray()
    stderr_bytes = 0
    active_job = None
    peak = {"rss_mib": 0.0, "pss_mib": 0.0}
    job_peaks = {}
    result = None
    timed_out = False
    sample_count = 0
    next_sample = start

    def consume(data):
        nonlocal active_job, result
        pending.extend(data)
        if len(pending) > 4 * 1024 * 1024:
            raise RuntimeError("Experiment produced an oversized numeric response")
        while b"\n" in pending:
            line, _, rest = pending.partition(b"\n")
            pending[:] = rest
            message = json.loads(line)
            event = message.get("event")
            if event == "job_begin":
                active_job = message["job"]
            elif event == "job_end":
                active_job = None
            elif event == "result":
                result = message

    try:
        while process.poll() is None or selector.get_map():
            now = time.monotonic()
            if now - start > timeout:
                timed_out = True
                break
            if now >= next_sample and process.poll() is None:
                sample = memory_sample(process.pid)
                if sample:
                    sample_count += 1
                    for key, value in sample.items():
                        peak[key] = max(peak[key], value)
                    if active_job is not None:
                        current = job_peaks.setdefault(active_job, {"rss_mib": 0.0, "pss_mib": 0.0, "samples": 0})
                        current["samples"] += 1
                        for key, value in sample.items():
                            current[key] = max(current[key], value)
                next_sample = now + sample_ms / 1000
            for key, _ in selector.select(max(0, min(0.1, next_sample - time.monotonic()))):
                data = key.fileobj.read()
                if data:
                    if key.fileobj is process.stdout:
                        consume(data)
                    else:
                        # Deliberately discard library stderr: it can contain
                        # private filenames or recognizer diagnostics.
                        stderr_bytes += len(data)
                elif data == b"":
                    selector.unregister(key.fileobj)
            if process.poll() is not None:
                next_sample = time.monotonic() + 0.05
        if timed_out:
            raise TimeoutError("Offline candidate exceeded its finite deadline")
        process.wait()
        if process.returncode or result is None:
            raise RuntimeError(f"Offline candidate failed (exit {process.returncode}); private diagnostics suppressed")
        for job in result["jobs"]:
            job["sampled_process_peak"] = job_peaks.get(job["job"], {"rss_mib": None, "pss_mib": None, "samples": 0})
        return {"result": result, "process_wall_ms": (time.monotonic() - start) * 1000,
                "sampled_process_peak": peak, "memory_samples": sample_count,
                "sample_interval_ms": sample_ms, "discarded_stderr_bytes": stderr_bytes}
    finally:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            except ProcessLookupError:
                pass
        selector.close()
        process.stdout.close()
        process.stderr.close()


def save_report(path, report):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n")
    os.chmod(temporary, 0o600)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path(__file__).resolve().parents[1] / "build/ocr_experiment")
    parser.add_argument("--source", choices=("synthetic", "synthetic1080", "synthetic4k", "dense4k", "private"), default="synthetic")
    parser.add_argument("--source-dir", type=Path)
    parser.add_argument("--fast-tessdata", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("full", "incremental", "regions"), default="full")
    candidate_names = ("system-original", "fast-original", "system1440", "fast1440", "fast1080",
                       "system-incremental", "system-regions")
    parser.add_argument("--candidates", nargs="+", choices=candidate_names, default=list(candidate_names[:5]),
                        help="Selected candidates in execution order; a full baseline is always prepared first")
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--sample-ms", type=int, default=50)
    args = parser.parse_args()
    if args.source == "private" and not args.source_dir:
        parser.error("--source-dir is required for the private corpus")
    if not 10 <= args.sample_ms <= 1000 or args.timeout <= 0:
        parser.error("Use a positive timeout and sample interval between 10 and 1000ms")
    if len(set(args.candidates)) != len(args.candidates):
        parser.error("Candidate names must be unique; use a separate output for a repeated run")
    if not (args.fast_tessdata / "eng.traineddata").is_file():
        parser.error("The fast model directory must contain eng.traineddata")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"source": args.source, "completed": False, "candidates": {},
              "limits": ["Token agreement with the system baseline is not accuracy.",
                         "Synthetic exact-term truth and actual historical search are measured separately.",
                         "Fresh process per candidate; warm OCR engine is retained within its sequence.",
                         "Per-job memory samples cover input decode/render plus record; record timings exclude input decode/render.",
                         "Sampling can miss short memory peaks; PSS excludes GPU allocations; no compositor is involved.",
                         "CPU and wall times are unpaced offline measurements, not producer/worker backlog proof.",
                         "Model, page-cache and run-order effects remain; one sequential run is not a statistical benchmark."]}
    save_report(args.output, report)
    candidates = {"system-original": (None, 0), "system-incremental": (None, 0), "system-regions": (None, 0),
                  "fast-original": (args.fast_tessdata, 0),
                  "system1440": (None, 1440), "fast1440": (args.fast_tessdata, 1440),
                  "fast1080": (args.fast_tessdata, 1080)}
    run_order = ["system-original"] + [name for name in args.candidates if name != "system-original"]
    report["execution_order"] = run_order
    try:
        # This private directory briefly contains original pixels and recognized
        # text in SQLite. It is removed on success, failure and Ctrl+C.
        with tempfile.TemporaryDirectory(prefix="ocr-data-", dir=args.output.parent) as temporary:
            root = Path(temporary)
            baseline = root / "system-original"
            for name in run_order:
                model, height = candidates[name]
                mode = {"system-original": "full", "system-incremental": "incremental",
                        "system-regions": "regions"}.get(name, args.mode)
                command = [str(args.binary), "--source", args.source, "--dataset-dir", str(root / name),
                           "--mode", mode,
                           "--ocr-height", str(height)]
                if args.source_dir:
                    command.extend(("--source-dir", str(args.source_dir)))
                if model:
                    command.extend(("--tessdata", str(model)))
                if name != "system-original":
                    command.extend(("--baseline-dir", str(baseline)))
                print(f"Running {args.source} / {name}", flush=True)
                measured = measured_run(command, args.timeout, args.sample_ms)
                if name in args.candidates:
                    report["candidates"][name] = measured
                else:
                    report["baseline_preparation"] = measured
                save_report(args.output, report)
            report["completed"] = True
            save_report(args.output, report)
    except (Exception, KeyboardInterrupt) as exception:
        report["failure_type"] = type(exception).__name__
        save_report(args.output, report)
        print("Experiment stopped; completed numeric results were preserved. Private temporary data removed.", flush=True)
        return 1
    print("Offline matrix complete; numeric report saved and temporary OCR datasets removed.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
