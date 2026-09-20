#!/usr/bin/env python3
"""Install Replay's user service and launcher. Recording and login startup remain opt-in."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tomllib
import tempfile

from install_viewer_exclusion import atomic_write, backup, hyprctl, lua_string, validate
from open_replay import desktop_entry, xdg_path
from install_viewer_exclusion import install as install_rule
from migrate_replay_paths import migrate, migration_plan

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '-- BEGIN Omarchy Replay summon'
END = '-- END Omarchy Replay summon'


def unit_text(binary):
    # systemd argument quoting is distinct from shell quoting.
    quoted = '"' + str(binary).replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%').replace('$', '$$') + '"'
    return ('[Unit]\nDescription=Replay screen history\nAfter=graphical-session.target\n'
            'PartOf=graphical-session.target\nStartLimitIntervalSec=60\nStartLimitBurst=3\n\n'
            '[Service]\nType=simple\n' + f'ExecStart={quoted} daemon run\n'
            'Restart=on-failure\nRestartSec=5\nTimeoutStopSec=55\nKillMode=control-group\n'
            'UMask=0077\nNice=10\nIOSchedulingClass=idle\n'
            'Environment=OMP_THREAD_LIMIT=1\nStandardOutput=null\nStandardError=journal\n\n'
            '[Install]\nWantedBy=graphical-session.target\n')


def managed_write(path, text, marker, mode):
    data = text.encode()
    if path.is_symlink():
        raise RuntimeError(f'Refusing to replace symlink: {path}')
    if path.exists():
        old = path.read_bytes()
        if old == data:
            return
        if marker.encode() not in old:
            raise RuntimeError(f'Existing file is not managed by Replay: {path}')
        backup(path)
    atomic_write(path, data, mode)


def install_shortcut(config):
    path = config / 'hypr/bindings.lua'
    if path.is_symlink():
        raise RuntimeError(f'Refusing to replace symlink: {path}')
    if not path.is_file():
        raise RuntimeError('Expected Omarchy Lua bindings; install with --no-shortcut to skip the shortcut.')
    original = path.read_text()
    if BEGIN not in original:
        binds = json.loads(hyprctl('-j', 'binds'))
        if any(b.get('modmask') == 72 and str(b.get('key', '')).upper() == 'R' for b in binds):
            raise RuntimeError('Super+Alt+R is already bound; use --no-shortcut and choose your own binding.')
    if BEGIN in original or END in original:
        if original.count(BEGIN) != 1 or original.count(END) != 1:
            raise RuntimeError('Replay shortcut markers are ambiguous.')
        before, remainder = original.split(BEGIN)
        _, after = remainder.split(END)
        base = before.rstrip() + '\n' + after.lstrip('\n')
    else:
        base = original
    command = shlex.quote(str(ROOT / 'scripts/replay')) + ' open --toggle --notify-errors'
    updated = base.rstrip() + '\n\n' + BEGIN + '\n' + 'o.bind("SUPER + ALT + R", "Replay", ' + lua_string(command) + ')\n' + END + '\n'
    validate()
    if path.is_symlink() or path.read_text() != original:
        raise RuntimeError('Bindings changed while preparing Replay shortcut; retry after reviewing the file.')
    if updated != original:
        backup(path)
        atomic_write(path, updated.encode(), path.stat().st_mode & 0o777)
    try:
        hyprctl('reload'); validate()
    except BaseException:
        if not path.is_symlink() and path.read_text() == updated:
            atomic_write(path, original.encode(), path.stat().st_mode & 0o777)
            hyprctl('reload')
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', help='selected display for initial configuration')
    parser.add_argument('--no-shortcut', action='store_true')
    args = parser.parse_args(argv)
    try:
        binary = ROOT / 'build/replay'
        if not binary.is_file():
            raise RuntimeError('Build Replay first: ./scripts/replay build')
        config = xdg_path('XDG_CONFIG_HOME', Path.home() / '.config')
        data = xdg_path('XDG_DATA_HOME', Path.home() / '.local/share')
        state = xdg_path('XDG_STATE_HOME', Path.home() / '.local/state')
        cache = xdg_path('XDG_CACHE_HOME', Path.home() / '.cache')
        runtime = os.environ.get('XDG_RUNTIME_DIR', '')
        if runtime and Path(runtime).is_absolute():
            old_runtime, new_runtime = Path(runtime) / 'oma-rewind', Path(runtime) / 'omarchy-replay'
        else:
            old_runtime = Path(tempfile.gettempdir()) / f'oma-rewind-user-{os.getuid()}' / 'oma-rewind'
            new_runtime = Path(tempfile.gettempdir()) / f'omarchy-replay-user-{os.getuid()}' / 'omarchy-replay'
        pairs = [(base / 'oma-rewind', base / 'omarchy-replay') for base in (config, data, state, cache)]
        pairs.append((old_runtime, new_runtime))
        migration_plan(pairs)
        legacy_unit = config / 'systemd/user/oma-rewind.service'
        unit = config / 'systemd/user/omarchy-replay.service'
        resume_coordinator = False
        if unit.exists():
            if unit.is_symlink() or '# Managed by Omarchy Replay' not in unit.read_text():
                raise RuntimeError('The service file is not managed by Replay.')
            resume_coordinator = subprocess.run(['systemctl', '--user', 'is-active', '--quiet', 'omarchy-replay.service'], timeout=5).returncode == 0
            if resume_coordinator:
                subprocess.run(['systemctl', '--user', 'stop', 'omarchy-replay.service'], check=True, timeout=60)
        if legacy_unit.exists():
            if legacy_unit.is_symlink() or '# Managed by Omarchy Replay' not in legacy_unit.read_text():
                raise RuntimeError('The legacy service file is not managed by Replay.')
            legacy_active = subprocess.run(['systemctl', '--user', 'is-active', '--quiet', 'oma-rewind.service'], timeout=5).returncode == 0
            resume_coordinator = resume_coordinator or legacy_active
            subprocess.run(['systemctl', '--user', 'stop', 'oma-rewind.service'], check=True, timeout=60)
        migrated = migrate(pairs)
        arguments = [str(binary), 'daemon', 'init']
        if args.output:
            arguments += ['--output', args.output]
        subprocess.run(arguments, check=True, capture_output=True, timeout=10)
        managed_write(unit, '# Managed by Omarchy Replay\n' + unit_text(binary), '# Managed by Omarchy Replay', 0o600)
        entry = data / 'applications/omarchy-replay.desktop'
        entry_marker = 'Name=Omarchy Replay\n'
        if entry.exists() and 'Name=Replay\n' in entry.read_text():
            entry_marker = 'Name=Replay\n'
        managed_write(entry, desktop_entry(), entry_marker, 0o644)
        if legacy_unit.exists():
            subprocess.run(['systemctl', '--user', 'disable', 'oma-rewind.service'], check=True, capture_output=True, timeout=5)
            backup(legacy_unit)
            legacy_unit.unlink()
        subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True, timeout=5)
        settings = tomllib.loads((config / 'omarchy-replay/config.toml').read_text())
        login = settings.get('service', {}).get('login_startup', False)
        subprocess.run(['systemctl', '--user', 'enable' if login else 'disable', 'omarchy-replay.service'],
                       check=True, capture_output=True, timeout=5)
        window_rule = install_rule(config, source=ROOT / 'config/hypr/replay-window.lua', filename='replay-window.lua',
                                   begin='-- BEGIN Omarchy Replay window', end='-- END Omarchy Replay window')
        install_rule(config)
        if not args.no_shortcut:
            install_shortcut(config)
        if resume_coordinator:
            subprocess.run(['systemctl', '--user', 'start', 'omarchy-replay.service'], check=True, timeout=10)
        print(json.dumps({'migrated': migrated, 'window_rule_validated': window_rule['validated'], 'installed': True,
                          'coordinator_restarted': resume_coordinator, 'login_startup': login,
                          'shortcut': None if args.no_shortcut else 'Super+Alt+R', 'unit': str(unit)}, indent=2))
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f'Replay install: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
