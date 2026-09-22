#!/usr/bin/env python3
"""Run the synthetic WebP effort comparison serially, outside the recorder."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time


PRESETS = {0: (0, 0), 1: (1, 20), 3: (3, 30), 5: (4, 50), 6: (4, 75), 8: (5, 90)}


def memory_kib(pid):
    try:
        fields = {}
        for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
            key, _, value = line.partition(":")
            if key in ("Pss", "Rss"):
                fields[key] = int(value.split()[0])
        return fields if "Pss" in fields and "Rss" in fields else None
    except (OSError, ValueError, IndexError):
        return None


def measure(command, timeout, stdout_path):
    peak_pss = peak_rss = None
    samples = 0
    env = {**os.environ, "QT_QPA_PLATFORM": "offscreen"}
    started = time.monotonic()
    with stdout_path.open("x") as stdout, stdout_path.with_suffix(".stderr").open("x") as stderr:
        process = subprocess.Popen(["nice", "-n", "15", *command], stdout=stdout, stderr=stderr, env=env)
        try:
            while process.poll() is None:
                memory = memory_kib(process.pid)
                if memory is not None:
                    samples += 1
                    peak_pss = max(peak_pss or 0, memory["Pss"])
                    peak_rss = max(peak_rss or 0, memory["Rss"])
                if time.monotonic() - started > timeout:
                    raise TimeoutError(f"Invocation exceeded {timeout}s: {command}")
                time.sleep(0.02)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
    if process.returncode:
        raise RuntimeError(f"Invocation exited {process.returncode}; inspect {stdout_path.with_suffix('.stderr')}")
    result = json.loads(stdout_path.read_text())
    result.update(sampled_peak_pss_kib=peak_pss, sampled_peak_rss_kib=peak_rss,
                  memory_samples=samples, invocation_wall_ms=(time.monotonic() - started) * 1000)
    if not result.get("all_exact"):
        raise RuntimeError(f"Pixel verification failed: {stdout_path}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory for this pass")
    parser.add_argument("--levels", default="0,1,3,5,6,8")
    parser.add_argument("--fixtures", help="Comma-separated fixture stems; default: all")
    parser.add_argument("--mode", choices=("direct", "reencode"), default="direct")
    parser.add_argument("--hint", choices=("default", "graph"), default="default")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    levels = [int(value) for value in args.levels.split(",")]
    if not levels or any(level not in PRESETS for level in levels):
        parser.error("Unsupported lossless level")
    if not 1 <= args.repeats <= 5 or not 1 <= args.timeout <= 120:
        parser.error("Use 1..5 repeats and 1..120 seconds per invocation")
    manifest = json.loads((args.corpus / "manifest.json").read_text())
    fixtures = manifest["fixtures"]
    if args.fixtures:
        names = set(args.fixtures.split(","))
        fixtures = [item for item in fixtures if Path(item["file"]).stem in names]
        if {Path(item["file"]).stem for item in fixtures} != names:
            parser.error("Unknown fixture name")
    if not fixtures:
        parser.error("Empty corpus")
    args.output.mkdir(parents=True, exist_ok=False)
    with (args.output / "results.jsonl").open("x") as results:
        for repeat in range(args.repeats):
            for fixture_index, fixture in enumerate(fixtures):
                # Rotate order so the same setting does not always run first.
                offset = (repeat + fixture_index) % len(levels)
                for level in levels[offset:] + levels[:offset]:
                    stem = Path(fixture["file"]).stem
                    name = f"{stem}-z{level}-r{repeat}"
                    method, quality = PRESETS[level]
                    command = [str(args.binary.resolve()), "--input", str((args.corpus / fixture["file"]).resolve()),
                               "--output", str((args.output / f"{name}.webp").resolve()),
                               "--method", str(method), "--quality", str(quality),
                               "--mode", args.mode, "--hint", args.hint, "--repeats", "1"]
                    result = measure(command, args.timeout, args.output / f"{name}.json")
                    result.update(fixture=stem, level=level, repeat=repeat)
                    if result["input_rgba_sha256"] != fixture["rgba_sha256"]:
                        raise RuntimeError(f"Fixture hash changed: {stem}")
                    results.write(json.dumps(result) + "\n")
                    results.flush()
                    iteration = result["iterations"][0]
                    print(f"{name}: {result['output_bytes']} bytes; "
                          f"{iteration['encode_cpu_ms']:.1f} ms encode CPU; exact", flush=True)


if __name__ == "__main__":
    main()
