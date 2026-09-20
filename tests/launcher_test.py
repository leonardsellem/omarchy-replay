#!/usr/bin/env python3
"""Launcher checks with fake histories and executables; no desktop capture."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import open_replay
import viewer_launch


class LauncherTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='replay-launcher-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runs = self.root / 'runs'
        self.trial = self.runs / 'previous'
        self.dataset = self.trial / 'dataset'
        self.dataset.mkdir(parents=True)
        (self.dataset / 'index.sqlite').touch()
        self.config = {'scheduler': 'adaptive', 'ocr_mode': 'incremental', 'idle_seconds': 90,
                       'idle_cpu_percent': 35, 'request_cpu_percent': 25}
        (self.trial / 'trial.json').write_text(json.dumps({'config': self.config}))
        (self.runs / 'latest.json').write_text(json.dumps({'trial_directory': str(self.trial)}))
        self.calls = self.root / 'calls.json'
        self.service_calls = self.root / 'service-calls.json'
        self.binary = self.root / 'viewer-proxy'
        self.binary.write_text('#!/usr/bin/env python3\nimport json, sys\n'
                               'assert sys.argv[1] in ("view", "service"), "Capture is forbidden"\n'
                               f'target = {str(self.service_calls)!r} if sys.argv[1] == "service" else {str(self.calls)!r}\n'
                               'open(target, "w").write(json.dumps(sys.argv[1:]))\n'
                               'if sys.argv[1] == "service": print(json.dumps({"enabled": True, "running": True}))\n')
        self.binary.chmod(0o700)

    def launch(self, *args):
        environment = dict(os.environ)
        environment.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
        return subprocess.run([str(ROOT / 'scripts/replay'), 'open', '--binary', str(self.binary),
                               '--runs-dir', str(self.runs), *map(str, args)],
                              capture_output=True, text=True, env=environment, timeout=5)

    def test_latest_and_explicit_preserve_adaptive_settings(self):
        before = (self.runs / 'latest.json').read_bytes()
        for args in ((), ('--dir', self.dataset), ('--trial', self.trial)):
            result = self.launch(*args)
            self.assertEqual(result.returncode, 0, result.stderr)
            command = json.loads(self.calls.read_text())
            self.assertEqual(command[:3], ['view', '--dir', str(self.dataset)])
            self.assertEqual(command, ['view', '--dir', str(self.dataset)])
            service = json.loads(self.service_calls.read_text())
            self.assertEqual(service[:4], ['service', 'ensure', '--dir', str(self.dataset)])
            self.assertNotIn('--index-while-viewing', service)
            self.assertEqual(service[service.index('--idle-seconds') + 1], '90')
            self.assertEqual(service[service.index('--idle-cpu-percent') + 1], '35')
        self.assertEqual((self.runs / 'latest.json').read_bytes(), before)
        self.assertEqual(set(self.runs.iterdir()), {self.trial, self.runs / 'latest.json'})

    def test_no_argument_helper_opens_latest_without_capture(self):
        with patch.object(open_replay, 'selected_trial', return_value=self.trial), \
             patch.object(open_replay, 'launch_viewer', return_value=0) as launch:
            self.assertEqual(open_replay.main([]), 0)
            self.assertEqual(launch.call_args.args[1:], (self.dataset, self.config))

    def test_fixed_trial_resumes_saved_policy_without_boost(self):
        for cpu in (0, 7, 100):
            config = dict(self.config, scheduler='fixed', ocr_mode='full', ocr_cpu_percent=cpu, ocr_max_wall_ms=17000)
            (self.trial / 'trial.json').write_text(json.dumps({'config': config}))
            result = self.launch()
            self.assertEqual(result.returncode, 0, result.stderr)
            command = json.loads(self.service_calls.read_text())
            self.assertEqual(command[:2], ['service', 'ensure'])
            self.assertEqual(command[command.index('--scheduler') + 1], 'fixed')
            self.assertEqual(float(command[command.index('--ocr-cpu-percent') + 1]), cpu)
            self.assertEqual(command[command.index('--ocr-mode') + 1], 'full')
            self.assertEqual(command[command.index('--ocr-max-wall-ms') + 1], '17000')
            self.assertNotIn('--idle-cpu-percent', command)
            self.assertNotIn('--request-cpu-percent', command)

    def test_legacy_trial_keeps_fixed_policy_but_unknown_dataset_only_views(self):
        legacy = {'ocr_cpu_percent': 8, 'ocr_mode': 'incremental'}
        command = viewer_launch.view_command(self.binary, self.dataset, legacy)
        self.assertIn('--index-while-viewing', command)
        self.assertEqual(command[command.index('--scheduler') + 1], 'fixed')
        self.assertEqual(command[command.index('--ocr-cpu-percent') + 1], '8')
        self.assertEqual(viewer_launch.view_command(self.binary, self.dataset, {}),
                         [str(self.binary), 'view', '--dir', str(self.dataset)])

    def test_missing_history_and_invalid_settings_do_not_launch(self):
        result = self.launch('--dir', self.root / 'absent')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no saved index', result.stderr)
        self.assertFalse(self.calls.exists())
        for invalid in (True, float('nan'), 101, 0):
            config = dict(self.config, idle_cpu_percent=invalid)
            with self.assertRaises(RuntimeError):
                viewer_launch.view_command(self.binary, self.dataset, config)
        with self.assertRaises(RuntimeError):
            viewer_launch.view_command(self.binary, self.dataset, dict(self.config, ocr_mode='other'))
        for scheduler, cpu in (('other', 10), ('adaptive', 0), ('fixed', .5), ('fixed', True), ('fixed', float('inf'))):
            with self.assertRaises(RuntimeError):
                viewer_launch.view_command(self.binary, self.dataset, dict(self.config, scheduler=scheduler, ocr_cpu_percent=cpu))

    def test_focuses_only_matching_process_and_address(self):
        clients = [{'pid': 101, 'mapped': True, 'address': 'bad; dispatch'},
                   {'pid': 102, 'mapped': True, 'address': '0x111'},
                   {'pid': 103, 'mapped': True, 'address': '0x222'}]
        def matching(pid, binary):
            return (self.dataset if pid == 103 else self.root / 'other'), True
        with patch.dict(os.environ, {'HYPRLAND_INSTANCE_SIGNATURE': 'test'}), \
             patch.object(viewer_launch, 'process_view', side_effect=matching), \
             patch.object(viewer_launch.subprocess, 'run', side_effect=[
                 subprocess.CompletedProcess([], 0, b'{}'),
                 subprocess.CompletedProcess([], 0, json.dumps(clients).encode()),
                 subprocess.CompletedProcess([], 0, b'ok')]) as run, \
             patch.object(viewer_launch.subprocess, 'call') as launch:
            self.assertEqual(viewer_launch.launch_viewer(self.binary, self.dataset, self.config), 0)
            launch.assert_not_called()
            self.assertEqual(run.call_args_list[-1].args[0],
                             ['hyprctl', 'dispatch', 'hl.dsp.focus({ window = "address:0x222" })'])

    def test_legacy_focus_fallback_and_failed_focus_never_duplicates(self):
        clients = [{'pid': 103, 'mapped': True, 'address': '0x222'}]
        listed = subprocess.CompletedProcess([], 0, json.dumps(clients).encode())
        failed = subprocess.CompletedProcess([], 7, b'unsupported dispatcher')
        success = subprocess.CompletedProcess([], 0, b'ok')
        for final in (success, failed):
            with patch.object(viewer_launch, 'process_view', return_value=(self.dataset, True)), \
                 patch.object(viewer_launch.subprocess, 'run', side_effect=[subprocess.CompletedProcess([], 0, b'{}'), listed, failed, final]) as run, \
                 patch.object(viewer_launch.subprocess, 'call') as launch:
                if final.returncode == 0:
                    self.assertEqual(viewer_launch.launch_viewer(self.binary, self.dataset, self.config), 0)
                else:
                    with self.assertRaisesRegex(RuntimeError, 'already open'):
                        viewer_launch.launch_viewer(self.binary, self.dataset, self.config)
                launch.assert_not_called()
                self.assertEqual(run.call_args_list[-1].args[0],
                                 ['hyprctl', 'dispatch', 'focuswindow', 'address:0x222'])

    def test_existing_plain_viewer_cannot_silently_claim_to_resume_indexing(self):
        clients = [{'pid': 103, 'mapped': True, 'address': '0x222'}]
        listed = subprocess.CompletedProcess([], 0, json.dumps(clients).encode())
        with patch.object(viewer_launch, 'process_view', return_value=(self.dataset, False)), \
             patch.object(viewer_launch.subprocess, 'run', return_value=listed) as run, \
             patch.object(viewer_launch.subprocess, 'call') as launch:
            with self.assertRaisesRegex(RuntimeError, 'Close the existing Replay window and reopen this trial to resume indexing'):
                viewer_launch.focus_existing(self.dataset, self.binary, require_indexing=True)
            launch.assert_not_called()
            self.assertEqual(run.call_count, 1, 'Incompatible viewer was focused or replaced')
        # Viewing without a saved indexing policy can still reuse this window.
        with patch.object(viewer_launch, 'process_view', return_value=(self.dataset, False)), \
             patch.object(viewer_launch.subprocess, 'run', side_effect=[listed, subprocess.CompletedProcess([], 0, b'ok')]), \
             patch.object(viewer_launch.subprocess, 'call') as launch:
            self.assertEqual(viewer_launch.launch_viewer(self.binary, self.dataset, {}), 0)
            launch.assert_not_called()

    def test_process_identity_and_relative_dataset_are_checked(self):
        # Python is a test executable that accepts 'view' as its script name.
        (self.root / 'view').write_text('import time\ntime.sleep(20)\n')
        relative = self.dataset.relative_to(self.root)
        for arguments in ([], ['--scheduler', 'adaptive', '--index-while-viewing']):
            process = subprocess.Popen([sys.executable, 'view', '--dir', str(relative), *arguments], cwd=self.root)
            try:
                self.assertEqual(viewer_launch.process_view(process.pid, Path(sys.executable)), (self.dataset, bool(arguments)))
                self.assertIsNone(viewer_launch.process_view(process.pid, self.binary))
                self.assertIsNone(viewer_launch.process_view(-1, Path(sys.executable)))
            finally:
                process.terminate()
                process.wait(timeout=3)

    def test_hyprland_failure_falls_back_to_viewer(self):
        def run(command, **kwargs):
            if command[0] == str(self.binary):
                return subprocess.CompletedProcess(command, 0, b'{}')
            raise FileNotFoundError
        with patch.dict(os.environ, {'HYPRLAND_INSTANCE_SIGNATURE': 'test'}), \
             patch.object(viewer_launch.subprocess, 'run', side_effect=run), \
             patch.object(viewer_launch.subprocess, 'call', return_value=0) as launch:
            self.assertEqual(viewer_launch.launch_viewer(self.binary, self.dataset, self.config), 0)
            self.assertEqual(launch.call_args.args[0][1:4], ['view', '--dir', str(self.dataset)])

    def test_service_failure_does_not_claim_background_indexing_started(self):
        with patch.object(viewer_launch.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, b'', b'Invalid saved policy')), \
             patch.object(viewer_launch.subprocess, 'call', return_value=0) as launch, \
             patch.object(sys, 'stderr', io.StringIO()) as stderr:
            self.assertEqual(viewer_launch.launch_viewer(self.binary, self.dataset, self.config), 0)
            self.assertIn('Background indexing could not start', stderr.getvalue())
            self.assertIn('Saved history will still open', stderr.getvalue())
            self.assertEqual(launch.call_args.args[0], [str(self.binary), 'view', '--dir', str(self.dataset)])

    def test_reopening_keeps_later_service_policy_instead_of_trial_defaults(self):
        saved = self.dataset / '.index-service.json'
        saved.write_text(json.dumps({'enabled': True, 'paused': True, 'policy': {'scheduler': 'fixed', 'ocr_cpu_percent': 7}}))
        before = saved.read_bytes()
        self.assertEqual(viewer_launch.service_command(self.binary, self.dataset, self.config),
                         [str(self.binary), 'service', 'ensure', '--dir', str(self.dataset)])
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.service_calls.read_text()), ['service', 'ensure', '--dir', str(self.dataset)])
        self.assertEqual(saved.read_bytes(), before)

    def test_reopening_does_not_undo_explicit_stop(self):
        (self.dataset / '.index-service.json').write_text('{}')
        stopped = json.dumps({'enabled': False, 'running': False, 'paused': False}).encode()
        with patch.object(viewer_launch.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, stopped)) as run, \
             patch.object(viewer_launch.subprocess, 'call', return_value=0) as launch:
            self.assertEqual(viewer_launch.launch_viewer(self.binary, self.dataset, self.config), 0)
            services = [call.args[0] for call in run.call_args_list if call.args[0][0] == str(self.binary)]
            self.assertEqual(services, [[str(self.binary), 'service', 'ensure', '--dir', str(self.dataset)]])
            self.assertEqual(launch.call_args.args[0], [str(self.binary), 'view', '--dir', str(self.dataset)])

    def test_desktop_errors_are_visible_and_entry_only_launches_viewer(self):
        with patch.object(open_replay, 'selected_trial', side_effect=RuntimeError('No saved history.')), \
             patch.object(open_replay, 'notify_error') as notify, \
             patch.object(sys, 'stderr', io.StringIO()) as stderr:
            self.assertEqual(open_replay.main(['--notify-errors']), 1)
            notify.assert_called_once_with('No saved history.')
            self.assertIn('No saved history.', stderr.getvalue())
        result = subprocess.run([str(ROOT / 'scripts/replay'), 'desktop-entry'], text=True,
                                capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'Exec="{ROOT / "scripts/replay"}" open --notify-errors\n', result.stdout)
        self.assertIn('Name=Replay\n', result.stdout)
        self.assertNotIn('record', result.stdout)


if __name__ == '__main__':
    unittest.main()
