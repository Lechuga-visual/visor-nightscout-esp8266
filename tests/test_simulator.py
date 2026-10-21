import json
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from simulator.server import SimulatorServer


class SimulatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = SimulatorServer(("127.0.0.1", 0), "normal")
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, path, payload=None):
        request = Request(self.base + path)
        if payload is not None:
            request = Request(self.base + path, json.dumps(payload).encode(), {"Content-Type": "application/json"})
        try:
            response = urlopen(request, timeout=3)
        except HTTPError as error:
            response = error
        with response:
            return response.status, json.load(response)

    def test_reading_contract_and_millisecond_timestamp(self):
        status, readings = self.request("/api/v1/entries.json?count=1&scenario=normal")
        self.assertEqual(status, 200)
        self.assertEqual(len(readings), 1)
        entry = readings[0]
        self.assertEqual(entry["type"], "sgv")
        self.assertIsInstance(entry["sgv"], int)
        self.assertEqual(entry["direction"], "Flat")
        self.assertTrue(entry["synthetic"])
        self.assertGreaterEqual(time.time() * 1000 - entry["date"], 0)
        self.assertLess(time.time() * 1000 - entry["date"], 301000)

    def test_history_newest_first_at_five_minute_intervals(self):
        status, entries = self.request("/api/v1/entries/sgv.json?count=12&scenario=rising")
        self.assertEqual(status, 200)
        self.assertEqual(len(entries), 12)
        self.assertEqual(len({e["_id"] for e in entries}), 12)
        for newer, older in zip(entries, entries[1:]):
            self.assertEqual(newer["date"] - older["date"], 300000)
            self.assertGreaterEqual(newer["sgv"], older["sgv"])

    def test_stale_reading_is_at_least_thirty_minutes_old(self):
        _, entries = self.request("/api/v1/entries.json?scenario=stale")
        self.assertGreaterEqual(time.time() * 1000 - entries[0]["date"], 1800000)

    def test_high_low_and_trend_scenarios(self):
        for scenario, direction in (("rising", "SingleUp"), ("falling", "SingleDown")):
            with self.subTest(scenario=scenario):
                status, entries = self.request(f"/api/v1/entries.json?scenario={scenario}")
                self.assertEqual(status, 200)
                self.assertEqual(entries[0]["direction"], direction)
        _, high = self.request("/api/v1/entries.json?scenario=high")
        _, low = self.request("/api/v1/entries.json?scenario=low")
        self.assertGreater(high[0]["sgv"], 180)
        self.assertLess(low[0]["sgv"], 70)

    def test_empty_and_upstream_error_are_distinct(self):
        self.assertEqual(self.request("/api/v1/entries.json?scenario=empty"), (200, []))
        self.assertEqual(self.request("/api/v1/entries.json?scenario=error")[0], 503)

    def test_invalid_queries(self):
        for query in ("count=0", "count=289", "count=no", "scenario=unknown"):
            with self.subTest(query=query):
                self.assertEqual(self.request("/api/v1/entries.json?" + query)[0], 400)
        self.assertEqual(self.request("/unknown")[0], 404)

    def test_change_scenario_applies_to_device_requests(self):
        self.assertEqual(self.request("/simulator/scenario", {"scenario": "low"})[0], 200)
        _, readings = self.request("/api/v1/entries.json?count=1")
        self.assertLess(readings[0]["sgv"], 70)
        self.assertEqual(self.request("/simulator/state")[1]["scenario"], "low")
        self.assertEqual(self.request("/simulator/scenario", {"scenario": "unknown"})[0], 400)

    def test_status(self):
        status, payload = self.request("/api/v1/status.json")
        self.assertEqual(status, 200)
        self.assertEqual(payload["settings"]["units"], "mg/dl")
        self.assertTrue(payload["synthetic"])


if __name__ == "__main__":
    unittest.main()
