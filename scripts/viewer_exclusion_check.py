#!/usr/bin/env python3
"""Verify saved synthetic pixels exclude Replay in a private Hyprland session.

Only private headless outputs are captured. No host compositor configuration,
personal history, or user service is changed. Keep results under ignored runs/.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

from headless_capture_check import run, stop

ROOT = Path(__file__).resolve().parents[1]


def wait_for(predicate, explanation, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise RuntimeError(explanation)


def pixels(path):
    header = path.read_bytes()[:24]
    if header[:8] != b'\x89PNG\r\n\x1a\n':
        raise RuntimeError('Expected an extracted PNG')
    width, height = struct.unpack('>II', header[16:24])
    decoded = subprocess.run(['ffmpeg', '-v', 'error', '-threads', '1', '-i', str(path),
                              '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                             capture_output=True, check=True, timeout=10).stdout
    if len(decoded) != width * height * 3:
        raise RuntimeError('Incomplete extracted image')
    return width, height, decoded


def inspect_rect(image, rect, black):
    width, height, rgb = image
    left, top, right, bottom = rect
    left, top = max(0, int(left) + 12), max(0, int(top) + 12)
    right, bottom = min(width, int(right) - 12), min(height, int(bottom) - 12)
    if right <= left or bottom <= top:
        raise RuntimeError('Window has no testable pixels on the requested output')
    if black:
        # Inspect every retained interior pixel; sampling could miss leaked text.
        for y in range(top, bottom):
            row = rgb[(y * width + left) * 3:(y * width + right) * 3]
            if row.strip(b'\0'):
                raise RuntimeError('Viewer content leaked: non-black interior pixels on row ' + str(y))
        count = (right - left) * (bottom - top)
        return {'pixels_checked': count, 'black_pixels': count, 'complete_interior': True}
    samples = [rgb[(y * width + x) * 3:(y * width + x) * 3 + 3]
               for y in range(top, bottom, max(1, (bottom - top) // 19))
               for x in range(left, right, max(1, (right - left) // 29))]
    black_count = sum(pixel == b'\0\0\0' for pixel in samples)
    if not black and black_count > len(samples) * .5:
        raise RuntimeError('Ordinary synthetic fixture was hidden or missing')
    return {'samples': len(samples), 'black_samples': black_count}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/replay')
    parser.add_argument('--dir', type=Path, required=True)
    parser.add_argument('--capture-exclusions', action='store_true',
                        help='Verify generated app/title capture masks and removing a custom mask.')
    args = parser.parse_args()
    binary = str(args.binary.resolve(strict=True))
    result_dir = args.dir.resolve()
    if result_dir.exists():
        parser.error('Result directory already exists')
    for tool in ('Hyprland', 'hyprctl', 'dbus-run-session', 'ffmpeg'):
        if not shutil.which(tool):
            parser.error(tool + ' is required')
    parent_display = Path(os.environ.get('WAYLAND_DISPLAY', ''))
    if not parent_display.is_absolute():
        parent_display = Path(os.environ.get('XDG_RUNTIME_DIR', '/nonexistent')) / parent_display
    if not parent_display.is_socket():
        parser.error('A parent Wayland session is needed for the isolated renderer')
    result_dir.mkdir(parents=True, mode=0o700)
    report = {'host_screen_captured': False, 'data_origin': 'synthetic-private-wayland-outputs',
              'backend': 'native-ext-image-copy-capture', 'checks': [], 'passed': False}
    processes = []
    with tempfile.TemporaryDirectory(prefix='re-') as temporary:
        base = Path(temporary)
        config_home = base / 'config'
        config = config_home / 'hypr/hyprland.lua'
        config.parent.mkdir(parents=True)
        for name in ('data', 'cache', 'state'):
            (base / name).mkdir()
        config.write_text('''hl.monitor({ output = "", mode = "1280x720@30", position = "auto", scale = 1 })
hl.config({
  misc = { disable_hyprland_logo = true, disable_splash_rendering = true, force_default_wallpaper = 0 },
  animations = { enabled = false },
  decoration = { rounding = 0, blur = { enabled = false }, shadow = { enabled = false } },
  general = { gaps_in = 0, gaps_out = 0, border_size = 0 },
  input = { follow_mouse = 0 },
  xwayland = { enabled = false },
})
''')
        env = dict(os.environ, WAYLAND_DISPLAY=str(parent_display), XDG_RUNTIME_DIR=str(base),
                   XDG_CONFIG_HOME=str(config_home), XDG_DATA_HOME=str(base / 'data'),
                   XDG_CACHE_HOME=str(base / 'cache'), XDG_STATE_HOME=str(base / 'state'),
                   LIBSEAT_BACKEND='replay-disabled', AQ_DRM_DEVICES='/dev/null',
                   HYPRLAND_NO_SD_VARS='1', HYPRLAND_NO_SD_NOTIFY='1',
                   HYPRLAND_NO_CRASHREPORTER='1', QT_QPA_PLATFORM='wayland',
                   QT_WAYLAND_DISABLE_WINDOWDECORATION='1', OMP_THREAD_LIMIT='1')
        for key in ('HYPRLAND_INSTANCE_SIGNATURE', 'NOTIFY_SOCKET', 'WAYLAND_SOCKET', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(key, None)
        logs = []
        def launch(command, name, wayland_debug=False):
            stream = (result_dir / (name + '.log')).open('w')
            logs.append(stream)
            process_env = dict(env, WAYLAND_DEBUG='client') if wayland_debug else env
            process = subprocess.Popen(command, env=process_env, stdout=stream, stderr=stream, start_new_session=True)
            processes.append(process)
            return process

        def clients():
            return json.loads(run(['hyprctl', '-j', 'clients'], env))

        def window(pid):
            return next((item for item in clients() if item['pid'] == pid and item['mapped']), None)

        def dispatch(name, **arguments):
            table = ', '.join(key + '=' + json.dumps(value) for key, value in arguments.items())
            run(['hyprctl', 'dispatch', 'hl.dsp.' + name + '({' + table + '})'], env)
            time.sleep(.15)

        def outputs():
            monitors = json.loads(run(['hyprctl', '-j', 'monitors'], env))
            if {monitor['name'] for monitor in monitors} != {'REPLAY-TEST', 'REPLAY-OTHER'}:
                raise RuntimeError('Private output isolation no longer holds')
            return {monitor['name']: monitor for monitor in monitors}

        def capture(name, output='REPLAY-TEST'):
            monitors = outputs()
            dataset = result_dir / (name + '-history')
            stats = json.loads(run([binary, 'record', '--dir', str(dataset), '--output', output,
                                    '--backend', 'native', '--codec', 'webp', '--duration', '1',
                                    '--interval', '2', '--no-ocr', '--max-mib', '64'], env, timeout=8))
            if stats['samples_attempted'] != 1 or stats.get('capture_timeouts'):
                raise RuntimeError('Capture did not retain exactly one observation')
            frames = json.loads(run([binary, 'list', '--dir', str(dataset)], env))
            image_path = result_dir / (name + '.png')
            run([binary, 'extract', '--dir', str(dataset), '--id', str(frames[0]['id']), '--out', str(image_path)], env)
            return pixels(image_path), monitors[output]

        def rectangle(client, monitor):
            scale = monitor['scale']
            x, y = client['at']
            width, height = client['size']
            return ((x - monitor['x']) * scale, (y - monitor['y']) * scale,
                    (x + width - monitor['x']) * scale, (y + height - monitor['y']) * scale)

        def check(name, pids, ordinary=None, output='REPLAY-TEST'):
            image, monitor = capture(name, output)
            metrics = [inspect_rect(image, rectangle(window(pid), monitor), True) for pid in pids]
            if ordinary:
                metrics.append(inspect_rect(image, rectangle(window(ordinary), monitor), False))
            report['checks'].append({'name': name, 'output': output, 'regions': metrics})

        def check_popup(name, process, output='REPLAY-TEST'):
            address = 'address:' + window(process.pid)['address']
            popup_pattern = r'xdg_popup[#@]\d+\.configure\((-?\d+),\s*(-?\d+),\s*(\d+),\s*(\d+)\)'
            def configurations():
                return re.findall(popup_pattern, (result_dir / 'viewer-one.log').read_text())
            previous = len(configurations())
            dispatch('focus', window=address)
            dispatch('send_shortcut', mods='CTRL', key='f', window=address)
            dispatch('send_shortcut', mods='', key='Menu', window=address)
            def popup_ready():
                found = configurations()
                return tuple(map(int, found[-1])) if len(found) > previous else None
            popup_x, popup_y, popup_width, popup_height = wait_for(popup_ready, 'Search context menu did not map')
            image, monitor = capture(name, output)
            parent = window(process.pid)
            popup = {'at': [parent['at'][0] + popup_x, parent['at'][1] + popup_y],
                     'size': [popup_width, popup_height]}
            report['checks'].append({'name': name, 'popup_configured': True,
                                     'regions': [inspect_rect(image, rectangle(popup, monitor), True)]})
            dispatch('send_shortcut', mods='', key='Escape', window=address)

        try:
            compositor = launch(['dbus-run-session', '--', 'Hyprland', '--config', str(config)], 'compositor')
            def sockets_ready():
                sockets = list(base.glob('hypr/*/.socket.sock'))
                displays = [path for path in base.glob('wayland-*') if path.is_socket()]
                if compositor.poll() is not None:
                    raise RuntimeError('Private compositor exited during startup')
                return (sockets[0], displays[0]) if sockets and displays else None
            socket, display = wait_for(sockets_ready, 'Private compositor startup timed out', 10)
            env['HYPRLAND_INSTANCE_SIGNATURE'] = socket.parent.name
            env['WAYLAND_DISPLAY'] = str(display)
            for output in ('REPLAY-TEST', 'REPLAY-OTHER'):
                run(['hyprctl', 'output', 'create', 'headless', output], env)
            # Persist private output rules so config reload preserves isolation.
            with config.open('a') as stream:
                stream.write('hl.monitor({ output = "WAYLAND-1", disabled = true })\n'
                             'hl.monitor({ output = "REPLAY-TEST", mode = "1920x1080@30", position = "0x0", scale = 1 })\n'
                             'hl.monitor({ output = "REPLAY-OTHER", mode = "1920x1080@30", position = "1920x0", scale = 1 })\n')
            run(['hyprctl', 'reload'], env)
            wait_for(lambda: len(json.loads(run(['hyprctl', '-j', 'monitors'], env))) == 2,
                     'Parent-facing output did not disable')
            outputs()
            run(['hyprctl', 'dismissnotify', '-1'], env)
            install_cmd = [sys.executable, str(ROOT / 'scripts/install_viewer_exclusion.py'), '--config-home', str(config_home)]
            if args.capture_exclusions:
                policy = base / 'replay.toml'
                policy.write_text('[exclusions]\napps=[]\n')
                install_cmd = [sys.executable, str(ROOT / 'scripts/install_capture_exclusions.py'),
                               '--config-home', str(config_home), '--config', str(policy),
                               '--instance', env['HYPRLAND_INSTANCE_SIGNATURE']]
            report['installation'] = json.loads(run(install_cmd, env))
            repeated = json.loads(run(install_cmd, env))
            if repeated['changed']:
                raise RuntimeError('Repeated installation was not idempotent')
            report['repeated_install_unchanged'] = True
            source = result_dir / 'synthetic-source'
            run([binary, 'demo', '--dir', str(source), '--frames', '1', '--static', '--no-ocr', '--codec', 'webp'], env)
            dispatch('focus', monitor='REPLAY-TEST')
            fixture = launch([binary, 'fixture', '--duration', '120', '--interval', '60'], 'fixture')
            fixture_window = wait_for(lambda: window(fixture.pid), 'Fixture did not map')
            if fixture_window['class'] != 'omarchy-replay-fixture':
                raise RuntimeError('Fixture app identity is not distinct')
            image, monitor = capture('fixture-baseline')
            report['checks'].append({'name': 'ordinary-fixture-recorded',
                                     'regions': [inspect_rect(image, rectangle(fixture_window, monitor), False)]})
            first = launch([binary, 'view', '--dir', str(source)], 'viewer-one', wayland_debug=True)
            first_window = wait_for(lambda: window(first.pid), 'Viewer did not map')
            if first_window['class'] != 'omarchy-replay':
                raise RuntimeError('Viewer app identity does not match its rule')
            check('tiled-viewer', [first.pid], fixture.pid)
            check_popup('viewer-context-menu', first)
            dispatch('focus', window='address:' + window(fixture.pid)['address'])
            check('unfocused-viewer', [first.pid], fixture.pid)
            second = launch([binary, 'view', '--dir', str(source)], 'viewer-two')
            wait_for(lambda: window(second.pid), 'Second viewer did not map')
            check('multiple-viewers', [first.pid, second.pid])
            stop(second)
            wait_for(lambda: not window(second.pid), 'Second viewer did not close')
            address = 'address:' + window(first.pid)['address']
            dispatch('window.float', window=address, action='enable')
            dispatch('window.resize', x=1000, y=700, relative=False, window=address)
            dispatch('window.move', x=1650, y=100, relative=False, window=address)
            check('straddling-first-output', [first.pid])
            check('straddling-second-output', [first.pid], output='REPLAY-OTHER')
            dispatch('window.move', x=2150, y=100, relative=False, window=address)
            check('moved-to-second-output', [first.pid], output='REPLAY-OTHER')
            check('first-output-still-recorded', [], fixture.pid)
            run(['hyprctl', 'reload'], env)
            if run(['hyprctl', 'configerrors'], env).strip():
                raise RuntimeError('Private reload introduced configuration errors')
            time.sleep(.2)
            check('survives-config-reload', [first.pid], output='REPLAY-OTHER')
            with config.open('a') as stream:
                stream.write('hl.monitor({ output = "REPLAY-OTHER", mode = "1920x1080@30", position = "1920x0", scale = 1.25 })\n')
            run(['hyprctl', 'reload'], env)
            time.sleep(.2)
            check('fractional-scale', [first.pid], output='REPLAY-OTHER')
            check_popup('fractional-scale-context-menu', first, output='REPLAY-OTHER')
            stop(first)
            wait_for(lambda: not window(first.pid), 'First viewer did not close')
            dispatch('focus', monitor='REPLAY-OTHER')
            reopened = launch([binary, 'view', '--dir', str(source)], 'viewer-reopened')
            wait_for(lambda: window(reopened.pid), 'Reopened viewer did not map')
            check('first-capture-after-reopen', [reopened.pid], output='REPLAY-OTHER')
            if args.capture_exclusions:
                policy.write_text('[exclusions]\napps=[]\n[[exclusions.windows]]\n'
                                  'app_id="omarchy-replay-fixture"\ntitle_regex="."\n')
                report['custom_mask_installation'] = json.loads(run(install_cmd, env))
                check('custom-app-title-mask', [fixture.pid])
                policy.write_text('[exclusions]\napps=[]\n')
                run(install_cmd, env)
                check('custom-mask-removal-restores-fixture', [], fixture.pid)
            report['passed'] = True
        except BaseException as error:
            report['error'] = str(error)
            raise
        finally:
            for process in reversed(processes):
                stop(process)
            for stream in logs:
                stream.close()
            report['all_owned_processes_stopped'] = all(process.poll() is not None for process in processes)
            (result_dir / 'check.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    os.umask(0o077)
    main()
