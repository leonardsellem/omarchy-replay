#!/usr/bin/env python3
"""Open Replay's latest saved history. Never starts a recording."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from runtime_layout import ROOT, read_json, selected_trial
from viewer_launch import launch_viewer


def xdg_path(name, fallback):
    value = os.environ.get(name, '')
    return Path(value) if value and Path(value).is_absolute() else fallback


def desktop_entry(executable=None):
    # Desktop Entry quoting is deliberately separate from shell quoting.
    executable = str(executable or ROOT / 'scripts/replay')
    for character in ('\\', '"', '`', '$'):
        executable = executable.replace(character, '\\' + character)
    executable = executable.replace('%', '%%')
    return ('[Desktop Entry]\nType=Application\nName=Omarchy Replay\n'
            'Comment=Find a moment in your saved screen history\n'
            f'Exec="{executable}" open --notify-errors\n'
            'Icon=edit-find\nTerminal=false\nCategories=Utility;\n'
            'StartupWMClass=omarchy-replay\nKeywords=history;recall;timeline;\n')


def notify_error(message):
    try:
        subprocess.run(['notify-send', '--app-name=Replay', '--urgency=normal',
                        'Replay could not open', message], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=2, check=False)
    except (OSError, subprocess.TimeoutExpired):
        pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--dir', type=Path, help='an existing dataset instead of the latest trial')
    selection.add_argument('--trial', type=Path, help='a previous trial directory')
    parser.add_argument('--runs-dir', type=Path)
    parser.add_argument('--toggle', action='store_true', help='dismiss this history if its viewer is focused')
    parser.add_argument('--settings', action='store_true', help='open Replay settings')
    parser.add_argument('--binary', type=Path, default=ROOT / 'scripts/replay', help=argparse.SUPPRESS)
    parser.add_argument('--notify-errors', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--print-desktop-entry', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.print_desktop_entry:
        print(desktop_entry(), end='')
        return 0
    try:
        explicit_runs = args.runs_dir is not None
        args.runs_dir = (args.runs_dir or ROOT / 'runs/trials').expanduser().resolve()
        args.binary = args.binary.expanduser().resolve()
        if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
            raise RuntimeError('Replay executable is missing or is not executable.')
        shared = xdg_path('XDG_DATA_HOME', Path.home() / '.local/share') / 'omarchy-replay/history'
        config_file = xdg_path('XDG_CONFIG_HOME', Path.home() / '.config') / 'omarchy-replay/config.toml'
        legacy = xdg_path('XDG_CONFIG_HOME', Path.home() / '.config') / 'oma-rewind/config.toml'
        if not config_file.exists() and legacy.exists() and not args.dir and not args.trial and not explicit_runs:
            raise RuntimeError('Update Replay paths first: ./scripts/replay install. Your existing history will be preserved.')
        if not args.dir and not args.trial and not explicit_runs and (shared.exists() or config_file.exists()):
            resolved = subprocess.run([str(args.binary), 'daemon', 'paths'], capture_output=True, timeout=8)
            if resolved.returncode:
                raise RuntimeError('Could not resolve Replay history: ' + resolved.stderr.decode(errors='replace')[-500:])
            locations = json.loads(resolved.stdout)
            if locations.get('config_error'):
                print('Replay: current settings are invalid; opening the last accepted history. '
                      + str(locations['config_error']), file=sys.stderr)
            shared = Path(locations['history'])
            if not shared.is_absolute():
                raise RuntimeError('Replay returned a non-absolute history directory.')
            if not (shared / 'index.sqlite').is_file():
                initialized = subprocess.run([str(args.binary), 'daemon', 'init'], capture_output=True, timeout=8)
                if initialized.returncode:
                    raise RuntimeError('Could not initialize shared history: ' + initialized.stderr.decode(errors='replace')[-500:])
            dataset, metadata = shared, None
        elif args.dir:
            dataset = args.dir.expanduser().resolve()
            # An explicit trial dataset retains the same settings as opening its trial.
            metadata = read_json(dataset.parent / 'trial.json') if dataset.name == 'dataset' else None
        else:
            trial = selected_trial(args)
            dataset = trial / 'dataset'
            metadata = read_json(trial / 'trial.json')
        config = metadata.get('config', {}) if isinstance(metadata, dict) else {}
        options = {}
        if args.toggle:
            options['toggle'] = True
        if args.settings:
            options['settings'] = True
        result = launch_viewer(args.binary, dataset, config, **options)
        if result:
            raise RuntimeError(f'The viewer exited with code {result}. Run {args.binary} open from a terminal for details.')
        return 0
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as exception:
        message = str(exception)
        print(f'Replay: {message}', file=sys.stderr)
        if args.notify_errors:
            notify_error(message)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
