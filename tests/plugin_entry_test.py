#!/usr/bin/env python3
"""Load the real bar entry in private Hyprland with a fictional backend."""
import argparse
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from headless_capture_check import run, stop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dir')
    args = parser.parse_args()
    if not args.dir:
        if os.environ.get('REPLAY_TEST_PLUGIN_SCOPE') != '1':
            print('SKIP: set REPLAY_TEST_PLUGIN_SCOPE=1 for isolated Omarchy plugin checks')
            return 77
        with tempfile.TemporaryDirectory(prefix='replay-plugin-native-') as temp:
            return subprocess.run([sys.executable, '-B', __file__, '--dir', str(Path(temp) / 'proof')], timeout=35).returncode
    output = Path(args.dir).resolve()
    if output.exists():
        parser.error('output directory must be new')
    for tool in ('Hyprland', 'hyprctl', 'qs', 'grim', 'dbus-run-session'):
        if not shutil.which(tool):
            print(f'SKIP: {tool} required')
            return 77
    parent = Path(os.environ.get('WAYLAND_DISPLAY', ''))
    if not parent.is_absolute():
        parent = Path(os.environ.get('XDG_RUNTIME_DIR', '/nonexistent')) / parent
    if not parent.is_socket():
        print('SKIP: parent Wayland display required for isolated compositor')
        return 77
    output.mkdir(parents=True, mode=0o700)
    compositor = shell_process = None
    with tempfile.TemporaryDirectory(prefix='rp-p-') as temp:
        base = Path(temp)
        for name in ('config', 'data', 'state', 'cache', 'scripts'):
            (base / name).mkdir(mode=0o700)
        for name in ('Commons', 'Ui'):
            (base / name).symlink_to(Path('/usr/share/omarchy/shell') / name, target_is_directory=True)
        shutil.copytree(ROOT / 'plugin', base / 'plugin')
        shutil.copy2(ROOT / 'tests/plugin/EntryCheck.qml', base / 'shell.qml')
        (base / 'scripts/plugin_control.py').write_text(
            'import sys,json,time\nfrom pathlib import Path\n'
            'with (Path(__file__).parent/"calls").open("a") as f:f.write(sys.argv[1]+"\\n")\n'
            'if sys.argv[1]=="status":time.sleep(.3)\n'
            'print(json.dumps({"installed":True,"service_running":True,"intent":"stopped","state":"stopped"}))\n')
        config = base / 'hyprland.conf'
        config.write_text('monitor = , 1280x720@30, auto, 1\nmisc {\n disable_hyprland_logo = true\n disable_splash_rendering = true\n}\nanimations {\n enabled = false\n}\nxwayland {\n enabled = false\n}\n')
        env = dict(os.environ, HOME=temp, WAYLAND_DISPLAY=str(parent), XDG_RUNTIME_DIR=temp,
                   XDG_CONFIG_HOME=str(base / 'config'), XDG_DATA_HOME=str(base / 'data'),
                   XDG_STATE_HOME=str(base / 'state'), XDG_CACHE_HOME=str(base / 'cache'),
                   QT_QPA_PLATFORM='wayland', QT_QPA_PLATFORMTHEME='generic',
                   LIBSEAT_BACKEND='replay-disabled', AQ_DRM_DEVICES='/dev/null',
                   HYPRLAND_NO_SD_VARS='1', HYPRLAND_NO_SD_NOTIFY='1', HYPRLAND_NO_CRASHREPORTER='1')
        env['PATH'] = str(Path(sys.executable).resolve().parent) + os.pathsep + env.get('PATH', '')
        for key in ('HYPRLAND_INSTANCE_SIGNATURE', 'NOTIFY_SOCKET', 'WAYLAND_SOCKET', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(key, None)
        try:
            with (output / 'compositor.log').open('w') as stream:
                compositor = subprocess.Popen(['dbus-run-session', '--', 'Hyprland', '--config', str(config)],
                    env=env, stdout=stream, stderr=stream, start_new_session=True)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    sockets = list(base.glob('hypr/*/.socket.sock'))
                    displays = [p for p in base.glob('wayland-*') if p.is_socket()]
                    if sockets and displays:
                        break
                    time.sleep(.05)
                else:
                    raise RuntimeError('Private compositor did not start')
                env['HYPRLAND_INSTANCE_SIGNATURE'] = sockets[0].parent.name
                env['WAYLAND_DISPLAY'] = str(displays[0])
                run(['hyprctl', 'output', 'create', 'headless', 'REPLAY-PLUGIN-TEST'], env)
                run(['hyprctl', 'keyword', 'monitor', 'REPLAY-PLUGIN-TEST,1280x720@30,0x0,1'], env)
                run(['hyprctl', 'keyword', 'monitor', 'WAYLAND-1,disable'], env)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    monitors = json.loads(run(['hyprctl', '-j', 'monitors'], env))
                    if len(monitors) == 1 and monitors[0]['name'] == 'REPLAY-PLUGIN-TEST':
                        break
                    time.sleep(.05)
                else:
                    raise RuntimeError('Private output not isolated')
                shell_process = subprocess.Popen(['qs', '--no-color', '-p', str(base / 'shell.qml')],
                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
                lines = []
                captured = False
                deadline = time.monotonic() + 12
                with selectors.DefaultSelector() as selector:
                    selector.register(shell_process.stdout, selectors.EVENT_READ)
                    while time.monotonic() < deadline:
                        ready = selector.select(.1)
                        if ready:
                            line = shell_process.stdout.readline()
                            if not line:
                                break
                            lines.append(line)
                            if 'REPLAY_PLUGIN_ENTRY_OPEN' in line:
                                monitors = json.loads(run(['hyprctl', '-j', 'monitors'], env))
                                if len(monitors) != 1 or monitors[0]['name'] != 'REPLAY-PLUGIN-TEST':
                                    raise RuntimeError('Private output isolation changed')
                                time.sleep(.15)
                                run(['grim', '-o', 'REPLAY-PLUGIN-TEST', str(output / 'panel.png')], env)
                                captured = True
                        elif shell_process.poll() is not None:
                            lines.append(shell_process.stdout.read())
                            break
                shell_process.wait(timeout=3)
                log = ''.join(lines)
                (output / 'check.log').write_text(log)
                calls = (base / 'scripts/calls').read_text().splitlines() if (base / 'scripts/calls').exists() else []
                passed = shell_process.returncode == 0 and 'REPLAY_PLUGIN_ENTRY_PASS' in log and captured
                passed = passed and calls.count('start') == 1 and set(calls) <= {'status', 'start'}
                report = {'passed': passed, 'host_screen_captured': False, 'real_bridge_executed': False,
                          'calls': calls, 'checks': ['real bar entry loads', 'queued action during poll',
                                                  'shell open/close', 'unload', 'native popup render']}
                (output / 'check.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(report, indent=2))
                if not passed:
                    print(log[-4000:])
                return 0 if passed else 1
        finally:
            stop(shell_process)
            stop(compositor)


if __name__ == '__main__':
    raise SystemExit(main())
