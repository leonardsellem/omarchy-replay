#!/usr/bin/env python3
"""Install validated Replay capture masks in Hyprland; never starts recording."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tomllib

from install_viewer_exclusion import atomic_write, backup, hyprctl, lua_string

BEGIN = '-- BEGIN Omarchy Replay capture exclusions'
END = '-- END Omarchy Replay capture exclusions'
PREFIX = '-- Managed by Omarchy Replay: capture exclusions\n'
DEFAULT_APPS = ['omarchy-replay', 'org.omarchy.screensaver', 'com.onepassword.OnePassword']
MAX_CONFIG = 256 * 1024


def text(value, name, limit, default=''):
    value = default if value is None else value
    if not isinstance(value, str) or len(value) > limit or '\x00' in value:
        raise RuntimeError('Invalid exclusion ' + name + '.')
    return value


def read_policy(path):
    with Path(path).open('rb') as stream:
        data = stream.read(MAX_CONFIG + 1)
    if len(data) > MAX_CONFIG:
        raise RuntimeError('Replay configuration exceeds its size limit.')
    document = tomllib.loads(data.decode('utf-8'))
    table = document.get('exclusions', {})
    if not isinstance(table, dict):
        raise RuntimeError('Exclusions must be a TOML table.')
    apps, windows = table.get('apps', DEFAULT_APPS), table.get('windows', [])
    if not isinstance(apps, list) or len(apps) > 64 or not isinstance(windows, list) or len(windows) > 64:
        raise RuntimeError('Exclusion lists exceed their limits.')
    apps = sorted(set(text(app, 'application identifier', 256) for app in apps)
                  | {'omarchy-replay', 'org.omarchy.screensaver'})
    if '' in apps:
        raise RuntimeError('Excluded application identifiers cannot be empty.')
    rules = []
    for item in windows:
        if not isinstance(item, dict) or set(item) - {'app_id', 'title_regex', 'address', 'scope', 'compositor_instance'}:
            raise RuntimeError('Unknown window exclusion setting.')
        rule = {key: text(item.get(key), key, limit) for key, limit in
                [('app_id', 256), ('title_regex', 512), ('address', 18), ('compositor_instance', 256)]}
        if item.get('scope', 'output') != 'output':
            raise RuntimeError('Only output-scoped exclusions are supported.')
        if not any(rule[key] for key in ('app_id', 'title_regex', 'address')):
            raise RuntimeError('A window exclusion needs a matcher.')
        if rule['address']:
            if not re.fullmatch(r'0x[0-9a-f]{1,16}', rule['address']) or not rule['compositor_instance']:
                raise RuntimeError('Window addresses require their original desktop session identity.')
            if not rule['app_id'] and not rule['title_regex']:
                raise RuntimeError('Address-only exclusions need an application or title rule for compositor masking.')
        rules.append(rule)
    return {'apps': apps, 'windows': rules}, hashlib.sha256(data).hexdigest()


def exact_pattern(value):
    return '^' + ''.join('\\' + char if char in r'.^$*+?{}[]\|()' else char for char in value) + '$'


def title_pattern(value):
    # PCRE accepts $ before a final newline; RE2's default $ requires EOF.
    # Broaden that boundary in the mask so a Qt-matched title cannot escape it.
    output, escaped, quoted, character_class = [], False, False, False
    class_first, posix_class = False, ''
    for index, char in enumerate(value):
        if escaped:
            output.append(char)
            if char == 'Q' and not character_class:
                quoted = True
            elif char == 'E':
                quoted = False
            if character_class:
                class_first = False
            escaped = False
        elif char == '\\':
            output.append(char)
            escaped = True
        elif quoted:
            output.append(char)
        elif character_class:
            output.append(char)
            if posix_class:
                if char == ']' and index > 0 and value[index - 1] == posix_class:
                    posix_class = ''
            elif char == '[' and index + 1 < len(value) and value[index + 1] in ':=.':
                posix_class = value[index + 1]
            elif char == ']' and not class_first:
                character_class = False
            if char != '^' or not class_first:
                class_first = False
        elif char == '[':
            character_class = class_first = True
            output.append(char)
        elif char == '$' and not character_class:
            output.append('(?:\\n?$)')
        else:
            output.append(char)
    return '(?s:.*)(?:' + ''.join(output) + ')(?s:.*)'


def render_rules(policy):
    canonical = json.dumps(policy, sort_keys=True, ensure_ascii=True, separators=(',', ':')).encode()
    lines = [PREFIX.rstrip(), '-- Dynamic compositor masks cover moving windows and their popups.',
             '_G.oma_replay_capture_exclusions = nil']
    def append(app='', title=''):
        variants = ['class', 'initial_class'] if app else [None]
        for variant in variants:
            matchers = []
            if variant:
                matchers.append(variant + ' = ' + lua_string(exact_pattern(app)))
            if title:
                # Hyprland RE2 rules match complete strings; Replay text rules
                # search anywhere. Only the surrounding wildcards match newline.
                matchers.append('title = ' + lua_string(title_pattern(title)))
            lines.extend(['hl.window_rule({', '  match = { ' + ', '.join(matchers) + ' },',
                          '  no_screen_share = true,', '})'])
    for app in policy['apps']:
        append(app)
    broadened = 0
    for rule in policy['windows']:
        if rule['address']:
            # Declarative rules cannot match addresses. Protect all windows
            # satisfying its other matchers, not only a possibly reused address.
            broadened += 1
        append(rule['app_id'], rule['title_regex'])
    token = hashlib.sha256(b'oma-replay-mask-v1\0' + canonical + b'\0' + '\n'.join(lines).encode()).hexdigest()
    lines.append('_G.oma_replay_capture_exclusions = ' + lua_string(token))
    return ('\n'.join(lines) + '\n').encode('utf-8'), token, broadened


def updated_config(original, rule_path):
    block = BEGIN + '\ndofile(' + lua_string(rule_path) + ')\n' + END
    if BEGIN in original or END in original:
        if original.count(BEGIN) != 1 or original.count(END) != 1:
            raise RuntimeError('Replay configuration markers are ambiguous; no files changed.')
        before, remainder = original.split(BEGIN)
        if END not in remainder:
            raise RuntimeError('Replay configuration markers are out of order; no files changed.')
        _, after = remainder.split(END)
        original = before.rstrip() + '\n' + after.lstrip('\n')
    return original.rstrip() + '\n\n' + block + '\n'


def install(config_home, policy_path, instance=None, check=False):
    policy, config_hash = read_policy(policy_path)
    rule_data, token, broadened = render_rules(policy)
    instance = instance or os.environ.get('HYPRLAND_INSTANCE_SIGNATURE', '')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,256}', instance):
        raise RuntimeError('A verified Hyprland desktop instance is required.')
    if any(rule['address'] and rule['compositor_instance'] != instance for rule in policy['windows']):
        raise RuntimeError('An excluded window belongs to a previous desktop session; remove or reselect it.')
    def invoke(*args):
        return hyprctl('-i', instance, *args)
    def validate():
        raw = invoke('-j', 'configerrors')
        try:
            errors = json.loads(raw)
        except ValueError as error:
            raise RuntimeError('Could not verify Hyprland configuration.') from error
        if not isinstance(errors, list) or any(not isinstance(item, str) or item.strip() for item in errors):
            # Config diagnostics can contain titles or paths. The caller needs
            # the failure category; detailed compositor errors stay local.
            raise RuntimeError('Hyprland configuration is invalid; check hyprctl configerrors locally.')
    def verify():
        if invoke('eval', 'assert(_G.oma_replay_capture_exclusions == ' + lua_string(token) + ')').strip() != 'ok':
            raise RuntimeError('Compositor capture exclusion verification failed.')
    home = Path(config_home).expanduser().resolve()
    config = (home / 'hypr/hyprland.lua').resolve(strict=True)
    if not config.is_file():
        raise RuntimeError('Expected an existing Hyprland Lua configuration.')
    rule = home / 'omarchy-replay/hypr/capture-exclusions.lua'
    if rule.is_symlink():
        raise RuntimeError('The managed capture rule must not be a symbolic link.')
    old_rule = rule.read_bytes() if rule.exists() else None
    if old_rule is not None and not old_rule.startswith(PREFIX.encode()):
        raise RuntimeError('The capture rule path contains an unmanaged file; no files changed.')
    old_config = config.read_bytes()
    new_config = updated_config(old_config.decode('utf-8'), str(rule)).encode('utf-8')
    mode = stat.S_IMODE(config.stat().st_mode)
    rule_mode = stat.S_IMODE(rule.stat().st_mode) if old_rule is not None else 0o600
    receipt = {'validated': False, 'changed': False, 'backups': [], 'mask_token': token,
               'config_sha256': config_hash, 'rules_sha256': hashlib.sha256(rule_data).hexdigest(),
               'compositor_instance': instance, 'broadened_address_masks': broadened}
    validate()
    if check:
        if old_rule != rule_data or old_config != new_config:
            raise RuntimeError('Capture exclusions need to be reapplied before recording.')
        verify()
        receipt['validated'] = True
        return receipt
    if old_config != new_config:
        receipt['backups'].append(str(backup(config)))
    if old_rule is not None and old_rule != rule_data:
        receipt['backups'].append(str(backup(rule)))
    if config.read_bytes() != old_config or (rule.read_bytes() if rule.exists() else None) != old_rule:
        raise RuntimeError('Hyprland configuration changed during installation; no Replay changes applied.')
    try:
        if old_rule != rule_data:
            atomic_write(rule, rule_data, rule_mode)
            receipt['changed'] = True
        if old_config != new_config:
            if config.read_bytes() != old_config:
                raise RuntimeError('Hyprland configuration changed during installation; preserving the newer edit.')
            atomic_write(config, new_config, mode)
            receipt['changed'] = True
        invoke('reload')
        validate()
        verify()
        # Catch another writer altering the policy/config while reload ran.
        if (config.read_bytes() != new_config or rule.read_bytes() != rule_data or
                hashlib.sha256(Path(policy_path).read_bytes()).hexdigest() != config_hash):
            raise RuntimeError('Capture configuration changed during verification; recording remains blocked.')
    except BaseException as error:
        current = config.read_bytes()
        if current == new_config and old_config != new_config:
            atomic_write(config, old_config, mode)
            current = old_config
        keep_target = BEGIN.encode() in current and lua_string(str(rule)).encode() in current
        if (old_rule is not None or not keep_target) and rule.exists() and rule.read_bytes() == rule_data and old_rule != rule_data:
            if old_rule is None:
                rule.unlink()
            else:
                atomic_write(rule, old_rule, rule_mode)
        elif keep_target and old_rule is None and rule.exists() and rule.read_bytes() == rule_data:
            # A concurrent editor retained the include. Leave a valid owned
            # target without a success token; capture stays blocked and their
            # newer desktop configuration remains loadable.
            atomic_write(rule, (PREFIX + '_G.oma_replay_capture_exclusions = nil\n').encode(), rule_mode)
        try:
            invoke('reload')
            validate()
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as rollback:
            raise RuntimeError(str(error) + '; restore/reload also needs attention.') from rollback
        raise
    receipt['validated'] = True
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--config-home', type=Path, default=Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')))
    parser.add_argument('--instance', help='Verified compositor instance from the recording environment monitor.')
    parser.add_argument('--check', action='store_true', help='Read-only verification; do not write or reload.')
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.config_home, args.config, args.instance, args.check)))
    except (OSError, RuntimeError, UnicodeError, tomllib.TOMLDecodeError, subprocess.TimeoutExpired) as error:
        print('Replay capture exclusions: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
