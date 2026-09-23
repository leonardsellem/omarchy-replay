#!/usr/bin/env python3
"""Synthetic configuration transactions; never mutates the desktop."""
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import install_capture_exclusions as installer


class CaptureExclusionsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='replay-masks-')
        self.addCleanup(temporary.cleanup)
        self.home = Path(temporary.name)
        self.config = self.home / 'hypr/hyprland.lua'
        self.config.parent.mkdir()
        self.original = '-- Unrelated settings\nhl.config({ general = { border_size = 2 } })\n'
        self.config.write_text(self.original)
        self.config.chmod(0o640)
        self.policy = self.home / 'validated.toml'
        self.policy.write_text('[exclusions]\napps = ["fixture.password"]\n')
        self.rule = self.home / 'omarchy-replay/hypr/capture-exclusions.lua'

    @staticmethod
    def command(*args):
        return '[""]' if args[-1] == 'configerrors' else 'ok'

    def install(self, **kwargs):
        return installer.install(self.home, self.policy, 'fixture-session', **kwargs)

    def test_idempotent_and_loaded_token_verification(self):
        with patch.object(installer, 'hyprctl', side_effect=self.command) as command:
            receipt = self.install()
            self.assertTrue(receipt['validated'])
            self.assertTrue(receipt['changed'])
            self.assertEqual(Path(receipt['backups'][0]).read_text(), self.original)
            self.assertEqual(self.config.stat().st_mode & 0o777, 0o640)
            self.assertEqual(self.rule.stat().st_mode & 0o777, 0o600)
            again = self.install()
            self.assertEqual(receipt['mask_token'], again['mask_token'])
            self.assertFalse(again['changed'])
            self.assertEqual(again['backups'], [])
            self.assertTrue(self.install(check=True)['validated'])
            self.assertTrue(any('assert(_G.oma_replay_capture_exclusions' in call.args[-1]
                                for call in command.call_args_list))
        self.assertIn('initial_class = "^omarchy-replay$"', self.rule.read_text())
        self.assertIn('class = "^fixture\\092.password$"', self.rule.read_text())

    def test_literal_ids_and_title_search_render_as_and_rules(self):
        self.policy.write_text('[exclusions]\napps=[]\n[[exclusions.windows]]\n'
                               'app_id="fixture.editor+test"\ntitle_regex="invoice"\n')
        policy, _ = installer.read_policy(self.policy)
        data, token, broadened = installer.render_rules(policy)
        rule = data.decode()
        self.assertEqual(len(token), 64)
        self.assertEqual(broadened, 0)
        self.assertIn('class = "^fixture\\092.editor\\092+test$", title = "(?s:.*)(?:invoice)(?s:.*)"', rule)
        self.assertIn('initial_class = "^fixture\\092.editor\\092+test$", title = ', rule)
        self.assertEqual(rule.count('no_screen_share = true'), 6)
        self.assertEqual(installer.title_pattern('^invoice$'), r'(?s:.*)(?:^invoice(?:\n?$))(?s:.*)')
        self.assertEqual(installer.title_pattern(r'[$]\$\Q$\E'), r'(?s:.*)(?:[$]\$\Q$\E)(?s:.*)')
        self.assertEqual(installer.title_pattern('[]$][^]$][[:alpha:]$]$'),
                         r'(?s:.*)(?:[]$][^]$][[:alpha:]$](?:\n?$))(?s:.*)')

    def test_defaults_use_exact_masks_and_explicit_apps_can_opt_out(self):
        mandatory = {'omarchy-replay', 'org.omarchy.screensaver'}
        removable = set(installer.DEFAULT_APPS) - mandatory
        for source in ('', '[exclusions]\n'):
            self.policy.write_text(source)
            policy, _ = installer.read_policy(self.policy)
            rule = installer.render_rules(policy)[0].decode()
            self.assertIn('com.belmoussaoui.Authenticator', policy['apps'])
            self.assertIn('Bitwarden', policy['apps'])
            for app in installer.DEFAULT_APPS:
                self.assertIn(app, policy['apps'])
                literal = re.escape(app).replace(r'\ ', ' ').replace(r'\-', '-').replace('\\', '\\092')
                for field in ('class', 'initial_class'):
                    self.assertEqual(rule.count('  match = { ' + field + ' = "^' + literal + '$" },\n'), 1)
            for optional in ('net.lutris.Lutris', 'heroic', 'com.libretro.RetroArch',
                             'com.moonlight_stream.Moonlight', 'mpv', 'vlc'):
                self.assertNotIn(optional, policy['apps'])
        for apps in ('[]', '["fixture.editor"]'):
            self.policy.write_text('[exclusions]\napps=' + apps + '\n')
            policy, _ = installer.read_policy(self.policy)
            rule = installer.render_rules(policy)[0].decode()
            self.assertEqual(set(policy['apps']), mandatory | ({'fixture.editor'} if apps != '[]' else set()))
            for app in removable:
                self.assertNotIn(app, policy['apps'])
            self.assertEqual(rule.count('no_screen_share = true'), len(policy['apps']) * 2)

    def test_screensaver_masks_cannot_be_removed_by_custom_apps(self):
        for apps in ('[]', '["fixture.editor"]', '["org.omarchy.screensaver"]'):
            self.policy.write_text('[exclusions]\napps=' + apps + '\n')
            policy, _ = installer.read_policy(self.policy)
            rule = installer.render_rules(policy)[0].decode()
            for field in ('class', 'initial_class'):
                self.assertEqual(rule.count('{ ' + field + ' = "^org\\092.omarchy\\092.screensaver$"'), 1)

    def test_address_bound_masks_are_broader_and_stale_or_address_only_rejected(self):
        prefix = '[exclusions]\napps=[]\n[[exclusions.windows]]\naddress="0x1234"\n'
        self.policy.write_text(prefix + 'app_id="fixture.editor"\ncompositor_instance="fixture-session"\n')
        with patch.object(installer, 'hyprctl', side_effect=self.command):
            self.assertEqual(self.install()['broadened_address_masks'], 1)
        self.assertNotIn('0x1234', self.rule.read_text())
        self.policy.write_text(prefix + 'app_id="fixture.editor"\ncompositor_instance="old-session"\n')
        with self.assertRaisesRegex(RuntimeError, 'previous desktop'):
            self.install()
        self.policy.write_text(prefix + 'compositor_instance="fixture-session"\n')
        with self.assertRaisesRegex(RuntimeError, 'Address-only'):
            self.install()

    def test_rule_or_loaded_token_failure_restores_configuration(self):
        for bad_kind in ('config', 'token'):
            config_checks = 0
            def fail_once(*args):
                nonlocal config_checks
                if args[-1] == 'configerrors':
                    config_checks += 1
                    return '["private title must not escape"]' if bad_kind == 'config' and config_checks == 2 else '[]'
                if 'eval' in args and bad_kind == 'token':
                    return 'assertion failed'
                return 'ok'
            with patch.object(installer, 'hyprctl', side_effect=fail_once):
                with self.assertRaises(RuntimeError) as caught:
                    self.install()
            self.assertNotIn('private title', str(caught.exception))
            self.assertEqual(self.config.read_text(), self.original)
            self.assertFalse(self.rule.exists())

    def test_existing_valid_rule_restored_after_failed_update(self):
        with patch.object(installer, 'hyprctl', side_effect=self.command):
            self.install()
        old_rule, old_config = self.rule.read_bytes(), self.config.read_bytes()
        self.policy.write_text('[exclusions]\napps=["fixture.changed"]\n')
        seen = 0
        def fail_update(*args):
            nonlocal seen
            if args[-1] == 'configerrors':
                seen += 1
                return '["invalid rule"]' if seen == 2 else '[]'
            return 'ok'
        with patch.object(installer, 'hyprctl', side_effect=fail_update):
            with self.assertRaises(RuntimeError):
                self.install()
        self.assertEqual(self.rule.read_bytes(), old_rule)
        self.assertEqual(self.config.read_bytes(), old_config)

    def test_concurrent_edits_and_unmanaged_targets_are_preserved(self):
        newer = self.original + '-- Concurrent editor\n'
        def edit_before(*args):
            self.config.write_text(newer)
            return '[]'
        with patch.object(installer, 'hyprctl', side_effect=edit_before):
            with self.assertRaisesRegex(RuntimeError, 'changed during installation'):
                self.install()
        self.assertEqual(self.config.read_text(), newer)
        self.assertFalse(self.rule.exists())
        self.rule.parent.mkdir(parents=True)
        self.rule.write_text('-- user-owned\n')
        with self.assertRaisesRegex(RuntimeError, 'unmanaged'):
            self.install()
        self.assertEqual(self.rule.read_text(), '-- user-owned\n')

    def test_last_include_and_read_only_check_refuse_later_overrides(self):
        with patch.object(installer, 'hyprctl', side_effect=self.command):
            self.install()
            self.config.write_text(self.config.read_text() + '-- Later rules\n')
            with self.assertRaisesRegex(RuntimeError, 'reapplied'):
                self.install(check=True)
            self.install()
        self.assertIn('-- Later rules\n\n' + installer.BEGIN, self.config.read_text())
        self.assertEqual(self.config.read_text().count(installer.BEGIN), 1)

    def test_concurrently_retained_include_has_valid_but_unverified_target(self):
        checks = 0
        def edit_after_apply(*args):
            nonlocal checks
            if args[-1] == 'configerrors':
                checks += 1
                if checks == 2:
                    self.config.write_text(self.config.read_text() + '-- Concurrent edit\n')
                    return '["invalid rule"]'
                return '[]'
            return 'ok'
        with patch.object(installer, 'hyprctl', side_effect=edit_after_apply):
            with self.assertRaises(RuntimeError):
                self.install()
        self.assertTrue(self.config.read_text().endswith('-- Concurrent edit\n'))
        self.assertIn(installer.BEGIN, self.config.read_text())
        self.assertEqual(self.rule.read_text(), installer.PREFIX + '_G.oma_replay_capture_exclusions = nil\n')


if __name__ == '__main__':
    unittest.main()
