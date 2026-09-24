#!/usr/bin/env python3
"""Plugin status is bounded, private, and cannot start capture or indexing."""
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import plugin_control as bridge


class PluginControlTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='replay-plugin-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        environment = {}
        for kind in ('CONFIG', 'DATA', 'STATE', 'CACHE', 'RUNTIME'):
            path = self.root / kind.lower()
            path.mkdir(mode=0o700)
            environment['XDG_' + kind + ('_DIR' if kind == 'RUNTIME' else '_HOME')] = str(path)
        patcher = patch.dict(os.environ, environment)
        patcher.start(); self.addCleanup(patcher.stop)
        self.app = self.root / 'data/omarchy-replay/app'
        self.payload = self.app / 'versions/0.1.0-test'
        self.runtime = self.root / 'runtime/omarchy-replay'

    def install(self):
        (self.payload / 'bin').mkdir(parents=True)
        (self.payload / 'bin/replay').touch()
        (self.payload / 'runtime-manifest.json').write_text(json.dumps({'version': '0.1.0'}))
        (self.app / 'current').symlink_to('versions/0.1.0-test')

    def test_missing_install_is_read_only(self):
        with patch.object(bridge.subprocess, 'run') as run, patch.object(bridge.subprocess, 'Popen') as spawn:
            self.assertFalse(bridge.action('status')['installed'])
            run.assert_not_called(); spawn.assert_not_called()
        self.assertFalse(self.app.exists())
        self.assertFalse(self.runtime.exists())

    def test_offline_status_preserves_saved_intent_without_starting(self):
        self.install()
        state = self.root / 'state/omarchy-replay/recording.json'
        state.parent.mkdir(mode=0o700)
        state.write_text('{"intent":"paused","indexing_paused":true}')
        state.chmod(0o600)
        with patch.object(bridge.subprocess, 'run') as run, patch.object(bridge.subprocess, 'Popen') as spawn:
            status = bridge.action('status')
            self.assertTrue(status['installed'])
            self.assertEqual(status['intent'], 'paused')
            self.assertFalse(status['service_running'])
            run.assert_not_called(); spawn.assert_not_called()
        self.assertFalse(self.runtime.exists())

    def test_live_status_requests_only_brief_state(self):
        self.install()
        self.runtime.mkdir(mode=0o700)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
            server.bind(str(self.runtime / 'control.sock'))
            server.listen()
            received = []
            def respond():
                with server.accept()[0] as client:
                    received.append(json.loads(client.recv(1024)))
                    client.sendall(b'{"running":true,"intent":"running","state":"locked","reason":"Desktop is locked."}\n')
            worker = threading.Thread(target=respond)
            worker.start()
            result = bridge.action('status')
            worker.join(2)
        self.assertEqual(received, [{'action': 'bar-status'}])
        self.assertEqual(result['state'], 'locked')
        self.assertEqual(result['intent'], 'running')
        self.assertTrue(result['service_running'])

    def test_outside_runtime_rejected(self):
        self.install()
        (self.app / 'current').unlink()
        (self.app / 'current').symlink_to(self.root)
        with self.assertRaisesRegex(RuntimeError, 'outside'):
            bridge.action('status')

    def test_state_symlink_and_oversize_rejected(self):
        self.install()
        state = self.root / 'state/omarchy-replay/recording.json'
        state.parent.mkdir(mode=0o700)
        state.symlink_to(self.payload / 'runtime-manifest.json')
        with self.assertRaises(OSError):
            bridge.action('status')
        state.unlink()
        state.write_bytes(b' ' * (bridge.LIMIT + 1)); state.chmod(0o600)
        with self.assertRaisesRegex(RuntimeError, 'size limit'):
            bridge.action('status')

    def test_setup_launches_explicit_terminal_without_shell(self):
        with patch.object(bridge.subprocess, 'Popen') as spawn:
            self.assertTrue(bridge.action('setup')['ok'])
            args = spawn.call_args.args[0]
            self.assertEqual(args[:3], ['omarchy', 'launch', 'terminal'])
            self.assertEqual(args[-1], 'setup-run')
            self.assertNotIn('shell', spawn.call_args.kwargs)

    def test_setup_cannot_race_an_existing_build(self):
        build = self.root / 'cache/build/test'
        with bridge.setup_lock(build):
            with self.assertRaisesRegex(RuntimeError, 'already running'):
                with bridge.setup_lock(build):
                    self.fail('Concurrent setup acquired the lock')
        with bridge.setup_lock(build):
            pass


if __name__ == '__main__':
    unittest.main()
