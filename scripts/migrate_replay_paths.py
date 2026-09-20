"""Rename Replay's early XDG directories without copying or merging archives."""
from contextlib import ExitStack
import fcntl
import os
from pathlib import Path
import stat


def migration_plan(pairs):
    planned = []
    seen = set()
    for old, new in pairs:
        old, new = Path(old), Path(new)
        if (old, new) in seen:
            continue
        seen.add((old, new))
        if old.is_symlink():
            if new.is_dir() and old.resolve() == new.resolve():
                continue
            raise RuntimeError(f'Unexpected legacy path symlink: {old}')
        if not old.exists():
            continue
        if not old.is_dir() or old.stat().st_uid != os.getuid():
            raise RuntimeError(f'Legacy Replay path is not an owned directory: {old}')
        if new.exists() or new.is_symlink():
            raise RuntimeError(f'Both Replay locations exist; refusing to merge {old} into {new}')
        planned.append((old, new))
    return planned


def migrate(pairs):
    planned = migration_plan(pairs)
    moved = []
    with ExitStack() as leases:
        for old, _ in planned:
            for lock in (old / 'coordinator.lock', old / 'history/.capture.lock', old / 'history/.indexer.lock'):
                if not lock.exists() and not lock.is_symlink():
                    continue
                fd = os.open(lock, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
                leases.callback(os.close, fd)
                info = os.fstat(fd)
                if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
                    raise RuntimeError('Replay migration lock is not an owned regular file')
                try:
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError as error:
                    raise RuntimeError('Stop the previous Replay coordinator and index workers before migrating.') from error
        try:
            for old, new in planned:
                new.parent.mkdir(parents=True, exist_ok=True)
                # Never replace a destination created after the initial check.
                if new.exists() or new.is_symlink():
                    raise RuntimeError(f'Replay destination appeared during migration: {new}')
                old.rename(new)
                moved.append((old, new))
                # Loaded compositor rules and old history links keep resolving.
                # Canonical writes and the new unit use the new real directory.
                old.symlink_to(new, target_is_directory=True)
        except BaseException:
            for old, new in reversed(moved):
                if old.is_symlink() and old.resolve() == new.resolve():
                    old.unlink()
                if not old.exists() and not old.is_symlink():
                    new.rename(old)
            raise
    return [{'previous': str(old), 'current': str(new)} for old, new in moved]
