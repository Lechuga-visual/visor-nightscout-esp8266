#!/usr/bin/env python3
"""Nightscout-compatible synthetic readings. Python standard library only."""

import argparse
import hashlib
import json
import math
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

SCENARIOS = ("cycle", "normal", "rising", "falling", "high", "low", "stale", "empty", "error")
PAGE = """<!doctype html><html lang="es"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Simulador Nightscout</title>
<style>body{font:18px system-ui;max-width:650px;margin:40px auto;padding:20px}
select,button{font:inherit;padding:8px}pre{white-space:pre-wrap}</style>
<h1>Simulador Nightscout</h1><p><strong>DATOS FICTICIOS PARA PRUEBAS</strong></p>
<p>El Wemos consulta <code>/api/v1/entries.json?count=1</code>.
Los valores se expresan en mg/dL.</p>
<form id="form"><label>Escenario <select id="scenario">
OPTIONS</select></label> <button>Aplicar</button></form>
<p id="status" role="status"></p><pre id="reading"></pre>
<script>
async function refresh(){
 const state=await fetch('/simulator/state').then(r=>r.json());
 document.getElementById('scenario').value=state.scenario;
 const r=await fetch('/api/v1/entries.json?count=1');
 document.getElementById('reading').textContent=JSON.stringify(await r.json(),null,2);
}
document.getElementById('form').onsubmit=async e=>{
 e.preventDefault();
 const r=await fetch('/simulator/scenario',{method:'POST',
 headers:{'Content-Type':'application/json'},
 body:JSON.stringify({scenario:document.getElementById('scenario').value})});
 document.getElementById('status').textContent=r.ok?'Escenario aplicado':'Error';
 await refresh();
};
refresh(); setInterval(refresh,15000);
</script></html>""".replace("OPTIONS", "".join(f'<option>{s}</option>' for s in SCENARIOS))


def make_entries(scenario, count, now=None):
    """Generate newest-first SGV entries, using Nightscout's millisecond dates."""
    now = time.time() if now is None else now
    if scenario == "empty":
        return []
    if scenario == "cycle":
        scenario = ("normal", "rising", "high", "falling", "low")[int(now // 60) % 5]
    latest = int(now // 300) * 300
    if scenario == "stale":
        latest -= 30 * 60
    base, slope, direction = {
        "normal": (115, 0, "Flat"),
        "rising": (150, 12, "SingleUp"),
        "falling": (125, -12, "SingleDown"),
        "high": (240, 0, "Flat"),
        "low": (60, 0, "Flat"),
        "stale": (118, 0, "Flat"),
    }[scenario]
    entries = []
    for i in range(count):
        timestamp = latest - i * 300
        value = round(base - slope * i + (3 * math.sin(timestamp / 600) if not slope else 0))
        entries.append({
            "_id": hashlib.sha256(f"{scenario}:{timestamp}".encode()).hexdigest()[:24],
            "type": "sgv",
            "sgv": max(40, min(400, value)),
            "direction": direction,
            "date": timestamp * 1000,
            "dateString": datetime.fromtimestamp(timestamp, timezone.utc).isoformat().replace("+00:00", "Z"),
            "device": "NightscoutSimulator",
            "synthetic": True,
        })
    return entries


class SimulatorServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, scenario="cycle"):
        super().__init__(address, Handler)
        self.scenario = scenario
        self.state_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def respond(self, status, body, content_type="application/json; charset=utf-8"):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path == "/":
            return self.respond(200, PAGE.encode(), "text/html; charset=utf-8")
        with self.server.state_lock:
            scenario = self.server.scenario
        if parsed.path == "/simulator/state":
            return self.respond(200, {"scenario": scenario, "synthetic": True})
        if parsed.path == "/api/v1/status.json":
            return self.respond(200, {
                "status": "ok", "name": "Nightscout Simulator", "version": "mock-1.0",
                "serverTimeEpoch": int(time.time() * 1000),
                "settings": {"units": "mg/dl"}, "synthetic": True,
            })
        if parsed.path not in ("/api/v1/entries.json", "/api/v1/entries/sgv.json"):
            return self.respond(404, {"error": "Not found"})
        query = parse_qs(parsed.query)
        try:
            count = int(query.get("count", ["1"])[0])
            if not 1 <= count <= 288:
                raise ValueError()
        except ValueError:
            return self.respond(400, {"error": "count must be an integer from 1 to 288"})
        scenario = query.get("scenario", [scenario])[0]
        if scenario not in SCENARIOS:
            return self.respond(400, {"error": "Unknown scenario"})
        if scenario == "error":
            return self.respond(503, {"error": "Simulated upstream failure"})
        self.respond(200, make_entries(scenario, count))

    def do_POST(self):
        if urlsplit(self.path).path != "/simulator/scenario":
            return self.respond(404, {"error": "Not found"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 1 <= length <= 1024:
                raise ValueError()
            payload = json.loads(self.rfile.read(length))
            scenario = payload["scenario"]
            if scenario not in SCENARIOS:
                raise ValueError()
        except (ValueError, KeyError, TypeError):
            return self.respond(400, {"error": "Expected JSON with a valid scenario"})
        with self.server.state_lock:
            self.server.scenario = scenario
        self.respond(200, {"scenario": scenario})

    def log_message(self, fmt, *args):
        # Do not log query strings: real Nightscout clients may put tokens there.
        print(f"{self.client_address[0]} {self.command} {urlsplit(self.path).path}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="Use 0.0.0.0 to accept the Wemos on your LAN")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--scenario", choices=SCENARIOS, default="cycle")
    args = parser.parse_args()
    server = SimulatorServer((args.host, args.port), args.scenario)
    print(f"Synthetic Nightscout API listening on {args.host}:{args.port}. Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
