#!/usr/bin/env python3
"""Install Replay's narrow Hyprland capture exclusion; never starts recording."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '-- BEGIN Omarchy Replay viewer exclusion'
END = '-- END Omarchy Replay viewer exclusion'
RULE_PREFIX = '-- Managed by Omarchy Replay:'


def lua_string(value):
    # Lua accepts escaped decimal bytes; JSON's Unicode escapes are not Lua.
    return '"' + ''.join(chr(byte) if 32 <= byte < 127 and byte not in (34, 92)
                         else '\\' + str(byte).zfill(3) for byte in os.fsencode(value)) + '"'


def updated_config(original, rule_path, begin=BEGIN, end=END):
    block = begin + '\n' + 'dofile(' + lua_string(rule_path) + ')\n' + end
    if begin in original or end in original:
        if original.count(begin) != 1 or original.count(end) != 1:
            raise RuntimeError('Replay configuration markers are ambiguous; no files changed.')
        before, remainder = original.split(begin)
        if end not in remainder:
            raise RuntimeError('Replay configuration markers are out of order; no files changed.')
        _, after = remainder.split(end)
        # Relocate the owned block to the end so later user rules cannot cancel it.
        original = before.rstrip() + '\n' + after.lstrip('\n')
    return original.rstrip() + '\n\n' + block + '\n'


def atomic_write(path, data, mode):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.' + path.name + '-', delete=False) as stream:
        temporary = Path(stream.name)
        try:
            os.fchmod(stream.fileno(), mode)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def hyprctl(*arguments):
    result = subprocess.run(['hyprctl', *arguments], capture_output=True, text=True,
                            timeout=5, check=False)
    if result.returncode:
        raise RuntimeError('Hyprland ' + arguments[0] + ' failed: ' +
                           (result.stderr or result.stdout).strip()[-1500:])
    return result.stdout.strip()


def validate():
    errors = hyprctl('configerrors')
    if errors and errors != '[]':
        raise RuntimeError('Hyprland reports configuration errors: ' + errors[-2000:])


def backup(path):
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    destination = path.with_name(path.name + '.replay-backup-' + stamp)
    shutil.copy2(path, destination)
    return destination


def install(config_home, source=None, filename="replay-viewer.lua", begin=BEGIN, end=END):
    config_home = Path(config_home).expanduser().resolve()
    config = (config_home / 'hypr/hyprland.lua').resolve(strict=True)
    if not config.is_file():
        raise RuntimeError('Expected an existing Hyprland Lua configuration.')
    rule = config_home / 'omarchy-replay/hypr' / filename
    if rule.is_symlink():
        raise RuntimeError('The managed Replay rule must not be a symbolic link.')
    old_rule = rule.read_bytes() if rule.exists() else None
    if old_rule is not None and not old_rule.startswith(RULE_PREFIX.encode()):
        raise RuntimeError('The Replay rule path contains an unmanaged file; no files changed.')
    rule_data = (source or ROOT / 'config/hypr/replay-viewer.lua').read_bytes()
    old_config = config.read_bytes()
    new_config = updated_config(old_config.decode('utf-8'), str(rule), begin, end).encode('utf-8')
    mode = stat.S_IMODE(config.stat().st_mode)
    old_rule_mode = stat.S_IMODE(rule.stat().st_mode) if old_rule is not None else 0o600
    # Do not attribute pre-existing errors to this change or install into a broken config.
    validate()
    receipt = {'config': str(config), 'rule': str(rule), 'changed': False, 'backups': []}
    if old_config != new_config:
        receipt['backups'].append(str(backup(config)))
    if old_rule is not None and old_rule != rule_data:
        receipt['backups'].append(str(backup(rule)))
    # Keep another editor's changes if the user touched either file during validation.
    if config.read_bytes() != old_config or (rule.read_bytes() if rule.exists() else None) != old_rule:
        raise RuntimeError('Hyprland configuration changed during installation; no Replay changes applied.')
    try:
        if old_rule != rule_data:
            atomic_write(rule, rule_data, old_rule_mode)
            receipt['changed'] = True
        if old_config != new_config:
            if config.read_bytes() != old_config:
                raise RuntimeError('Hyprland configuration changed during installation; preserving the newer edit.')
            atomic_write(config, new_config, mode)
            receipt['changed'] = True
        # Validate even a repeated install: the current compositor may have restarted.
        hyprctl('reload')
        validate()
    except BaseException as error:
        # Restore only our changes; never overwrite an intervening edit.
        current_config = config.read_bytes()
        if current_config == new_config and old_config != new_config:
            atomic_write(config, old_config, mode)
            current_config = old_config
        # A concurrent editor may retain our include. Keep its target rather
        # than breaking that newer configuration during rollback.
        keep_rule = begin.encode() in current_config and lua_string(str(rule)).encode() in current_config
        if (old_rule is not None or not keep_rule) and rule.exists() and rule.read_bytes() == rule_data and old_rule != rule_data:
            if old_rule is None:
                rule.unlink()
            else:
                atomic_write(rule, old_rule, old_rule_mode)
        try:
            hyprctl('reload')
            validate()
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as rollback:
            raise RuntimeError(str(error) + '; restore/reload also needs attention: ' + str(rollback)) from error
        raise
    receipt['validated'] = True
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config-home', type=Path,
                        default=Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')))
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.config_home), indent=2))
    except (OSError, RuntimeError, UnicodeError, subprocess.TimeoutExpired) as error:
        print('Replay exclusion: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
