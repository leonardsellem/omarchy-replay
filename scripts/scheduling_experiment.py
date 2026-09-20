#!/usr/bin/env python3
"""Measure a finite, offline activity/priority/restart indexing experiment."""
import argparse
import json
import os
from pathlib import Path
import signal
import tempfile
import time

from measure import measure


ROOT = Path(__file__).resolve().parents[1]


def height(value):
    try:
        number = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("height must be an integer") from error
    if number != 0 and not 256 <= number <= 8192:
        raise argparse.ArgumentTypeError("height must be zero or between 256 and 8192")
    return number


def save_private(path, report):
    """The requested report is outside the disposable source/dataset directory."""
    data = json.dumps(report, indent=2, allow_nan=False) + "\n"
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as file:
        file.write(data)


def interrupt(*_):
    raise KeyboardInterrupt


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--source-dir", type=Path, help="Existing read-only dataset with eight retained lossless originals")
    source.add_argument("--quick", action="store_true", help="12-second synthetic profile, 10-second maximum drain")
    parser.add_argument("--out", type=Path, required=True, help="New numeric report outside the disposable dataset")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/scheduling_experiment")
    parser.add_argument("--codec", choices=("webp", "h264", "hevc", "h264-vaapi", "hevc-vaapi"))
    parser.add_argument("--archive-first", action="store_true")
    parser.add_argument("--profile", choices=("adaptive", "always-active"), default="adaptive")
    parser.add_argument("--ocr-mode", choices=("full", "incremental", "regions"), default="incremental")
    parser.add_argument("--ocr-data-path", type=Path)
    parser.add_argument("--ocr-max-height", type=height, default=0)
    parser.add_argument("--temp-root", type=Path, default=ROOT / "runs", help="Parent for private disposable inputs/dataset")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.out = args.out.absolute()
    if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
        parser.error("--binary must name a built executable")
    if args.out.exists():
        parser.error("--out already exists; choose a new report path")
    if args.source_dir is not None:
        args.source_dir = args.source_dir.resolve()
        if not (args.source_dir / "index.sqlite").is_file():
            parser.error("--source-dir must contain index.sqlite")
    if args.ocr_data_path is not None:
        args.ocr_data_path = args.ocr_data_path.resolve()
        model = args.ocr_data_path / "eng.traineddata"
        if not model.is_file() or not os.access(model, os.R_OK) or model.stat().st_size == 0:
            parser.error("--ocr-data-path must contain readable, nonempty eng.traineddata")
    args.temp_root.mkdir(parents=True, exist_ok=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    codec = args.codec or ("webp" if args.quick or args.archive_first else "h264-vaapi")
    if args.archive_first and codec != "webp":
        parser.error("--archive-first requires lossless WebP")
    environment = os.environ.copy()
    environment.update({"QT_QPA_PLATFORM": "offscreen", "QT_QPA_PLATFORMTHEME": "",
                        "QT_STYLE_OVERRIDE": "Fusion", "OMP_THREAD_LIMIT": "1"})
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        environment.pop(name, None)

    # measure() owns a fresh process group and terminates every owned descendant
    # on timeout or interruption. Convert TERM to Python's cleanup path as well.
    previous_term = signal.signal(signal.SIGTERM, interrupt)
    report = {"report_version": 1, "completed": False, "interrupted": False,
              "configuration": {"quick": args.quick, "codec": codec, "ocr_mode": args.ocr_mode,
                                "archive_first": args.archive_first,
                                "profile": args.profile,
                                "ocr_max_height": args.ocr_max_height,
                                "explicit_model": args.ocr_data_path is not None},
              "limits": [
                  "Offline images and injected idle/zero-pressure/10%-CPU-utilization signals; no native capture, display, GPU allocation, or foreground responsiveness proof.",
                  "Full profile offers 24 observations every 5 seconds over 120 seconds. Active input advances through eight oldest-first originals; idle input repeats the last active image. The source sequence may cycle.",
                  "The always-active profile advances source images throughout capture and keeps active CPU policy during drain; it has no frozen-input or idle interval.",
                  "Active allowance is 10%, idle 40%, explicitly requested jobs 30%. These are cooperative OCR allowances, not whole-process CPU limits.",
                  "Phase codes: 0 initial active, 1 idle, 2 resumed active, 3 idle drain. Queue event codes: 0 periodic, 1 priority request, 2 before stop, 3 restarted, 4 capture end, 5 final.",
                  "A single priority request observes priority selection and subsequent oldest work; it does not exercise the three-priority starvation bound.",
                  "Retention is demonstrated only when restart.retention_check_nonempty is one. Temporary encoded-source hashes are compared across worker restart and excluded from this report.",
                  "The producer's reported CPU/profile clock starts after source preparation. External process-tree measurement includes setup, source validation/copying, restart checks, finalization and drain.",
                  "Queue lag is oldest pending age, not per-frame completion latency. Memory sampling may miss short-lived peaks and excludes GPU allocations.",
                  "Quick mode scales the profile to 12 seconds with 0.5-second arrivals, 2-second request, 4.5-second restart and at most 10 seconds drain; it is a lifecycle smoke check.",
              ]}
    returncode = 1
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix="scheduling-", dir=args.temp_root) as temporary:
            dataset = Path(temporary) / "dataset"
            command = [str(args.binary), "--dir", str(dataset), "--codec", codec,
                       "--ocr-mode", args.ocr_mode, "--profile", args.profile,
                       "--ocr-max-height", str(args.ocr_max_height)]
            if args.quick:
                command.append("--quick")
            else:
                command += ["--source-dir", str(args.source_dir)]
            if args.ocr_data_path:
                command += ["--ocr-data-path", str(args.ocr_data_path)]
            if args.archive_first:
                command.append("--archive-first")
            measured = measure(command, timeout=120 if args.quick else 300,
                               env=environment, sample_ms=100)
            # Raw subprocess diagnostics may contain Tesseract/source details.
            # They remain in neither this report nor the terminal output.
            for key in ("command", "stdout_tail", "stderr_tail"):
                measured.pop(key, None)
            report["measurement"] = measured
            result = measured.get("result")
            report["completed"] = bool(isinstance(result, dict) and result.get("completed") and not measured["timed_out"])
            report["interrupted"] = bool(isinstance(result, dict) and result.get("interrupted"))
            returncode = measured["returncode"] or int(measured["timed_out"] or not report["completed"])
        report["temporary_data_removed"] = not Path(temporary).exists()
    except KeyboardInterrupt:
        report["interrupted"] = True
        report["temporary_data_removed"] = "temporary" not in locals() or not Path(temporary).exists()
        returncode = 130
    except Exception:
        report["exception"] = True
        report["temporary_data_removed"] = "temporary" not in locals() or not Path(temporary).exists()
        returncode = 1
    finally:
        signal.signal(signal.SIGTERM, previous_term)
    report["wrapper_wall_seconds"] = time.monotonic() - started
    save_private(args.out, report)
    print(json.dumps({"report": str(args.out), "completed": report["completed"],
                      "interrupted": report["interrupted"], "returncode": returncode,
                      "temporary_data_removed": report.get("temporary_data_removed", False)}))
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())
