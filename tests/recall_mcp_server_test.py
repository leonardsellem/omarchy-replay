#!/usr/bin/env python3
"""MCP adapter regressions over stdio against a synthetic demo archive."""
import json
import os
from datetime import datetime, timedelta, timezone
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / 'scripts/replay_mcp.py'
BIN = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/replay')
ENV = dict(os.environ, OMP_THREAD_LIMIT='1', QT_QPA_PLATFORM='offscreen',
           OMARCHY_REPLAY_BIN=BIN, PYTHONDONTWRITEBYTECODE='1')
ENV.pop('OMARCHY_REPLAY_ARCHIVE', None)


class Server:
    def __init__(self, archive):
        self.proc = subprocess.Popen(
            [sys.executable, '-B', str(SERVER)], env=dict(ENV, OMARCHY_REPLAY_ARCHIVE=archive),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def call(self, payload, timeout=120):
        self.proc.stdin.write(json.dumps(payload) + '\n')
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        assert line, f'server closed: {self.proc.stderr.read()}'
        return json.loads(line)

    def notify(self, payload):
        self.proc.stdin.write(json.dumps(payload) + '\n')
        self.proc.stdin.flush()


def main():
    with tempfile.TemporaryDirectory(prefix='replay-mcp-') as temporary:
        os.chdir(temporary)
        started = datetime.now(timezone.utc)
        dataset = Path(temporary) / 'archive'
        # Fictional synthetic workload only; no real screen content is involved.
        subprocess.run([BIN, 'demo', '--dir', str(dataset), '--frames', '8', '--interval', '.25',
                        '--width', '960', '--height', '540', '--max-mib', '64'],
                       env=ENV, capture_output=True, text=True, timeout=120)
        assert (dataset / 'ground-truth.json').exists(), 'demo archive was not created'

        server = Server(dataset)
        init = server.call({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
            'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'test'}}})
        assert init['result']['serverInfo']['name'] == 'omarchy-replay-mcp', init
        server.notify({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        # A notification must not produce a response line; the next reply is tools/list.
        listing = server.call({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'})
        names = {tool['name'] for tool in listing['result']['tools']}
        assert names == {'search', 'list_frames', 'get_moment', 'status'}, names
        assert all('untrusted' in tool['description'].lower() for tool in listing['result']['tools'])

        search = server.call({'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {
            'name': 'search', 'arguments': {'words': 'Patrick', 'limit': 2}}})
        assert search['result']['isError'] is False, search
        payload = json.loads(search['result']['content'][0]['text'])
        assert payload['total_matches'] >= 1 and payload['results'], payload
        first_id = payload['results'][0]['id']

        moment = server.call({'jsonrpc': '2.0', 'id': 4, 'method': 'tools/call', 'params': {
            'name': 'get_moment', 'arguments': {'id': first_id, 'context_seconds': 5}}})
        moment_payload = json.loads(moment['result']['content'][0]['text'])
        assert moment_payload['moment']['id'] == first_id, moment_payload
        assert moment_payload['moment']['image_path'], moment_payload
        assert not Path('extracted.png').exists(), 'moment fetch must not extract images'

        status = server.call({'jsonrpc': '2.0', 'id': 5, 'method': 'tools/call', 'params': {
            'name': 'status', 'arguments': {}}})
        status_payload = json.loads(status['result']['content'][0]['text'])
        assert isinstance(status_payload, dict), status_payload

        browsed = server.call({'jsonrpc': '2.0', 'id': 6, 'method': 'tools/call', 'params': {
            'name': 'list_frames', 'arguments': {
                'since': (started - timedelta(hours=1)).strftime('%Y-%m-%dT%H:%M:%SZ'), 'limit': 3}}})
        browse_payload = json.loads(browsed['result']['content'][0]['text'])
        assert isinstance(browse_payload, list) and browse_payload, browse_payload

        # A bad argument is an MCP tool error, not a crash; the server stays alive.
        bad = server.call({'jsonrpc': '2.0', 'id': 7, 'method': 'tools/call', 'params': {
            'name': 'search', 'arguments': {'words': 'Patrick', 'limit': 0}}})
        assert bad['result']['isError'] is True, bad
        assert 'limit' in bad['result']['content'][0]['text'], bad
        after = server.call({'jsonrpc': '2.0', 'id': 8, 'method': 'tools/call', 'params': {
            'name': 'search', 'arguments': {'words': 'Patrick', 'limit': 1}}})
        assert after['result']['isError'] is False, after

        unknown_tool = server.call({'jsonrpc': '2.0', 'id': 9, 'method': 'tools/call', 'params': {
            'name': 'nope', 'arguments': {}}})
        assert unknown_tool['result']['isError'] is True, unknown_tool

        missing = server.call({'jsonrpc': '2.0', 'id': 10, 'method': 'no/such/method'})
        assert missing['error']['code'] == -32601, missing

        server.proc.stdin.close()
        assert server.proc.wait(timeout=30) == 0, server.proc.stderr.read()


if __name__ == '__main__':
    main()
