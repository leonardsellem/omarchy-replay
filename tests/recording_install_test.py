#!/usr/bin/env python3
"""Native installer transactions without changing the actual desktop."""
import json
import io
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import install_recording_service as installer


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='replay-service-install-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'hypr/bindings.lua'
        self.path.parent.mkdir()
        self.original = '-- User settings\no.bind("SUPER + A", "App", "some-app")\n'
        self.path.write_text(self.original)
        self.path.chmod(0o640)

    def test_shortcut_is_idempotent_and_preserves_existing_settings(self):
        with patch.object(installer, 'hyprctl', return_value='[]'), patch.object(installer, 'validate'):
            installer.install_shortcut(self.root)
            once = self.path.read_text()
            installer.install_shortcut(self.root)
        self.assertEqual(once, self.path.read_text())
        self.assertTrue(once.startswith(self.original))
        self.assertEqual(once.count(installer.BEGIN), 1)
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o640)
        self.assertIn('open --toggle --notify-errors', once)

    def test_existing_binding_symlink_and_concurrent_edits_are_preserved(self):
        with patch.object(installer, 'hyprctl', return_value=json.dumps([{'modmask': 72, 'key': 'R'}])):
            with self.assertRaisesRegex(RuntimeError, 'already bound'):
                installer.install_shortcut(self.root)
        self.assertEqual(self.path.read_text(), self.original)
        with patch.object(installer, 'hyprctl', return_value='[]'), \
             patch.object(installer, 'validate', side_effect=lambda: self.path.write_text('new user edit\n')):
            with self.assertRaisesRegex(RuntimeError, 'Bindings changed'):
                installer.install_shortcut(self.root)
        self.assertEqual(self.path.read_text(), 'new user edit\n')
        self.path.unlink(); target = self.root / 'linked.lua'; target.write_text(self.original); self.path.symlink_to(target)
        with self.assertRaisesRegex(RuntimeError, 'symlink'):
            installer.install_shortcut(self.root)
        self.assertEqual(target.read_text(), self.original)

    def test_validation_failure_rolls_back_owned_change(self):
        with patch.object(installer, 'hyprctl', return_value='[]'), \
             patch.object(installer, 'validate', side_effect=[None, RuntimeError('bad config')]):
            with self.assertRaisesRegex(RuntimeError, 'bad config'):
                installer.install_shortcut(self.root)
        self.assertEqual(self.path.read_text(), self.original)

    def test_invalid_xdg_values_match_native_fallback(self):
        fallback = self.root / 'fallback'
        for value in ('', 'relative/path', '~/config'):
            with patch.dict(os.environ, {'XDG_CONFIG_HOME': value}):
                self.assertEqual(installer.xdg_path('XDG_CONFIG_HOME', fallback), fallback)

    def test_managed_unit_quoting_and_unmanaged_file_are_safe(self):
        text = installer.unit_text(Path('/tmp/space dir/100%/replay'))
        self.assertIn('ExecStart="/tmp/space dir/100%%/replay" daemon run', text)
        self.assertIn('KillMode=control-group', text)
        target = self.root / 'existing.service'; target.write_text('user service\n')
        with self.assertRaisesRegex(RuntimeError, 'not managed'):
            installer.managed_write(target, 'replacement', '# Managed', 0o600)
        self.assertEqual(target.read_text(), 'user service\n')

    def test_reinstall_restarts_only_an_already_running_coordinator(self):
        repository = self.root / 'repository'
        binary = repository / 'build/replay'; binary.parent.mkdir(parents=True); binary.touch()
        settings = self.root / 'omarchy-replay/config.toml'; settings.parent.mkdir()
        settings.write_text('[service]\nlogin_startup = false\n')
        unit = self.root / 'systemd/user/omarchy-replay.service'; unit.parent.mkdir(parents=True)
        unit.write_text('# Managed by Omarchy Replay\nold unit\n')
        environment = {name: str(self.root / name) for name in
                       ('XDG_DATA_HOME', 'XDG_STATE_HOME', 'XDG_CACHE_HOME', 'XDG_RUNTIME_DIR')}
        environment['XDG_CONFIG_HOME'] = str(self.root)
        for active in (False, True):
            commands = []
            def run(command, **kwargs):
                commands.append(command)
                result = 0 if active or 'is-active' not in command else 3
                return subprocess.CompletedProcess(command, result, b'{}', b'')
            with patch.dict(os.environ, environment), patch.object(installer, 'ROOT', repository), \
                 patch.object(installer, 'install_rule', return_value={'validated': True}), \
                 patch.object(installer.subprocess, 'run', side_effect=run), patch.object(sys, 'stdout', io.StringIO()):
                self.assertEqual(installer.main(['--no-shortcut']), 0)
            self.assertEqual(['systemctl', '--user', 'start', 'omarchy-replay.service'] in commands, active)
            self.assertEqual(['systemctl', '--user', 'stop', 'omarchy-replay.service'] in commands, active)
            self.assertIn(['systemctl', '--user', 'disable', 'omarchy-replay.service'], commands)
            self.assertEqual(settings.read_text(), '[service]\nlogin_startup = false\n')


if __name__ == '__main__':
    unittest.main()
