#!/usr/bin/env python3
"""Check privacy masks and Replay-only skips in a private Hyprland session.

All screenshots use grim against one verified, synthetic headless output. The
host desktop, personal history, user config and services are never captured or
changed. The nested compositor briefly opens an empty parent-facing window,
then disables it before any fixtures are started. Requires an existing Wayland
session, Hyprland, mpv (Wayland OpenGL output), grim and ffmpeg. No packages are installed.

Run after building Replay, with a new output directory under ignored runs/:
  python3 scripts/exclusion_scope_check.py --dir runs/exclusion-scope-check

The browser/meeting cases use mpv with explicit synthetic application IDs; this
tests compositor identity matching, not an actual Meet or Zoom call. Recorder
skip decisions are covered separately by recording_environment_test. A native
coordinator also reloads masks while stopped and paused; it is never started
or resumed for recording, and its capture counters must remain zero.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from headless_capture_check import run, stop
from viewer_exclusion_check import inspect_rect, pixels, wait_for


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = 'REPLAY-SCOPE-TEST'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/replay')
    parser.add_argument('--dir', type=Path, required=True)
    args = parser.parse_args()
    binary = str(args.binary.resolve(strict=True))
    result_dir = args.dir.resolve()
    if result_dir.exists():
        parser.error('Result directory already exists')
    for tool in ('Hyprland', 'hyprctl', 'dbus-run-session', 'mpv', 'grim', 'ffmpeg'):
        if not shutil.which(tool):
            parser.error(tool + ' is required')
    parent_display = Path(os.environ.get('WAYLAND_DISPLAY', ''))
    if not parent_display.is_absolute():
        parent_display = Path(os.environ.get('XDG_RUNTIME_DIR', '/nonexistent')) / parent_display
    if not parent_display.is_socket():
        parser.error('A parent Wayland session is required for the isolated renderer')

    result_dir.mkdir(parents=True, mode=0o700)
    report = {'passed': False, 'host_screen_captured': False,
              'data_origin': 'synthetic-private-wayland-output',
              'screenshot_backend': 'grim', 'checks': [],
              'meeting_clients': 'synthetic identities, not real calls'}
    processes, logs = [], []
    with tempfile.TemporaryDirectory(prefix='re-scope-') as temporary:
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
        # The coordinator launches python3 itself. Prefer this harness's real
        # interpreter over user version-manager shims under isolated XDG paths.
        env['PATH'] = str(Path(sys.executable).resolve().parent) + os.pathsep + env.get('PATH', '')
        for key in ('HYPRLAND_INSTANCE_SIGNATURE', 'NOTIFY_SOCKET', 'WAYLAND_SOCKET',
                    'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(key, None)

        def launch(command, name):
            stream = (result_dir / (name + '.log')).open('w')
            logs.append(stream)
            process = subprocess.Popen(command, env=env, stdout=stream, stderr=stream,
                                       start_new_session=True)
            processes.append(process)
            return process

        def window(process):
            clients = json.loads(run(['hyprctl', '-j', 'clients'], env))
            return next((item for item in clients
                         if item['pid'] == process.pid and item['mapped']), None)

        def isolated_output():
            monitors = json.loads(run(['hyprctl', '-j', 'monitors'], env))
            if len(monitors) != 1 or monitors[0]['name'] != OUTPUT:
                raise RuntimeError('Private headless output isolation no longer holds')
            if monitors[0]['width'] != 1280 or monitors[0]['height'] != 720 or monitors[0]['scale'] != 1:
                raise RuntimeError('Private output dimensions changed')
            return monitors[0]

        def capture(name, process, black):
            monitor = isolated_output()
            client = window(process)
            if not client:
                raise RuntimeError('Synthetic window disappeared before capture')
            image_path = result_dir / (name + '.png')
            # This is a generic desktop screenshot, deliberately outside Replay.
            run(['grim', '-o', OUTPUT, str(image_path)], env, timeout=8)
            image = pixels(image_path)
            if image[:2] != (1280, 720):
                raise RuntimeError('Screenshot did not match the private output')
            x, y = client['at']
            width, height = client['size']
            rect = (x - monitor['x'], y - monitor['y'],
                    x + width - monitor['x'], y + height - monitor['y'])
            metrics = inspect_rect(image, rect, black)
            report['checks'].append({'name': name, 'class': client['class'],
                                     'initial_class': client['initialClass'],
                                     'expected_black': black, 'pixels': metrics})
            return image

        def close(process):
            stop(process)
            wait_for(lambda: window(process) is None, 'Synthetic window did not close')

        def show_image(app_id, name):
            process = launch(['mpv', '--no-config', '--vo=gpu', '--gpu-context=wayland',
                              '--gpu-api=opengl', '--hwdec=no', '--ao=null',
                              '--osc=no', '--osd-level=0', '--input-default-bindings=no',
                              '--border=no', '--fullscreen', '--image-display-duration=inf',
                              '--wayland-app-id=' + app_id,
                              '--title=1Password / Bitwarden — synthetic shared screen',
                              str(fixture_image)], name)
            client = wait_for(lambda: window(process), 'Synthetic image window did not map')
            if client['class'] != app_id:
                raise RuntimeError('Synthetic window did not use the requested application ID')
            time.sleep(.25)
            return process

        def install_policy(apps, skip_apps):
            policy.write_text('[exclusions]\napps=' + json.dumps(apps)
                              + '\nskip_apps=' + json.dumps(skip_apps) + '\n')
            receipt = json.loads(run(install_cmd, env, timeout=10))
            if not receipt['validated']:
                raise RuntimeError('Private capture masks were not verified')
            return receipt

        try:
            compositor = launch(['dbus-run-session', '--', 'Hyprland', '--config', str(config)],
                                'compositor')

            def sockets_ready():
                sockets = list(base.glob('hypr/*/.socket.sock'))
                displays = [path for path in base.glob('wayland-*') if path.is_socket()]
                if compositor.poll() is not None:
                    raise RuntimeError('Private compositor exited during startup')
                return (sockets[0], displays[0]) if sockets and displays else None

            socket, display = wait_for(sockets_ready, 'Private compositor startup timed out', 10)
            env['HYPRLAND_INSTANCE_SIGNATURE'] = socket.parent.name
            env['WAYLAND_DISPLAY'] = str(display)
            run(['hyprctl', 'output', 'create', 'headless', OUTPUT], env)
            with config.open('a') as stream:
                stream.write('hl.monitor({ output = "WAYLAND-1", disabled = true })\n'
                             'hl.monitor({ output = "' + OUTPUT
                             + '", mode = "1280x720@30", position = "0x0", scale = 1 })\n')
            run(['hyprctl', 'reload'], env)
            wait_for(lambda: len(json.loads(run(['hyprctl', '-j', 'monitors'], env))) == 1,
                     'Parent-facing output did not disable')
            isolated_output()
            run(['hyprctl', 'dismissnotify', '-1'], env)
            policy = base / 'replay.toml'
            install_cmd = [sys.executable, str(ROOT / 'scripts/install_capture_exclusions.py'),
                           '--config-home', str(config_home), '--config', str(policy),
                           '--instance', env['HYPRLAND_INSTANCE_SIGNATURE']]
            source = result_dir / 'synthetic-source'
            run([binary, 'demo', '--dir', str(source), '--frames', '1', '--static',
                 '--no-ocr', '--codec', 'webp'], env)
            frames = json.loads(run([binary, 'list', '--dir', str(source)], env))
            fixture_image = result_dir / 'synthetic-fixture.png'
            run([binary, 'extract', '--dir', str(source), '--id', str(frames[0]['id']),
                 '--out', str(fixture_image)], env)

            player = show_image('mpv', 'player')
            baseline = capture('mpv-baseline', player, black=False)
            report['old_policy'] = install_policy(['mpv', 'fixture.private'], [])
            time.sleep(.2)
            capture('old-mpv-privacy-mask', player, black=True)
            report['new_policy'] = install_policy(['fixture.private'], ['mpv'])
            time.sleep(.2)
            restored = capture('mpv-skip-restores-generic-screenshot', player, black=False)
            if baseline != restored:
                raise RuntimeError('Restored mpv screenshot differs from its unmasked baseline')
            report['restored_matches_unmasked_baseline'] = True
            repeated = json.loads(run(install_cmd, env, timeout=10))
            if repeated['changed']:
                raise RuntimeError('Repeated mask installation was not idempotent')
            report['repeated_install_unchanged'] = True
            close(player)

            private = show_image('fixture.private', 'private')
            capture('strict-privacy-still-masked', private, black=True)
            close(private)

            viewer = launch([binary, 'view', '--dir', str(source)], 'viewer')
            client = wait_for(lambda: window(viewer), 'Replay viewer did not map')
            if client['class'] != 'omarchy-replay':
                raise RuntimeError('Replay viewer application ID changed')
            time.sleep(.2)
            capture('replay-self-still-masked', viewer, black=True)
            close(viewer)

            for app_id in ('chromium', 'zoom'):
                meeting = show_image(app_id, 'synthetic-' + app_id)
                capture(app_id + '-shared-fixture-not-masked', meeting, black=False)
                close(meeting)

            # Exercise settings reload through the real coordinator, not by
            # invoking the mask installer ourselves. Capture intent never runs.
            service_config = config_home / 'omarchy-replay/config.toml'

            def write_service_config(apps, skip_apps):
                service_config.write_text('[recording]\noutput=' + json.dumps(OUTPUT)
                                          + '\ninterval_seconds=5\n'
                                            '[storage]\nretention_days=1\nmax_disk_mib=64\nmin_free_mib=0\n'
                                            '[indexing]\ncpu_ceiling_percent=0\n'
                                            '[exclusions]\napps=' + json.dumps(apps)
                                          + '\nskip_apps=' + json.dumps(skip_apps) + '\n')

            def control(action):
                return json.loads(run([binary, 'daemon', action], env, timeout=15))

            service_check_cmd = [sys.executable, str(ROOT / 'scripts/install_capture_exclusions.py'),
                                 '--config-home', str(config_home), '--config', str(service_config),
                                 '--instance', env['HYPRLAND_INSTANCE_SIGNATURE'], '--check']

            def masks_current():
                status = control('status')
                report['coordinator_last_status'] = {key: status.get(key) for key in
                    ('running', 'synthetic', 'intent', 'state', 'reason', 'config_error',
                     'capture_attempts', 'retained_this_run', 'exclusions_pending',
                     'exclusions_error', 'compositor_instance')}
                try:
                    verified = json.loads(run(service_check_cmd, env))['validated']
                    return verified and status.get('exclusions_pending') is False and not status.get('exclusions_error')
                except RuntimeError as error:
                    report['last_mask_check_error'] = str(error)
                    return False

            def confirm_idle(intent):
                status = control('status')
                if (not status.get('running') or status.get('synthetic')
                        or status.get('intent') != intent or status.get('config_error')
                        or status.get('exclusions_pending') is not False or status.get('exclusions_error')
                        or status.get('capture_attempts') != 0 or status.get('retained_this_run') != 0):
                    raise RuntimeError('Native coordinator changed recording intent or captured pixels')
                return {key: status[key] for key in
                        ('running', 'synthetic', 'intent', 'capture_attempts', 'retained_this_run',
                         'exclusions_pending', 'exclusions_error')}

            write_service_config(['mpv', 'fixture.private'], [])
            if control('init')['recording_started']:
                raise RuntimeError('Isolated initialization started capture')
            coordinator = launch([binary, 'daemon', 'run'], 'coordinator')
            wait_for(lambda: control('status').get('running'), 'Native coordinator did not become available', 15)
            wait_for(masks_current, 'Stopped coordinator did not install initial privacy masks', 15)
            report['coordinator_checks'] = [{'phase': 'initial', **confirm_idle('stopped')}]
            player = show_image('mpv', 'coordinator-player')
            capture('stopped-coordinator-installs-strict-mask', player, black=True)

            write_service_config(['fixture.private'], ['mpv'])
            control('reload')
            wait_for(masks_current, 'Stopped coordinator did not remove the old mpv mask', 15)
            capture('stopped-coordinator-reload-restores-screenshot', player, black=False)
            report['coordinator_checks'].append({'phase': 'reload-to-skip', **confirm_idle('stopped')})

            control('pause')
            write_service_config(['mpv', 'fixture.private'], [])
            control('reload')
            wait_for(masks_current, 'Paused coordinator did not install the requested mpv mask', 15)
            capture('paused-coordinator-reload-adds-strict-mask', player, black=True)
            report['coordinator_checks'].append({'phase': 'reload-to-strict', **confirm_idle('paused')})
            write_service_config(['fixture.private'], [])
            control('reload')
            wait_for(masks_current, 'Paused coordinator did not remove the old mpv mask', 15)
            capture('paused-coordinator-reload-removes-strict-mask', player, black=False)
            report['coordinator_checks'].append({'phase': 'remove-app', **confirm_idle('paused')})
            close(player)
            control('shutdown')
            coordinator.wait(timeout=10)
            report['passed'] = True
        except BaseException as error:
            report['error'] = str(error)
            raise
        finally:
            for process in reversed(processes):
                stop(process)
            for stream in logs:
                stream.close()
            coordinator_log = base / 'state/omarchy-replay/recording.log'
            if coordinator_log.is_file():
                (result_dir / 'coordinator-state.log').write_bytes(coordinator_log.read_bytes()[-65536:])
            report['all_owned_processes_stopped'] = all(process.poll() is not None for process in processes)
            (result_dir / 'check.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    os.umask(0o077)
    main()
