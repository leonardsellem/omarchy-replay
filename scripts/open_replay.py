#!/usr/bin/env python3
"""Open Replay's latest saved history. Never starts a recording."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from trial import ROOT, read_json, selected_trial
from viewer_launch import launch_viewer


def desktop_entry():
    # Desktop Entry quoting is deliberately separate from shell quoting.
    executable = str(ROOT / 'scripts/replay')
    for character in ('\\', '"', '`', '$'):
        executable = executable.replace(character, '\\' + character)
    executable = executable.replace('%', '%%')
    return ('[Desktop Entry]\nType=Application\nName=Replay\n'
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
    parser.add_argument('--runs-dir', type=Path, default=ROOT / 'runs/trials')
    parser.add_argument('--binary', type=Path, default=ROOT / 'scripts/replay', help=argparse.SUPPRESS)
    parser.add_argument('--notify-errors', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--print-desktop-entry', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.print_desktop_entry:
        print(desktop_entry(), end='')
        return 0
    try:
        args.runs_dir = args.runs_dir.expanduser().resolve()
        args.binary = args.binary.expanduser().resolve()
        if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
            raise RuntimeError('Replay executable is missing or is not executable.')
        if args.dir:
            dataset = args.dir.expanduser().resolve()
            # An explicit trial dataset retains the same settings as opening its trial.
            metadata = read_json(dataset.parent / 'trial.json') if dataset.name == 'dataset' else None
        else:
            trial = selected_trial(args)
            dataset = trial / 'dataset'
            metadata = read_json(trial / 'trial.json')
        config = metadata.get('config', {}) if isinstance(metadata, dict) else {}
        result = launch_viewer(args.binary, dataset, config)
        if result:
            raise RuntimeError(f'The viewer exited with code {result}. Run ./scripts/replay from a terminal for details.')
        return 0
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, ValueError) as exception:
        message = str(exception)
        print(f'Replay: {message}', file=sys.stderr)
        if args.notify_errors:
            notify_error(message)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
