#!/usr/bin/env python3
"""Run pure plugin-state tests with Qt 6, or report an explicit dependency skip."""
import os
from pathlib import Path
import shutil
import subprocess


def main():
    candidates = (Path('/usr/lib/qt6/bin/qmltestrunner'), Path('/usr/lib64/qt6/bin/qmltestrunner'))
    runner = next((str(p) for p in candidates if p.is_file()), shutil.which('qmltestrunner6'))
    if not runner:
        print('SKIP: Qt 6 qmltestrunner is required for plugin model tests')
        return 77
    fixture = Path(__file__).resolve().parent / 'plugin/tst_model.qml'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic')
    return subprocess.run([runner, '-input', str(fixture), '-o', '-,txt'], env=env, timeout=15).returncode


if __name__ == '__main__':
    raise SystemExit(main())
