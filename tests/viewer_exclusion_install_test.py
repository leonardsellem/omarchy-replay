#!/usr/bin/env python3
"""Installer transactions in a temporary config root; never changes the desktop."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import install_viewer_exclusion as installer


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='replay-rule-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = self.root / 'hypr/hyprland.lua'
        self.config.parent.mkdir()
        self.original = '-- Existing unrelated settings\nhl.config({ general = { border_size = 2 } })\n'
        self.config.write_text(self.original)
        self.config.chmod(0o640)

    def test_install_is_idempotent_preserves_settings_and_has_backup(self):
        with patch.object(installer, 'hyprctl', return_value='') as command:
            result = installer.install(self.root)
            self.assertTrue(result['changed'])
            self.assertTrue(result['validated'])
            self.assertEqual(Path(result['backups'][0]).read_text(), self.original)
            self.assertTrue(self.config.read_text().startswith(self.original))
            self.assertEqual(self.config.stat().st_mode & 0o777, 0o640)
            again = installer.install(self.root)
            self.assertFalse(again['changed'])
            self.assertEqual(again['backups'], [])
            self.assertEqual(self.config.read_text().count(installer.BEGIN), 1)
            self.assertEqual(command.call_count, 6)

    def test_invalid_rule_rolls_back_and_revalidates(self):
        with patch.object(installer, 'hyprctl', side_effect=['', '', 'bad rule', '', '']) as command:
            with self.assertRaisesRegex(RuntimeError, 'bad rule'):
                installer.install(self.root)
            self.assertEqual(command.call_count, 5)
        self.assertEqual(self.config.read_text(), self.original)
        self.assertFalse((self.root / 'omarchy-replay/hypr/replay-viewer.lua').exists())

    def test_rejected_update_restores_an_already_included_rule(self):
        with patch.object(installer, 'hyprctl', return_value=''):
            installer.install(self.root)
        rule = self.root / 'omarchy-replay/hypr/replay-viewer.lua'
        previous = rule.read_bytes()
        config = self.config.read_bytes()
        source = self.root / 'replacement.lua'
        source.write_bytes(previous + b'-- Simulated rejected update\n')
        with patch.object(installer, 'hyprctl', side_effect=['', '', 'bad update', '', '']):
            with self.assertRaisesRegex(RuntimeError, 'bad update'):
                installer.install(self.root, source=source)
        self.assertEqual(rule.read_bytes(), previous)
        self.assertEqual(self.config.read_bytes(), config)

    def test_preexisting_errors_or_unmanaged_file_leave_configuration_alone(self):
        with patch.object(installer, 'hyprctl', return_value='existing error'):
            with self.assertRaisesRegex(RuntimeError, 'existing error'):
                installer.install(self.root)
        self.assertEqual(self.config.read_text(), self.original)
        rule = self.root / 'omarchy-replay/hypr/replay-viewer.lua'
        rule.parent.mkdir(parents=True)
        rule.write_text('-- My own rule\n')
        with self.assertRaisesRegex(RuntimeError, 'unmanaged'):
            installer.install(self.root)
        self.assertEqual(rule.read_text(), '-- My own rule\n')
        self.assertEqual(self.config.read_text(), self.original)

    def test_owned_block_moves_after_later_overrides(self):
        once = installer.updated_config(self.original, '/path/with "quotes"/é.lua')
        twice = installer.updated_config(once + '-- Later setting\n', '/path/with "quotes"/é.lua')
        self.assertIn('-- Later setting\n\n' + installer.BEGIN, twice)
        self.assertEqual(twice.count(installer.BEGIN), 1)
        self.assertIn('\\034quotes\\034', twice)
        self.assertIn('\\195\\169.lua', twice)

    def test_window_and_capture_rules_keep_separate_managed_blocks(self):
        begin, end = '-- BEGIN Omarchy Replay window', '-- END Omarchy Replay window'
        with patch.object(installer, 'hyprctl', return_value=''):
            installer.install(self.root)
            installer.install(self.root, source=ROOT / 'config/hypr/replay-window.lua',
                              filename='replay-window.lua', begin=begin, end=end)
            once = self.config.read_text()
            installer.install(self.root, source=ROOT / 'config/hypr/replay-window.lua',
                              filename='replay-window.lua', begin=begin, end=end)
        self.assertEqual(once, self.config.read_text())
        self.assertEqual(once.count(installer.BEGIN), 1)
        self.assertEqual(once.count(begin), 1)
        self.assertTrue((self.root / 'omarchy-replay/hypr/replay-viewer.lua').is_file())
        self.assertTrue((self.root / 'omarchy-replay/hypr/replay-window.lua').is_file())

    def test_concurrent_edit_before_apply_is_preserved(self):
        newer = self.original + '-- Added by another editor\n'
        def edit_during_validation(*_):
            self.config.write_text(newer)
            return ''
        with patch.object(installer, 'hyprctl', side_effect=edit_during_validation):
            with self.assertRaisesRegex(RuntimeError, 'changed during installation'):
                installer.install(self.root)
        self.assertEqual(self.config.read_text(), newer)
        self.assertFalse((self.root / 'omarchy-replay/hypr/replay-viewer.lua').exists())

    def test_failed_validation_does_not_break_concurrently_retained_include(self):
        calls = 0
        def edit_after_apply(*_):
            nonlocal calls
            calls += 1
            if calls == 3:
                self.config.write_text(self.config.read_text() + '-- Concurrent edit\n')
                return 'another setting needs attention'
            return ''
        with patch.object(installer, 'hyprctl', side_effect=edit_after_apply):
            with self.assertRaisesRegex(RuntimeError, 'needs attention'):
                installer.install(self.root)
        self.assertTrue(self.config.read_text().endswith('-- Concurrent edit\n'))
        self.assertIn(installer.BEGIN, self.config.read_text())
        self.assertTrue((self.root / 'omarchy-replay/hypr/replay-viewer.lua').is_file())


if __name__ == '__main__':
    unittest.main()
