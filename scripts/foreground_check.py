#!/usr/bin/env python3
"""Paired CPU-work probe with/without finite synthetic recording, not UI timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def probe(seconds):
    payload = b'controlled foreground workload\n' * 65536
    deadline = time.perf_counter() + seconds
    samples = []
    start = time.perf_counter()
    while time.perf_counter() < deadline:
        tick = time.perf_counter()
        hashlib.sha256(payload).digest()
        samples.append((time.perf_counter() - tick) * 1000)
    elapsed = time.perf_counter() - start
    ordered = sorted(samples)
    return {'iterations_per_second': len(samples) / elapsed,
            'p95_unit_ms': ordered[int((len(ordered)-1)*.95)],
            'p99_unit_ms': ordered[int((len(ordered)-1)*.99)],
            'elapsed_seconds': elapsed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--pairs', type=int, default=3)
    parser.add_argument('--codec', default='h264-vaapi')
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/replay')
    parser.add_argument('--ocr-mode', choices=['full', 'incremental', 'regions'])
    parser.add_argument('--ocr-cpu-percent', type=float)
    parser.add_argument('--workload', choices=['mixed', 'editing'])
    parser.add_argument('--indexing', choices=['sync', 'deferred'])
    parser.add_argument('--archive-first', action='store_true')
    parser.add_argument('--scheduler', choices=['fixed', 'adaptive'], default='fixed')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--interval', type=float, default=2)
    parser.add_argument('--same-cpu', action='store_true', help='Constrain only this probe and recorder to one allowed CPU')
    args = parser.parse_args()
    args.binary = args.binary.resolve(strict=True)
    if args.ocr_cpu_percent is not None and not (args.ocr_cpu_percent == 0 or 1 <= args.ocr_cpu_percent <= 100):
        parser.error('OCR CPU percent must be 0 or 1–100')
    if args.out.exists() or not 2 <= args.seconds <= 30 or not 1 <= args.pairs <= 5:
        parser.error('Use a fresh output directory, seconds2–30, pairs1–5')
    if not 320 <= args.width <= 8192 or not 240 <= args.height <= 8192 or not .1 <= args.interval <= 30:
        parser.error('Invalid fixture dimensions or interval')
    if args.scheduler == 'adaptive' and args.indexing != 'deferred':
        parser.error('Adaptive scheduling requires --indexing deferred')
    if args.archive_first and (args.codec != 'webp' or args.indexing != 'deferred'):
        parser.error('Archive-first requires --codec webp --indexing deferred')
    args.out.mkdir(parents=True)
    args.out = args.out.resolve()
    os.environ['OMP_THREAD_LIMIT'] = '1'
    affinity = os.sched_getaffinity(0)
    if args.same_cpu:
        os.sched_setaffinity(0, {min(affinity)})
    trials = []
    try:
        for pair in range(args.pairs):
            for with_recorder in ([False, True] if pair % 2 == 0 else [True, False]):
                proc = None
                try:
                    if with_recorder:
                        command = ['nice', '-n', '10', str(args.binary), 'demo',
                                                 '--dir', str(args.out / f'pair-{pair}'), '--codec', args.codec,
                                                 '--realtime', '--interval', str(args.interval), '--frames', '10000',
                                                 '--width', str(args.width), '--height', str(args.height)]
                        if args.scheduler == 'adaptive':
                            # This probe exercises the active-user allowance;
                            # no idle boost should begin within its short run.
                            command += ['--scheduler', 'adaptive', '--idle-seconds', '3600',
                                        '--ocr-max-wall-ms', '60000', '--pending-frames', '0']
                        if args.ocr_mode is not None:
                            command += ['--ocr-mode', args.ocr_mode]
                        if args.ocr_cpu_percent is not None:
                            command += ['--ocr-cpu-percent', str(args.ocr_cpu_percent)]
                        if args.workload is not None:
                            command += ['--workload', args.workload]
                        if args.indexing is not None:
                            command += ['--indexing', args.indexing, '--drain-seconds', '0']
                        if args.archive_first:
                            command.append('--archive-first')
                        proc = subprocess.Popen(command,
                                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                                text=True, start_new_session=True)
                    row = probe(args.seconds)
                    row.update(pair=pair, with_recorder=with_recorder)
                    if proc:
                        proc.send_signal(signal.SIGINT)
                        stdout, stderr = proc.communicate(timeout=40)
                        if proc.returncode != 0:
                            raise RuntimeError(f'Recorder failed: {stderr[-1500:]}')
                        row['recording'] = json.loads(stdout)
                    trials.append(row)
                    print(json.dumps(row), flush=True)
                finally:
                    if proc:
                        try:
                            os.killpg(proc.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        proc.wait(timeout=3)
        pairs = []
        for pair in range(args.pairs):
            baseline = next(t for t in trials if t['pair'] == pair and not t['with_recorder'])
            active = next(t for t in trials if t['pair'] == pair and t['with_recorder'])
            pairs.append({'pair': pair,
                          'throughput_change_percent': 100*(active['iterations_per_second']/baseline['iterations_per_second']-1),
                          'p95_latency_change_percent': 100*(active['p95_unit_ms']/baseline['p95_unit_ms']-1),
                          'p99_latency_change_percent': 100*(active['p99_unit_ms']/baseline['p99_unit_ms']-1)})
        report = {'trials': trials, 'pairs': pairs,
                  'median_throughput_change_percent': statistics.median(p['throughput_change_percent'] for p in pairs),
                  'same_cpu': args.same_cpu, 'recorder_nice': 10,
                  'binary': str(args.binary), 'ocr_mode': args.ocr_mode,
                  'ocr_cpu_percent': args.ocr_cpu_percent,
                  'workload': args.workload,
                  'indexing': args.indexing,
                  'archive_first': args.archive_first,
                  'scheduler': args.scheduler, 'width': args.width, 'height': args.height,
                  'interval': args.interval,
                  'scope': 'Short CPU-bound synthetic foreground probe, alternating order. Not browser/build/video tests or compositor/GPU frame timing. No general smoothness claim.'}
        (args.out / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    finally:
        os.sched_setaffinity(0, affinity)


if __name__ == '__main__':
    main()
