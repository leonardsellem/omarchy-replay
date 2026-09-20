#!/usr/bin/env python3
"""Compare finite synthetic recall runs; never captures the user's desktop."""
import argparse
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import time

from measure import measure

ROOT = Path(__file__).resolve().parents[1]


def tokens(text):
    return re.findall(r'[\w]+', text.casefold())


def verify(dataset, binary=ROOT / 'build/replay'):
    truth = json.loads((dataset / 'ground-truth.json').read_text())
    db = sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True)
    frame_text = dict(db.execute('SELECT id,text FROM frames'))
    columns = {row[1] for row in db.execute('PRAGMA table_info(frames)')}
    states = dict(db.execute('SELECT id,ocr_state FROM frames')) if 'ocr_state' in columns else {}
    missed, unindexed, checked = [], [], 0
    for frame in truth:
        expected = frame.get('tokens', frame.get('expected_tokens', []))
        actual = ' '.join(tokens(frame_text[frame['frame_id']]))
        for token in expected:
            checked += 1
            if states.get(frame['frame_id']) in ('pending', 'failed', 'disabled'):
                unindexed.append({'sample': frame['sample_index'], 'token': token,
                                  'frame_id': frame['frame_id'], 'state': states[frame['frame_id']]})
                continue
            if ' '.join(tokens(token)) not in actual:
                missed.append({'sample': frame['sample_index'], 'token': token, 'frame_id': frame['frame_id']})
    observation_count = db.execute('SELECT count(*) FROM observations').fetchone()[0]
    # Query core in a fresh process to exercise its actual literal-search behavior.
    searches = {}
    for query in ('Patrick XYZ-1042', 'XYZ-1043', 'Omakase cedar', 'Omakase juniper'):
        start = time.perf_counter()
        completed = subprocess.run([str(binary), 'search', '--dir', str(dataset), query],
                                   capture_output=True, text=True, timeout=10)
        result = json.loads(completed.stdout) if completed.returncode == 0 else []
        searches[query] = {'matches': len(result), 'cli_wall_ms': (time.perf_counter()-start)*1000,
                           'returncode': completed.returncode}
    db.close()
    return {'tokens_checked': checked, 'tokens_missing': missed, 'tokens_unindexed': unindexed, 'observations': observation_count,
            'truth_samples': len(truth), 'searches': searches,
            'scope': 'OCR of original synthetic pixels. Not encoded-image OCR or live-capture coverage.'}


def main():
    os.environ['OMP_THREAD_LIMIT'] = '1'
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True, help='New output directory')
    p.add_argument('--codecs', default='webp,h264,h264-vaapi,hevc-vaapi')
    p.add_argument('--size', default='1920x1080')
    p.add_argument('--frames', type=int, default=16)
    p.add_argument('--interval', type=float, default=2)
    p.add_argument('--realtime', action='store_true')
    p.add_argument('--static', action='store_true')
    p.add_argument('--no-ocr', action='store_true')
    p.add_argument('--binary', type=Path, default=ROOT / 'build/replay')
    p.add_argument('--ocr-mode', choices=['full', 'incremental'])
    p.add_argument('--ocr-cpu-percent', type=float)
    p.add_argument('--workload', choices=['mixed', 'editing'])
    p.add_argument('--indexing', choices=['sync', 'deferred'])
    p.add_argument('--pending-frames', type=int)
    p.add_argument('--pending-mib', type=float)
    p.add_argument('--drain-seconds', type=float, default=10)
    args = p.parse_args()
    args.binary = args.binary.resolve(strict=True)
    if args.ocr_cpu_percent is not None and not (args.ocr_cpu_percent == 0 or 1 <= args.ocr_cpu_percent <= 100):
        p.error('OCR CPU percent must be 0 or 1–100')
    if args.out.exists():
        p.error('Output exists; choose a fresh directory')
    if not 1 <= args.frames <= 1000 or not .25 <= args.interval <= 60:
        p.error('Frames must be 1–1000 and interval 0.25–60 seconds')
    match = re.fullmatch(r'(\d+)x(\d+)', args.size)
    if not match:
        p.error('Size must be WIDTHxHEIGHT')
    codecs = args.codecs.split(',')
    if any(c not in ('webp', 'h264', 'hevc', 'h264-vaapi', 'hevc-vaapi') for c in codecs):
        p.error('Unsupported codec')
    args.out.mkdir(parents=True)
    args.out = args.out.resolve()
    rows = []
    for codec in codecs:
        dataset = args.out / codec
        command = ['nice', '-n', '10', str(args.binary), 'demo', '--dir', str(dataset),
                   '--codec', codec, '--frames', str(args.frames), '--interval', str(args.interval),
                   '--width', match[1], '--height', match[2]]
        if args.ocr_mode is not None:
            command += ['--ocr-mode', args.ocr_mode]
        if args.ocr_cpu_percent is not None:
            command += ['--ocr-cpu-percent', str(args.ocr_cpu_percent)]
        if args.workload is not None:
            command += ['--workload', args.workload]
        if args.indexing is not None:
            command += ['--indexing', args.indexing, '--drain-seconds', str(args.drain_seconds)]
        if args.pending_frames is not None:
            command += ['--pending-frames', str(args.pending_frames)]
        if args.pending_mib is not None:
            command += ['--pending-mib', str(args.pending_mib)]
        for flag in ('realtime', 'static', 'no_ocr'):
            if getattr(args, flag):
                command.append('--' + flag.replace('_', '-'))
        print(f'Measuring {codec} at {args.size} ({"paced" if args.realtime else "burst"})', flush=True)
        report = measure(command, timeout=max(120, args.frames * args.interval * 2),
                         dataset=dataset, binary=args.binary)
        report['codec'] = codec
        if report['returncode'] == 0:
            report['final_directory_bytes'] = sum(f.stat().st_size for f in dataset.rglob('*') if f.is_file())
            if not args.no_ocr:
                report['recall'] = verify(dataset, args.binary)
        rows.append(report)
        (args.out / 'report.json').write_text(json.dumps(rows, indent=2)+'\n')
        result = report.get('result') or {}
        print(json.dumps({'codec': codec, 'exit': report['returncode'], 'pss_mib': report['peak_tree_pss_mib'],
                          'bytes': report.get('final_directory_bytes'), 'ocr_ms': result.get('ocr_wall_ms'),
                          'missed_tokens': len(report.get('recall', {}).get('tokens_missing', [])),
                          'unindexed_tokens': len(report.get('recall', {}).get('tokens_unindexed', []))}), flush=True)
    return int(any(row['returncode'] for row in rows))


if __name__ == '__main__':
    sys.exit(main())
