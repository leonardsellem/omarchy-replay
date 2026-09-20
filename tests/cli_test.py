#!/usr/bin/env python3
"""Finite scheduling and termination regressions; synthetic/offscreen only."""
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BIN = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/replay')
ENV = dict(os.environ, OMP_THREAD_LIMIT='1', QT_QPA_PLATFORM='offscreen')


def main():
    started = time.monotonic()
    proc = subprocess.run([BIN, 'demo', '--capture-only', '--static', '--realtime', '--frames', '8',
                           '--interval', '.25'], env=ENV, capture_output=True, text=True, timeout=5)
    assert proc.returncode == 0, proc.stderr
    result = json.loads(proc.stdout)
    assert result['samples_attempted'] == 8
    assert time.monotonic() - started < 5
    # Validate the longest recording duration without recording a display or
    # waiting four hours: synthetic demos stop at their requested frame count.
    longest = subprocess.run([BIN, 'demo', '--capture-only', '--static', '--frames', '1',
                              '--duration', '14400'], env=ENV, capture_output=True, text=True, timeout=5)
    assert longest.returncode == 0, longest.stderr
    assert json.loads(longest.stdout)['samples_attempted'] == 1
    for command in ([BIN, 'demo', '--interval', 'nan'], [BIN, 'record'],
                    [BIN, 'demo', '--duration', '14401'],
                    [BIN, 'demo', '--ocr-mode', 'unknown'],
                    [BIN, 'demo', '--ocr-cpu-ceiling-percent', '.5'],
                    [BIN, 'demo', '--ocr-cpu-ceiling-percent', '101'],
                    [BIN, 'demo', '--pressure-cpu-percent', '0'],
                    [BIN, 'demo', '--ocr-cpu-percent', '.5']):
        bad = subprocess.run(command, env=ENV, capture_output=True, timeout=5)
        assert bad.returncode != 0
    unavailable = subprocess.run([BIN, 'index', '--ocr-cpu-percent', '0',
                                  '--ocr-cpu-ceiling-percent', '60'],
                                 env=dict(ENV, DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent/replay-test-bus'),
                                 capture_output=True, text=True, timeout=5)
    assert unavailable.returncode and 'Worker CPU ceiling unavailable' in unavailable.stderr
    with tempfile.TemporaryDirectory(prefix='replay-cli-') as temporary:
        recorder = subprocess.Popen([BIN, 'demo', '--realtime', '--static', '--no-ocr', '--frames', '100',
                                     '--dir', str(Path(temporary)/'dataset')], env=ENV,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            time.sleep(.5)
            recorder.send_signal(signal.SIGINT)
            stdout, stderr = recorder.communicate(timeout=5)
            assert recorder.returncode == 0, stderr
            assert json.loads(stdout)['interrupted'] is True
        finally:
            if recorder.poll() is None:
                recorder.kill(); recorder.wait()
    with tempfile.TemporaryDirectory(prefix='replay-cli-budget-') as temporary:
        dataset = Path(temporary)/'dataset'
        recorder = subprocess.Popen([BIN, 'demo', '--realtime', '--workload', 'editing', '--frames', '100',
                                     '--interval', '.25', '--codec', 'h264',
                                     '--ocr-cpu-percent', '5', '--dir', str(dataset)], env=ENV,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            # Wait for actual acceptance before interrupting another OCR unit.
            deadline = time.monotonic() + 8
            accepted = False
            while time.monotonic() < deadline and recorder.poll() is None:
                try:
                    with sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True) as db:
                        accepted = db.execute('SELECT count(*) FROM frames').fetchone()[0] > 0
                except sqlite3.Error:
                    pass
                if accepted: break
                time.sleep(.02)
            assert accepted, 'Budgeted recorder did not accept its first frame'
            time.sleep(.35)
            recorder.send_signal(signal.SIGINT)
            stdout, stderr = recorder.communicate(timeout=5)
            assert recorder.returncode == 0, stderr
            result = json.loads(stdout)
            assert result['interrupted'] is True
            assert result['ocr_budget_cancellations'] >= 1, result
            assert result['cancelled_samples'] == 1, result
            assert result['samples_attempted'] == result['observations'] + result['capture_timeouts'] + result['cancelled_samples'], result
            extracted = subprocess.run([BIN, 'extract', '--dir', str(dataset), '--id', '1',
                                        '--out', str(Path(temporary)/'accepted.png')], env=ENV,
                                       capture_output=True, text=True, timeout=5)
            assert extracted.returncode == 0, extracted.stderr
        finally:
            if recorder.poll() is None:
                recorder.kill(); recorder.wait()
    fixture = subprocess.Popen([BIN, 'fixture', '--duration', '30'], env=ENV,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(.3)
        fixture.terminate()
        assert fixture.wait(timeout=2) != 0
    finally:
        if fixture.poll() is None:
            fixture.kill(); fixture.wait()
    print('Finite scheduling, CLI validation, graceful recording stop, and GUI termination passed.')


if __name__ == '__main__':
    main()
