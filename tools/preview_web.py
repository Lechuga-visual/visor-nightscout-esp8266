#!/usr/bin/env python3
"""Local preview of the device dashboard with synthetic data only (no firmware).

python tools/preview_web.py --port 8766
Login: preview / preview-only. This server does not access Nightscout or a Wemos.
"""
import argparse
import base64
import json
import math
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]


def state(scenario='normal'):
    now = int(time.time())
    available = scenario != 'empty'
    age = None if scenario == 'clock' else 1800 if scenario == 'stale' else 120
    value = 58 if scenario == 'low' else 242 if scenario == 'high' else 118
    values = [value + 20 * (math.sin(i / 5) - math.sin(24 / 5)) + 7 * (math.sin(i / 2) - math.sin(24 / 2)) for i in range(25)]
    low,high=min(60,int(min(values)-10)),max(200,int(max(values)+10))
    points = [] if not available or age is None else [
        [int((7200-age-(24-i)*300)*1000/7200), int((high-values[i])*1000/(high-low)), False]
        for i in range(25) if 7200-age-(24-i)*300>=0]
    if points: points[0][2]=True
    return {'firmware':'0.5.0','available':available,'glucose':value if available else None,
            'reading_date_ms':(now - (age or 0)) * 1000 if available else None,'age_seconds':age,
            'tone':{'stale':'old','clock':'clock'}.get(scenario,scenario) if scenario in ('low','high','stale','clock','error','empty') else 'normal',
            'trend_symbol':'→','trend_label':'Estable','synthetic':True,'clock_valid':age is not None,
            'message':'<img src=x onerror=alert(1)>' if scenario == 'untrusted-text' else 'Error HTTP 503' if scenario == 'error' else 'Conectado',
            'wifi_connected':True,'rssi_dbm':-61,'ip':'192.168.1.42','checked_seconds_ago':3,
            'plot':{'low':low,'high':high,'points':points}}


class Handler(BaseHTTPRequestHandler):
    def authorized(self):
        if self.headers.get('Authorization') == 'Basic ' + base64.b64encode(b'preview:preview-only').decode():
            return True
        self.send_response(401); self.send_header('WWW-Authenticate','Basic realm="Preview"'); self.end_headers()
        return False

    def respond(self, status, body, mime='application/json'):
        if not isinstance(body, bytes): body = json.dumps(body).encode()
        self.send_response(status); self.send_header('Content-Type',mime)
        self.send_header('Cache-Control','no-store'); self.send_header('Content-Length',str(len(body)))
        self.end_headers(); self.wfile.write(body)

    def do_GET(self):
        if not self.authorized(): return
        path = urlsplit(self.path).path
        if path == '/api/state':
            if self.server.scenario == 'unreachable': return self.respond(503,{'error':'synthetic'})
            return self.respond(200,state(self.server.scenario))
        files = {'/':('index.html','text/html; charset=utf-8'),
                 '/assets/ui.css':('ui.css','text/css'),'/assets/app.js':('app.js','application/javascript')}
        if path in files:
            name,mime=files[path]; return self.respond(200,(ROOT / 'web' / name).read_bytes(),mime)
        return self.respond(404,{'error':'Preview only'})

    def do_POST(self):
        if not self.authorized(): return
        if self.path != '/preview/scenario': return self.respond(404,{'error':'Preview only'})
        payload = json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))))
        self.server.scenario = payload['scenario']; self.respond(200,{'ok':True})

    def log_message(self, *args): pass


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--port',type=int,default=8766)
    args=parser.parse_args(); server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    server.scenario='normal'
    print(f'Preview: http://127.0.0.1:{server.server_port} (preview / preview-only)',flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()
