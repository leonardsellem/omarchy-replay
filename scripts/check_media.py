#!/usr/bin/env python3
"""Decode retained synthetic frames and check their text after media compression."""
import argparse
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def normalized(text):
    return ' '.join(re.findall(r'[\w]+', text.casefold()))


def check(dataset):
    truth = json.loads((dataset / 'ground-truth.json').read_text())
    expected = {}
    for entry in truth:
        expected.setdefault(entry['frame_id'], set()).update(entry['tokens'])
    db = sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True)
    original = dict(db.execute('SELECT id,text FROM frames'))
    db.close()
    output = dataset.parent / (dataset.name + '-decoded-check')
    output.mkdir(exist_ok=False)
    rows = []
    for frame_id, tokens in expected.items():
        image = output / f'{frame_id}.png'
        start = time.perf_counter()
        subprocess.run([str(ROOT / 'build/replay'), 'extract', '--dir', str(dataset), '--id', str(frame_id),
                        '--out', str(image)], capture_output=True, check=True, timeout=35)
        seek_ms = (time.perf_counter() - start) * 1000
        proc = subprocess.run(['tesseract', str(image), 'stdout', '-l', 'eng', '--psm', '3'],
                              capture_output=True, text=True, check=True, timeout=15)
        decoded = normalized(proc.stdout)
        source = normalized(original[frame_id])
        rows.append({'frame_id': frame_id, 'cli_decode_ms': seek_ms, 'tokens': sorted(tokens),
                     'original_missing': [t for t in sorted(tokens) if normalized(t) not in source],
                     'decoded_missing': [t for t in sorted(tokens) if normalized(t) not in decoded]})
    report = {'dataset': str(dataset), 'frames': rows,
              'scope': 'Same small synthetic token fixture after decode; no claim of universal legibility or equal perceptual quality. Decode time includes fresh CLI/decoder startup.'}
    (output / 'quality.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    os.environ['OMP_THREAD_LIMIT'] = '1'
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('datasets', nargs='+', type=Path)
    args = p.parse_args()
    for dataset in args.datasets:
        result = check(dataset.resolve())
        frames = result['frames']
        print(json.dumps({'dataset': str(dataset), 'frames': len(frames),
                          'decoded_missing': [r for r in frames if r['decoded_missing']],
                          'worst_cli_decode_ms': max(r['cli_decode_ms'] for r in frames)}), flush=True)
