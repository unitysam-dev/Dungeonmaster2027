"""Check the Menagerie HTTP host with legally supplied local game data."""
import json, os, re, subprocess, sys
from pathlib import Path
from urllib.request import Request, urlopen

root = Path(__file__).resolve().parent
assert os.environ.get('DM_DATA'), 'Set DM_DATA to your original game data directory'
with subprocess.Popen([sys.executable, '-u', 'server.py'], cwd=root,
                      env={**os.environ, 'PORT': '0'}, stdout=subprocess.PIPE, text=True) as host:
    try:
        m = re.search(r'http://127\.0\.0\.1:(\d+)', host.stdout.readline())
        assert m, 'Host did not announce its loopback URL'
        base = m[0]
        def request(path, data=None, token=None):
            headers = {'Content-Type': 'application/json'}
            if token:
                headers['Authorization'] = 'Bearer ' + token
            with urlopen(Request(base + path, data=data, headers=headers), timeout=5) as r:
                return r.read()
        assert b'<!doctype html>' in request('/').lower()
        a,b = [json.loads(request('/api/join', json.dumps({'party': i}).encode())) for i in (0,1)]
        assert a['partyId'] == 0 and b['partyId'] == 1 and a['token'] != b['token']
        for owner in (a,b):
            world = json.loads(request('/api/state', token=owner['token']))['world']
            assert len(world['monsters']) == 15
            assert request('/api/frame', token=owner['token']).startswith(b'\x89PNG\r\n\x1a\n')
            assert all(len(p['state']['champions']) == 4 for p in world['parties'])
        print('PASS: page loads; two independent four-champion parties see the 15-creature world')
    finally:
        host.terminate()
        try:
            host.wait(timeout=5)
        except subprocess.TimeoutExpired:
            host.kill()
            host.wait()
