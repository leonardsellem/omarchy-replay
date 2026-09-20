#!/usr/bin/env python3
"""Service intent controls on synthetic histories; no supervisor or OCR is started."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='replay-service-controls-') as temporary:
        root = Path(temporary)
        runtime = root / 'runtime'
        runtime.mkdir(mode=0o700)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), QT_QPA_PLATFORM='offscreen')

        def history(name, policy=None):
            trial = root / name
            dataset = trial / 'dataset'
            dataset.mkdir(parents=True)
            (dataset / 'index.sqlite').touch()
            if policy is not None:
                settings = trial / 'trial.json'
                settings.write_text(json.dumps({'config': policy}))
                settings.chmod(0o600)
            return dataset

        def control(dataset, action, okay=True):
            result = subprocess.run([binary, 'service', action, '--dir', str(dataset)],
                                    env=env, capture_output=True, text=True, timeout=5)
            if not okay:
                assert result.returncode != 0, f'{action} unexpectedly started work'
                return result
            assert result.returncode == 0, result.stderr
            status = json.loads(result.stdout)
            assert not status['running'] and not status['worker_running']
            return status

        policy = {'scheduler': 'adaptive', 'ocr_mode': 'incremental', 'ocr_cpu_percent': 12,
                  'ocr_max_wall_ms': 60000, 'idle_seconds': 90, 'idle_cpu_percent': 35,
                  'request_cpu_percent': 25}
        dataset = history('saved-trial', policy)
        paused = control(dataset, 'pause')
        assert paused['paused'] and paused['enabled'] and paused['configured']
        durable = dataset / '.index-service.json'
        assert json.loads(durable.read_text())['policy'] == policy
        assert durable.stat().st_mode & 0o077 == 0
        assert not control(dataset, 'stop')['enabled']

        missing = dict(policy, ocr_data_path=str(root / 'removed-model'))
        dataset = history('missing-model', missing)
        paused = control(dataset, 'pause')
        assert paused['paused'] and paused['enabled'] and paused.get('policy_error')
        durable = dataset / '.index-service.json'
        before = durable.read_bytes()
        assert json.loads(before)['policy'] == missing
        for action in ('start', 'resume', 'ensure'):
            rejected = control(dataset, action, okay=False)
            assert 'Saved OCR model is unavailable' in rejected.stderr
            assert durable.read_bytes() == before, 'Failed launch changed saved pause'
        assert not control(dataset, 'stop')['enabled']
        assert json.loads(durable.read_text())['policy'] == missing

        dataset = history('first-stop', missing)
        assert not control(dataset, 'stop')['enabled']
        assert json.loads((dataset / '.index-service.json').read_text())['policy'] == missing

        dataset = history('unknown-policy')
        paused = control(dataset, 'pause')
        assert paused['paused'] and paused['enabled'] and not paused['configured']
        assert 'No saved indexing policy' in control(dataset, 'resume', okay=False).stderr
        assert not control(dataset, 'stop')['enabled']
    print('Saved-trial pause/stop intent and deferred launch validation passed without workers.')


if __name__ == '__main__':
    main()
