#!/usr/bin/env python3
"""Numerical accounting checks using fake procfs and private worker receipts."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import measure
import trial


class WorkerAccounting(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='replay-measurement-')
        self.root = Path(self.temporary.name)
        self.proc = self.root / 'proc'
        self.dataset = self.root / 'history'
        self.dataset.mkdir()
        self.binary = self.root / 'replay'
        self.binary.write_text('synthetic executable identity')
        self.runtime = self.root / 'runtime'
        self.runtime.mkdir(mode=0o700)
        app = self.runtime / 'replay'
        app.mkdir(mode=0o700)
        private = app / hashlib.sha256(str(self.dataset).encode()).hexdigest()
        private.mkdir(mode=0o700)
        self.receipt_path = private / 'worker.json'
        self.environment = {'XDG_RUNTIME_DIR': str(self.runtime)}
        self.receipt = {'pid': 200, 'process_start_ticks': '900',
                        'owner_pid': 100, 'owner_start_ticks': '700'}
        self.write_receipt()
        self.process(100, '700', cpu=10, pss=1024, children=[101])
        self.process(101, '800', cpu=20, pss=2048)
        self.process(200, '900', cpu=150, pss=4096)

    def tearDown(self):
        self.temporary.cleanup()

    def write_receipt(self):
        self.receipt_path.write_text(json.dumps(self.receipt))
        self.receipt_path.chmod(0o600)

    def process(self, pid, started, cpu=0, pss=0, children=()):
        base = self.proc / str(pid)
        base.mkdir(parents=True, exist_ok=True)
        fields = ['0'] * 22
        fields[0], fields[11], fields[12], fields[19] = 'S', str(cpu), '0', str(started)
        (base / 'stat').write_text(f'{pid} (synthetic (worker)) ' + ' '.join(fields))
        (base / 'smaps_rollup').write_text(f'Rss: {pss * 2} kB\nPss: {pss} kB\n')
        (base / 'io').write_text('read_bytes: 0\nwrite_bytes: 4096\n')
        tasks = base / 'task' / str(pid)
        tasks.mkdir(parents=True, exist_ok=True)
        (tasks / 'children').write_text(' '.join(map(str, children)))
        (base / 'cmdline').write_bytes(os.fsencode(self.binary) + b'\0index\0--dir\0' + os.fsencode(self.dataset) + b'\0')
        (base / 'exe').unlink(missing_ok=True)
        (base / 'exe').symlink_to(self.binary)

    def rows(self, **kwargs):
        return measure.sampled_processes(100, dataset=self.dataset, binary=self.binary,
                                         environment=self.environment, proc_root=self.proc, **kwargs)

    def test_service_worker_is_included_with_cpu_and_pss(self):
        rows = self.rows()
        self.assertEqual({row['pid'] for row in rows}, {100, 101, 200})
        self.assertEqual(sum(row['cpu_ticks'] for row in rows), 180)
        self.assertEqual(sum(row['pss'] for row in rows), 7 * 2**20)

    def test_ordinary_descendants_unchanged_without_dataset(self):
        rows = measure.sampled_processes(100, proc_root=self.proc)
        self.assertEqual({row['pid'] for row in rows}, {100, 101})
        self.assertEqual(sum(row['cpu_ticks'] for row in rows), 30)

    def test_descendant_worker_is_not_counted_twice(self):
        (self.proc / '100/task/100/children').write_text('101 200')
        self.assertEqual(len(self.rows()), 3)
        self.assertEqual(sum(row['cpu_ticks'] for row in self.rows()), 180)

    def test_service_worker_children_are_counted_once(self):
        self.process(201, '950', cpu=50, pss=512)
        (self.proc / '200/task/200/children').write_text('201 101')
        self.assertEqual(len(self.rows()), 4)
        self.assertEqual(sum(row['cpu_ticks'] for row in self.rows()), 230)

    def test_script_launcher_uses_exact_sampled_owner_executable(self):
        launcher = self.root / 'launcher'
        launcher.write_text('#!/bin/sh\nexec replay "$@"\n')
        rows = measure.sampled_processes(100, dataset=self.dataset, binary=launcher,
                                         environment=self.environment, proc_root=self.proc)
        self.assertEqual(len(rows), 3)
        (self.proc / '100/cmdline').write_bytes(b'sh\0-c\0unrelated\0')
        rows = measure.sampled_processes(100, dataset=self.dataset, binary=launcher,
                                         environment=self.environment, proc_root=self.proc)
        self.assertEqual(len(rows), 2)

    def test_stale_worker_or_owner_identity_is_excluded(self):
        for key, value in [('process_start_ticks', '899'), ('owner_start_ticks', '699'), ('owner_pid', 999), ('pid', True)]:
            with self.subTest(key=key):
                original = self.receipt[key]
                self.receipt[key] = value
                self.write_receipt()
                self.assertEqual({row['pid'] for row in self.rows()}, {100, 101})
                self.receipt[key] = original

    def test_receipt_missing_identity_is_excluded(self):
        self.receipt.pop('process_start_ticks')
        self.write_receipt()
        self.assertEqual(len(self.rows()), 2)

    def test_wrong_executable_is_excluded(self):
        other = self.root / 'other-binary'
        other.write_text('not Replay')
        executable = self.proc / '200/exe'
        executable.unlink()
        executable.symlink_to(other)
        self.assertEqual(len(self.rows()), 2)

    def test_wrong_command_dataset_and_ambiguous_dataset_are_excluded(self):
        prefix = os.fsencode(self.binary)
        cases = [prefix + b'\0view\0--dir\0' + os.fsencode(self.dataset) + b'\0',
                 prefix + b'\0index\0--dir\0/other/dataset\0',
                 prefix + b'\0index\0--dir\0history\0',
                 prefix + b'\0index\0--dir\0' + os.fsencode(self.dataset) + b'\0--dir=/other\0']
        for cmdline in cases:
            with self.subTest(cmdline=cmdline):
                (self.proc / '200/cmdline').write_bytes(cmdline)
                self.assertEqual(len(self.rows()), 2)

    def test_untrusted_receipt_is_excluded(self):
        self.receipt_path.chmod(0o644)
        self.assertEqual(len(self.rows()), 2)
        self.receipt_path.unlink()
        target = self.root / 'linked-receipt'
        target.write_text(json.dumps(self.receipt))
        target.chmod(0o600)
        self.receipt_path.symlink_to(target)
        self.assertEqual(len(self.rows()), 2)

    def test_oversized_and_malformed_receipts_are_excluded(self):
        for value in ['x' * 65537, '{broken', '[]']:
            self.receipt_path.write_text(value)
            self.assertEqual(len(self.rows()), 2)

    def test_owner_exit_during_sampling_is_excluded(self):
        original = measure.process_sample

        def disappearing_owner(pid, proc_root):
            row = original(pid, proc_root)
            if pid == 200:
                (self.proc / '100/stat').unlink()
            return row

        with patch.object(measure, 'process_sample', side_effect=disappearing_owner):
            self.assertEqual(len(self.rows()), 2)

    def test_trial_sampler_counts_verified_rows_once(self):
        sampler = trial.Sampler(self.dataset, self.binary, self.environment)
        with patch.object(trial, 'sampled_processes', return_value=self.rows()) as samples:
            row = sampler.sample(100, 1)
            again = sampler.sample(100, 2)
        samples.assert_called_with(100, dataset=self.dataset, binary=self.binary, environment=self.environment)
        self.assertEqual(row['process_count'], 3)
        self.assertEqual(row['tree_pss_mib'], 7)
        self.assertEqual(row['sampled_cpu_seconds_lower_bound'], 180 / os.sysconf('SC_CLK_TCK'))
        self.assertEqual(again['sampled_cpu_seconds_lower_bound'], row['sampled_cpu_seconds_lower_bound'])
        self.assertEqual(row['sampled_root_write_bytes'], 4096)

    def test_new_cpu_receipts_remain_numeric_and_do_not_copy_content(self):
        receipt = {'managed_index_cpu_seconds': 1.25, 'total_cpu_seconds': 2.0,
                   'process_cpu_seconds': 1.1, 'process_start_ticks': 900,
                   'owner_pid': 100, 'text': 'Private recognized screen content',
                   'scope': 'Private runtime detail', 'self_cpu_seconds': float('nan')}
        self.assertEqual(trial.numeric(receipt, trial.RECORDER_FIELDS),
                         {'managed_index_cpu_seconds': 1.25, 'total_cpu_seconds': 2.0})
        self.assertEqual(trial.numeric(receipt, trial.WORKER_FIELDS),
                         {'managed_index_cpu_seconds': 1.25, 'total_cpu_seconds': 2.0,
                          'process_cpu_seconds': 1.1})


if __name__ == '__main__':
    unittest.main()
