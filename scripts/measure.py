#!/usr/bin/env python3
"""Bounded sampling of a process tree and an explicitly identified Replay worker."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import time


def process_sample(pid, proc_root=Path('/proc')):
    try:
        base = proc_root / str(pid)
        stat = (base / 'stat').read_text().rsplit(')', 1)[1].split()
        values = {}
        for line in (base / 'smaps_rollup').read_text().splitlines():
            if ':' in line:
                key, value = line.split(':', 1)
                if key in ('Rss', 'Pss'):
                    values[key] = int(value.split()[0]) * 1024
        io = dict(line.split(': ') for line in (base / 'io').read_text().splitlines())
        children = (base / 'task' / str(pid) / 'children').read_text().split()
        return {
            'pid': pid, 'start': stat[19], 'cpu_ticks': int(stat[11]) + int(stat[12]),
            'rss': values.get('Rss', 0), 'pss': values.get('Pss', 0),
            'write_bytes': int(io.get('write_bytes', 0)),
            'read_bytes': int(io.get('read_bytes', 0)),
            'children': [int(child) for child in children],
        }
    except (OSError, ValueError, IndexError, ProcessLookupError):
        return None


def _private_directory(path):
    value = path.lstat()
    return stat.S_ISDIR(value.st_mode) and value.st_uid == os.geteuid() and not value.st_mode & 0o077


def _worker_receipt(dataset, environment):
    """Read the same private per-dataset runtime file as Replay's coordinator."""
    base = Path(environment.get('XDG_RUNTIME_DIR') or f'/run/user/{os.geteuid()}')
    try:
        private = _private_directory(base)
    except OSError:
        private = False
    if not private:
        base = Path(f'/dev/shm/omarchy-replay-{os.geteuid()}')
    app = base / 'replay'
    runtime = app / hashlib.sha256(str(dataset).encode()).hexdigest()
    if not all(_private_directory(path) for path in (base, app, runtime)):
        return None
    fd = os.open(runtime / 'worker.json', os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        value = os.fstat(stream.fileno())
        if not stat.S_ISREG(value.st_mode) or value.st_uid != os.geteuid() or value.st_mode & 0o077:
            return None
        raw = stream.read(65537)
    if len(raw) > 65536:
        return None
    receipt = json.loads(raw)
    return receipt if isinstance(receipt, dict) else None


def _dataset_command(base, dataset, commands):
    args = (base / 'cmdline').read_bytes().split(b'\0')
    if len(args) < 4 or args[1] not in commands:
        return False
    directories = []
    for offset, arg in enumerate(args):
        if arg == b'--dir' and offset + 1 < len(args):
            directories.append(os.fsdecode(args[offset + 1]))
        elif arg.startswith(b'--dir='):
            directories.append(os.fsdecode(arg[len(b'--dir='):]))
    return (len(directories) == 1 and Path(directories[0]).is_absolute() and
            Path(directories[0]).resolve() == dataset)


def owned_index_worker(dataset, binary, owners, environment=None, proc_root=Path('/proc')):
    """Resolve one receipt-backed worker; never search /proc or signal a process.

    A service worker has a different parent from the recorder. Its receipt must
    identify an owner already sampled in the command's tree, including start
    times for both processes so stale receipts and recycled PIDs cannot match.
    """
    try:
        dataset = Path(dataset).resolve(strict=True)
        binary = Path(binary).resolve(strict=True)
        receipt = _worker_receipt(dataset, os.environ if environment is None else environment)
        if not receipt:
            return None
        pid, owner = receipt.get('pid'), receipt.get('owner_pid')
        if type(pid) is not int or pid <= 0 or type(owner) is not int or owner not in owners:
            return None
        if str(receipt.get('owner_start_ticks')) != str(owners[owner]):
            return None
        base = proc_root / str(pid)
        if not _dataset_command(base, dataset, (b'index',)):
            return None
        if not (base / 'exe').samefile(binary):
            # The normal trial entry point is scripts/replay, which execs the
            # native binary. A script's sampled owner is authoritative only if
            # it runs this dataset's recorder/index command and the worker has
            # the exact same executable inode. Never infer a path from a shell.
            with binary.open('rb') as stream:
                script = stream.read(2) == b'#!'
            owner_base = proc_root / str(owner)
            if (not script or not _dataset_command(owner_base, dataset, (b'record', b'demo', b'index')) or
                    not (base / 'exe').samefile(owner_base / 'exe')):
                return None
        row = process_sample(pid, proc_root)
        if row is None or str(receipt.get('process_start_ticks')) != row['start']:
            return None
        worker_fields = (base / 'stat').read_text().rsplit(')', 1)[1].split()
        if worker_fields[0] == 'Z' or worker_fields[19] != row['start']:
            return None
        # Check the owner's identity again after reading the worker, including
        # command paths that can start or stop while this sample is collected.
        owner_fields = (proc_root / str(owner) / 'stat').read_text().rsplit(')', 1)[1].split()
        if owner_fields[0] == 'Z' or owner_fields[19] != str(owners[owner]):
            return None
        return row
    except (OSError, ValueError, TypeError, IndexError):
        return None


def sampled_processes(pid, *, dataset=None, binary=None, environment=None, proc_root=Path('/proc')):
    seen, rows = set(), []

    def descendants(pending):
        while pending and len(seen) < 256:
            current = pending.pop()
            if current in seen:
                continue
            seen.add(current)
            row = process_sample(current, proc_root)
            if row is not None:
                pending.extend(row['children'])
                rows.append(row)

    descendants([pid])
    if dataset is not None and binary is not None:
        worker = owned_index_worker(dataset, binary, {row['pid']: row['start'] for row in rows}, environment, proc_root)
        if worker is not None and worker['pid'] not in seen and len(seen) < 256:
            seen.add(worker['pid'])
            rows.append(worker)
            descendants(worker['children'])
    return rows


def measure(command, timeout=120, env=None, sample_ms=50, *, dataset=None, binary=None):
    if (dataset is None) != (binary is None):
        raise ValueError('Provide both dataset and binary to sample an independent Replay worker')
    start = time.monotonic()
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=env, start_new_session=True)
    selector = None
    try:
        # Drain pipes continuously, rather than letting output fill and block the child.
        import selectors
        selector = selectors.DefaultSelector()
        for pipe in (proc.stdout, proc.stderr):
            os.set_blocking(pipe.fileno(), False)
            selector.register(pipe, selectors.EVENT_READ)
        output = {proc.stdout: bytearray(), proc.stderr: bytearray()}
        maxima, samples = {}, []
        root_io = {'write_bytes': 0, 'read_bytes': 0}
        timed_out = False
        while proc.poll() is None:
            if time.monotonic() - start > timeout:
                timed_out = True
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                break
            rows = sampled_processes(proc.pid, dataset=dataset, binary=binary, environment=env)
            for row in rows:
                if row['pid'] == proc.pid:
                    for key in root_io:
                        root_io[key] = max(root_io[key], row[key])
                identity = (row['pid'], row['start'])
                previous = maxima.setdefault(identity, {'cpu_ticks': 0, 'write_bytes': 0, 'read_bytes': 0})
                for key in previous:
                    previous[key] = max(previous[key], row[key])
            samples.append({'seconds': time.monotonic() - start,
                            'pss_mib': sum(row['pss'] for row in rows) / 2**20,
                            'rss_mib': sum(row['rss'] for row in rows) / 2**20})
            for key, _ in selector.select(sample_ms / 1000):
                data = key.fileobj.read()
                if data:
                    output[key.fileobj].extend(data)
                    if len(output[key.fileobj]) > 2**20:
                        del output[key.fileobj][:-2**20]
                elif data == b'':
                    selector.unregister(key.fileobj)
        proc.wait()
        # Descendants owned by the finite command must not survive a timeout.
        if timed_out:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for pipe in (proc.stdout, proc.stderr):
            try:
                remaining = pipe.read()
                if remaining:
                    output[pipe].extend(remaining)
            except OSError:
                pass
        elapsed = time.monotonic() - start
        stdout = output[proc.stdout].decode(errors='replace')
        try:
            result = json.loads(stdout)
        except json.JSONDecodeError:
            result = None
        cpu_seconds = sum(row['cpu_ticks'] for row in maxima.values()) / os.sysconf('SC_CLK_TCK')
        steady = [s['pss_mib'] for s in samples if s['seconds'] >= min(3, elapsed / 3)]
        return {
            'command': command, 'returncode': proc.returncode, 'timed_out': timed_out,
            'wall_seconds': elapsed, 'sample_interval_ms': sample_ms,
            'sampled_cpu_seconds_lower_bound': cpu_seconds,
            'sampled_percent_one_cpu_lower_bound': cpu_seconds / elapsed * 100,
            'peak_tree_pss_mib': max((s['pss_mib'] for s in samples), default=0),
            'median_after_warmup_tree_pss_mib': sorted(steady)[len(steady)//2] if steady else 0,
            'sampled_root_write_bytes': root_io['write_bytes'],
            'sampled_root_read_bytes': root_io['read_bytes'],
            'result': result, 'stdout_tail': stdout[-2000:] if result is None else None,
            'stderr_tail': output[proc.stderr].decode(errors='replace')[-2000:],
            'limits': ('Samples cover the command and observed descendants' +
                       (' plus its verified per-dataset Replay service worker. ' if dataset is not None else '. ') +
                       'Sampling can miss short-lived activity and peaks. CPU is a lower bound. Root I/O can include reaped children, excludes independent service workers, and must not be added to child I/O. PSS excludes GPU allocations. No host device-wide attribution.'),
        }
    except BaseException:
        # The command starts a private session; never signal the caller
        # or a process found merely through the resource sampler.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
        raise
    finally:
        if selector is not None:
            selector.close()
        for pipe in (proc.stdout, proc.stderr):
            pipe.close()



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--dataset', type=Path, help='Explicit Replay dataset for independent OCR worker accounting')
    parser.add_argument('--binary', type=Path, help='Expected Replay executable for that dataset')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('provide a command after --')
    if (args.dataset is None) != (args.binary is None):
        parser.error('--dataset and --binary must be supplied together')
    report = measure(command, args.timeout, dataset=args.dataset, binary=args.binary)
    text = json.dumps(report, indent=2) + '\n'
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
    print(text, end='')
    raise SystemExit(report['returncode'] or int(report['timed_out']))
