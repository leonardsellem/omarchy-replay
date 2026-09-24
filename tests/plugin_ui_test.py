#!/usr/bin/env python3
"""Check Replay's native quick actions offscreen; all content is synthetic."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dir', help='new directory for synthetic captures')
    args = parser.parse_args()
    if not args.dir:
        if os.environ.get('REPLAY_TEST_PLUGIN_SCOPE') != '1':
            print('SKIP: set REPLAY_TEST_PLUGIN_SCOPE=1 for isolated Omarchy plugin checks')
            return 77
        with tempfile.TemporaryDirectory(prefix='replay-plugin-check-') as temp:
            return subprocess.run([sys.executable, '-B', __file__, '--dir', str(Path(temp) / 'proof')], timeout=25).returncode
    root = Path(__file__).resolve().parents[1]
    output = Path(args.dir).resolve()
    if output.exists():
        parser.error('output directory must be new')
    shell = Path('/usr/share/omarchy/shell')
    if not shutil.which('qs') or not (shell / 'Ui').is_dir():
        print('SKIP: Quickshell and installed Omarchy shell are required')
        return 77
    output.mkdir(parents=True, mode=0o700)
    with tempfile.TemporaryDirectory(prefix='replay-plugin-ui-') as temp:
        base = Path(temp)
        for name in ('Commons', 'Ui'):
            (base / name).symlink_to(shell / name, target_is_directory=True)
        (base / 'plugin').symlink_to(root / 'plugin', target_is_directory=True)
        shutil.copy2(root / 'tests/plugin/UiCheck.qml', base / 'shell.qml')
        runtime = base / 'runtime'
        runtime.mkdir(mode=0o700)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
                   QT_QUICK_BACKEND='software',
                   HOME=temp, XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(base / 'config'),
                   XDG_STATE_HOME=str(base / 'state'), XDG_CACHE_HOME=str(base / 'cache'),
                   REPLAY_PLUGIN_TEST_OUTPUT=str(output))
        for key in ('WAYLAND_DISPLAY', 'DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(key, None)
        result = subprocess.run(['qs', '--no-color', '-p', str(base / 'shell.qml')],
                                env=env, capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr
        (output / 'check.log').write_text(log)
        passed = (result.returncode == 0 and 'REPLAY_PLUGIN_UI_PASS' in log
                  and 'REPLAY_PLUGIN_UI_FAIL' not in log)
        report = {'passed': passed, 'host_screen_captured': False, 'real_bridge_executed': False,
                  'checks': ['keyboard navigation', 'Enter/Space actions', 'Escape', 'busy gate',
                             'setup', 'paused/update', 'native warning preserves Stop', 'error recovery', 'content bounds'],
                  'images': sorted(p.name for p in output.glob('*.png'))}
        (output / 'check.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        if not passed:
            print(log[-4000:])
        return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
