"""End-to-end: Python strategy -> gRPC -> Go execution hub. Skipped if Go isn't installed."""
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


@unittest.skipUnless(shutil.which("go"), "Go toolchain not installed")
class GoHub(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        exe = cls.tmp / "exechub"
        subprocess.run(["go", "build", "-o", str(exe), "."], cwd=ROOT / "services/exechub", check=True, timeout=300)
        cls.grpc_port, cls.http_port = free_port(), free_port()
        cls.proc = subprocess.Popen([str(exe), "-grpc", f"127.0.0.1:{cls.grpc_port}", "-http", f"127.0.0.1:{cls.http_port}",
                                     "-journal", str(cls.tmp / "j.jsonl")], stderr=subprocess.PIPE)
        from tradebot.hubclient import HubClient
        cls.client = HubClient(f"127.0.0.1:{cls.grpc_port}")
        for _ in range(50):
            try:
                cls.client.state()
                break
            except Exception:
                time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)
        cls.proc.stderr.close()

    def sub(self, cid, **kw):
        args = dict(client_order_id=cid, asset_id="BTC_USD", asset_class="crypto", tier="crypto_major", side="buy", qty=0.01,
                    bid=83000.0, ask=83001.0, quote_ts_ms=int(time.time() * 1000), equity=10_000.0)
        args.update(kw)
        return self.client.submit(**args)

    def test_1_fill_duplicate_and_latency(self):
        from tradebot.pb import trading_pb2 as pb
        t0 = time.perf_counter()
        r = self.sub("e2e-1")
        rtt_ms = (time.perf_counter() - t0) * 1000
        self.assertEqual(r.status, pb.FILLED)
        self.assertLess(rtt_ms, 200)  # local round trip; generous bound for slow CI
        self.assertEqual(self.sub("e2e-1").status, pb.DUPLICATE)

    def test_2_hub_enforces_its_own_limits(self):
        from tradebot.pb import trading_pb2 as pb
        r = self.sub("e2e-big", qty=1.0)
        self.assertEqual(r.status, pb.REJECTED)
        self.assertTrue(any("tier cap" in x for x in r.reasons))
        r = self.sub("e2e-penny", asset_id="XYZ", asset_class="stock", tier="penny_stock", bid=0.45, ask=0.46, qty=100)
        self.assertEqual(r.status, pb.REJECTED)  # spread 2.2% > 1.5% and market order on a limit-only tier

    def test_3_halt_from_dashboard_api_and_exits_only(self):
        from tradebot.pb import trading_pb2 as pb
        req = urllib.request.Request(f"http://127.0.0.1:{self.http_port}/api/v1/control/halt", data=b'{"reason":"test"}', method="POST",
                                     headers={"Content-Type": "application/json"})
        st = json.loads(urllib.request.urlopen(req, timeout=5).read())
        self.assertTrue(st["halted"])
        self.assertEqual(self.sub("e2e-new").status, pb.REJECTED)
        self.assertEqual(self.sub("e2e-exit", side="sell", reduce_only=True).status, pb.FILLED)
        bad = urllib.request.Request(f"http://127.0.0.1:{self.http_port}/api/v1/control/resume", data=b"{}", method="POST",
                                     headers={"Origin": "https://evil.example"})
        with self.assertRaises(urllib.error.HTTPError):
            urllib.request.urlopen(bad, timeout=5)  # cross-site control refused
        self.client.resume("test")
        self.assertFalse(self.client.state().halted)

    def test_4_dashboard_served(self):
        html = urllib.request.urlopen(f"http://127.0.0.1:{self.http_port}/", timeout=5).read().decode()
        self.assertIn("exechub control", html)


if __name__ == "__main__":
    unittest.main()
