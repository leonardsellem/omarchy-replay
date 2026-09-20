#!/usr/bin/env python3
"""Offline recording controls never need to start a recorder or inspect a screen."""
import fcntl
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time

BINARY = str(Path(sys.argv[1]).resolve())


def main():
    with tempfile.TemporaryDirectory(prefix='replay-offline-controls-') as temporary:
        root = Path(temporary)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='',
                   QT_STYLE_OVERRIDE='Fusion', OMP_THREAD_LIMIT='1')
        for kind in ('CONFIG', 'DATA', 'STATE', 'CACHE', 'RUNTIME'):
            path = root / kind.lower()
            path.mkdir(mode=0o700)
            env['XDG_' + kind + ('_DIR' if kind == 'RUNTIME' else '_HOME')] = str(path)
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE'):
            env.pop(key, None)
        state = root / 'state/omarchy-replay/recording.json'
        runtime = root / 'runtime/omarchy-replay'
        hold = runtime / 'hold-capture-on-start'

        def call(action, success=True):
            result = subprocess.run([BINARY, 'daemon', action], env=env, text=True,
                                    capture_output=True, timeout=14)
            if success:
                assert result.returncode == 0, result.stderr
                return json.loads(result.stdout)
            assert result.returncode != 0, result.stdout
            return result.stderr

        def save(value):
            state.parent.mkdir(mode=0o700, exist_ok=True)
            state.write_text(json.dumps(value))
            state.chmod(0o600)

        first = call('status')
        assert first['intent'] == 'stopped' and first['running'] is False
        assert not state.parent.exists() and not runtime.exists(), 'status created service files'
        original = dict(intent='running', indexing_paused=True, last_retained_ms=123456,
                        deletion={'from': 10, 'until': 20}, future_field={'keep': True})
        save(original)
        status = call('status')
        assert status['intent'] == 'running' and status['indexing_paused'], 'offline status lost saved intent'
        assert status['running'] is False and not runtime.exists()
        for action, expected in (('pause', 'paused'), ('stop', 'stopped'), ('shutdown', 'stopped')):
            save(original)
            runtime.mkdir(mode=0o700, exist_ok=True)
            hold.write_text('pause\n'); hold.chmod(0o600)
            result = call(action)
            expected_state = dict(original, intent=expected)
            assert json.loads(state.read_text()) == expected_state, 'offline control discarded saved fields'
            assert result['intent'] == expected and result['indexing_paused'] and not result['running']
            assert call('status')['intent'] == expected
            assert state.stat().st_mode & 0o777 == 0o600
            assert not hold.exists(), 'stale startup hold overrides the new saved intent'
            assert not (root / 'data/omarchy-replay').exists(), 'offline control started a coordinator'
            assert not (runtime / 'control.sock').exists()

        save(original)
        lock = (runtime / 'coordinator.lock').open('r+')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            for action in ('stop', 'index-pause'):
                before = state.read_bytes()
                started = time.monotonic()
                error = call(action, success=False)
                assert 4.5 <= time.monotonic() - started < 12, 'coordinator wait was not bounded'
                assert 'starting or busy' in error
                assert state.read_bytes() == before and not hold.exists(), 'wrote behind the coordinator lease'

            # A coordinator can own its lease before its socket is ready. The
            # control must reach that process once ready, never change its file.
            received = []
            errors = []

            def starting_coordinator():
                try:
                    time.sleep(.3)
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                        server.settimeout(8)
                        server.bind(str(runtime / 'control.sock'))
                        server.listen()
                        with server.accept()[0] as client:
                            client.settimeout(3)
                            data = b''
                            while b'\n' not in data:
                                data += client.recv(65536)
                            received.append(json.loads(data)['action'])
                            client.sendall(b'{"running":true,"intent":"stopped","test_rpc":true}\n')
                except Exception as error:
                    errors.append(error)

            server = threading.Thread(target=starting_coordinator)
            server.start()
            try:
                result = call('stop')
                assert result['test_rpc'] and received == ['stop']
                assert json.loads(state.read_text()) == original, 'RPC control also wrote offline state'
            finally:
                server.join(timeout=10)
                (runtime / 'control.sock').unlink(missing_ok=True)
            assert not errors and not server.is_alive(), errors
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)
            lock.close()

        for invalid in ({'intent': 'unknown'}, {'intent': False}, {'indexing_paused': 'false'}):
            save(invalid)
            before = state.read_bytes()
            call('stop', success=False)
            assert state.read_bytes() == before
        for invalid in ('not json', ' ' * (128 * 1024 + 1)):
            state.write_text(invalid)
            call('status', success=False)
        state.unlink()
        target = root / 'unrelated.json'
        target.write_text(json.dumps(original)); target.chmod(0o600)
        state.symlink_to(target)
        call('stop', success=False)
        assert state.is_symlink() and json.loads(target.read_text()) == original
        state.unlink()
        os.link(target, state)
        call('status', success=False)
        state.unlink()
        os.mkfifo(state, mode=0o600)
        call('status', success=False)
        state.unlink()
        assert not (root / 'data/omarchy-replay').exists()
        print('PASS offline saved intent, no-spawn controls, lease handoff, bounded contention and private state validation')


if __name__ == '__main__':
    main()
