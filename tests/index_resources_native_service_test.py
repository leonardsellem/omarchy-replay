#!/usr/bin/env python3
"""Run the native lifecycle test in one owned, finite user-service fixture.

python3 tests/index_resources_native_service_test.py build/replay [report.json]
The caller's existing limits are unchanged. This unique fixture is deliberately
constrained to TasksMax=32; the OCR job starts as a separate Replay workload.
"""
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import time
import uuid


def main():
    root = Path(__file__).resolve().parents[1]
    binary = Path(sys.argv[1]).resolve()
    report_path = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    if report_path and report_path.exists():
        raise RuntimeError('Refusing to replace an existing report')
    token = uuid.uuid4().hex
    unit = f'oma-replay-service-proof-{token}.service'
    description = f'Replay finite resource-service fixture {token}'
    runtime = os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}')
    bus = os.environ.get('DBUS_SESSION_BUS_ADDRESS', f'unix:path={runtime}/bus')
    env = dict(os.environ, XDG_RUNTIME_DIR=runtime, DBUS_SESSION_BUS_ADDRESS=bus)

    def state():
        result = subprocess.run(['systemctl', '--user', 'show', unit, '--property=LoadState', '--property=Description'],
                                env=env, capture_output=True, text=True, timeout=3)
        fields = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        if fields.get('LoadState') == 'not-found':
            return None
        if result.returncode:
            raise RuntimeError('Cannot inspect owned test unit: ' + result.stderr[:512])
        return fields

    def cleanup():
        current = state()
        if current:
            if current.get('Description') != description:
                raise RuntimeError('Test unit identity differs; refusing to stop it')
            result = subprocess.run(['systemctl', '--user', 'stop', '--no-block', unit], env=env,
                                    capture_output=True, text=True, timeout=3)
            if result.returncode:
                raise RuntimeError('Cannot stop owned test unit: ' + result.stderr[:512])
        deadline = time.monotonic() + 8
        while state() is not None:
            if time.monotonic() >= deadline:
                raise RuntimeError('Owned test unit was not collected within the cleanup deadline')
            time.sleep(.1)

    if state() is not None:
        raise RuntimeError('Unique fixture name is already loaded; refusing to reuse it')
    with tempfile.TemporaryDirectory(prefix='replay-native-service-proof-') as temporary:
        inner_report = Path(temporary) / 'native.json'
        command = [
            'systemd-run', '--user', '--wait', '--collect', '--pipe', '--quiet', f'--unit={unit}',
            f'--description={description}', '--service-type=exec', '--property=Slice=background.slice',
            '--property=TasksMax=32', '--property=RuntimeMaxSec=45', '--property=TimeoutStopSec=3',
            '--property=KillMode=control-group', f'--working-directory={root}',
            '--setenv=REPLAY_TEST_RESOURCE_SCOPE=1', f'--setenv=DBUS_SESSION_BUS_ADDRESS={bus}',
            f'--setenv=XDG_RUNTIME_DIR={runtime}', sys.executable,
            str(root / 'tests/index_resources_native_test.py'), str(binary), str(inner_report)]
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = bytearray()
        deadline = time.monotonic() + 52
        try:
            with selectors.DefaultSelector() as poll:
                poll.register(process.stdout, selectors.EVENT_READ)
                while poll.get_map():
                    if time.monotonic() > deadline:
                        raise RuntimeError('Finite native fixture exceeded its client deadline')
                    for key, _ in poll.select(.1):
                        block = os.read(key.fileobj.fileno(), 4096)
                        if not block:
                            poll.unregister(key.fileobj)
                            continue
                        if len(output) + len(block) > 65536:
                            raise RuntimeError('Native fixture exceeded the 64 KiB output bound')
                        output.extend(block)
                process.wait(timeout=3)
        finally:
            try:
                cleanup()
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)
                process.stdout.close()
        result = json.loads(inner_report.read_text()) if inner_report.exists() else {
            'status': 'failed', 'reason': output.decode(errors='replace')[-4096:]}
        result['fixture_unit'] = unit
        result['fixture_unit_collected'] = True
        result['fixture_runtime_max_seconds'] = 45
        result['fixture_tasks_max'] = 32
        result['systemd_run_exit_code'] = process.returncode
        rendered = json.dumps(result, indent=2) + '\n'
        if report_path:
            report_path.write_text(rendered)
        print(rendered, end='')
        if result.get('status') == 'passed' and process.returncode == 0:
            return 0
        if result.get('status') == 'skipped' and process.returncode == 77:
            return 77
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
