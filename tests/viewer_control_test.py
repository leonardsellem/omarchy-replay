#!/usr/bin/env python3
"""Open/settings reuse one synthetic viewer without starting a recorder."""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

BINARY = str(Path(sys.argv[1]).resolve())


def main():
    with tempfile.TemporaryDirectory(prefix='replay-view-control-') as temporary:
        root = Path(temporary)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='',
                   QT_STYLE_OVERRIDE='Fusion', OMP_THREAD_LIMIT='1')
        for kind in ('CONFIG', 'DATA', 'STATE', 'CACHE', 'RUNTIME'):
            path = root / kind.lower(); path.mkdir(mode=0o700)
            env['XDG_' + kind + ('_DIR' if kind == 'RUNTIME' else '_HOME')] = str(path)
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE'):
            env.pop(key, None)
        result = subprocess.run([BINARY, 'daemon', 'init'], env=env, capture_output=True, timeout=5)
        assert result.returncode == 0, result.stderr
        history = root / 'data/omarchy-replay/history'
        key = hashlib.sha256(os.fsencode(history.resolve())).hexdigest()[:24]
        runtime = root / 'runtime/omarchy-replay'
        endpoint = runtime / ('view-' + key + '.sock')
        command = [BINARY, 'view', '--dir', str(history)]
        with (root / 'viewer.log').open('wb') as log:
            viewer = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 5
                while not endpoint.exists() and viewer.poll() is None and time.monotonic() < deadline:
                    time.sleep(.05)
                assert endpoint.exists(), (root / 'viewer.log').read_text()
                for args in (['--settings'], ['--settings'], []):
                    result = subprocess.run(command + args, env=env, capture_output=True, timeout=4)
                    assert result.returncode == 0, result.stderr
                    assert viewer.poll() is None, 'First viewer exited during reuse'
                # Reject unsupported operations without disturbing the viewer.
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                    client.settimeout(2); client.connect(str(endpoint))
                    client.sendall(b'start\n')
                    assert client.recv(128) == b''
                assert not (runtime / 'control.sock').exists(), 'Opening settings started a coordinator'
                assert len(list(runtime.glob('view-*.sock'))) == 1
            finally:
                viewer.terminate()
                try:
                    viewer.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    viewer.kill(); viewer.wait(timeout=2)
        # Forced exit can leave socket/lease files. A later viewer must recover.
        with (root / 'reopened.log').open('wb') as log:
            reopened = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            try:
                time.sleep(.4)
                assert reopened.poll() is None, (root / 'reopened.log').read_text()
                result = subprocess.run(command, env=env, capture_output=True, timeout=4)
                assert result.returncode == 0, result.stderr
            finally:
                reopened.terminate()
                try:
                    reopened.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    reopened.kill(); reopened.wait(timeout=2)
    print('Viewer open/settings reuse and recovery passed; no recording started.')


if __name__ == '__main__':
    main()
