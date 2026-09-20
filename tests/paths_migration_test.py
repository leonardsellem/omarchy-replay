#!/usr/bin/env python3
"""Keep settings and history intact while renaming the early XDG directories."""
import fcntl
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from migrate_replay_paths import migrate


class MigrationTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='replay-migration-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.old = self.root / 'oma-rewind'
        self.new = self.root / 'omarchy-replay'
        self.old.mkdir(mode=0o700)
        self.payload = self.old / 'history/index.sqlite'
        self.payload.parent.mkdir(mode=0o700)
        self.payload.write_bytes(b'Preserve these bytes and this inode.')
        self.payload.chmod(0o600)

    def test_rename_preserves_content_permissions_inode_and_old_links(self):
        inode = self.payload.stat().st_ino
        pairs = [(self.old, self.new), (self.old, self.new)]
        receipt = migrate(pairs)
        self.assertEqual(len(receipt), 1)
        self.assertEqual((self.new / 'history/index.sqlite').stat().st_ino, inode)
        self.assertEqual(self.new.stat().st_mode & 0o777, 0o700)
        self.assertEqual(self.payload.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.payload.read_bytes(), b'Preserve these bytes and this inode.')
        self.assertTrue(self.old.is_symlink())
        self.assertEqual(migrate(pairs), [])

    def test_conflict_preflight_does_not_move_any_directory(self):
        other_old, other_new = self.root / 'old-state', self.root / 'new-state'
        other_old.mkdir(); other_new.mkdir()
        with self.assertRaisesRegex(RuntimeError, 'refusing to merge'):
            migrate([(self.old, self.new), (other_old, other_new)])
        self.assertFalse(self.old.is_symlink())
        self.assertFalse(self.new.exists())
        self.assertTrue(self.payload.is_file())

    def test_live_worker_lock_prevents_migration(self):
        lock = self.old / 'history/.indexer.lock'
        lock.touch(mode=0o600)
        with lock.open('rb') as worker:
            fcntl.flock(worker, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(RuntimeError, 'Stop the previous Replay'):
                migrate([(self.old, self.new)])
        self.assertFalse(self.new.exists())

    def test_failure_rolls_back_completed_renames(self):
        other_old, other_new = self.root / 'old-state', self.root / 'new-state'
        other_old.mkdir()
        real_rename = Path.rename
        def rename(path, target):
            if path == other_old:
                raise OSError('simulated rename failure')
            return real_rename(path, target)
        with patch.object(Path, 'rename', rename):
            with self.assertRaisesRegex(OSError, 'simulated'):
                migrate([(self.old, self.new), (other_old, other_new)])
        self.assertFalse(self.old.is_symlink())
        self.assertFalse(self.new.exists())
        self.assertTrue(self.payload.is_file())

    def test_unexpected_symlinks_are_rejected(self):
        other = self.root / 'unrelated'; other.mkdir()
        legacy = self.root / 'legacy-link'; legacy.symlink_to(other)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected legacy path'):
            migrate([(legacy, self.new)])
        self.assertTrue(other.is_dir())


if __name__ == '__main__':
    unittest.main()
