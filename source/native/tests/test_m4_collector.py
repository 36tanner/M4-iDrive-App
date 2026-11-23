"""Run the M4/S55 collector against a loopback HSFZ gateway that answers like a 2015 F82 M4.

Payloads marked OBDb are recorded responses from the OBDb BMW-M4 repository (CC BY-SA 4.0)."""
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

BIN = str(Path(__file__).resolve().parents[1] / "build/boostdeck-collector-m4")
TIRE_FL = bytes.fromhex("0526033900020817" "1b086f8d6300")          # OBDb, 2.05 bar, set 2.075, 23 C
TIRE_RL = bytes.fromhex("08350339026c0709" "d00715c96300")          # OBDb, 1.90 bar, set 2.00, 9 C
BATTERY = bytes.fromhex("010e74b40a3c" + "00" * 20 + "ffff")        # OBDb, 13.469 V
DME = {
    "224201": bytes.fromhex("6735"),        # OBDb baro 103.207 kPa
    "224205": bytes.fromhex("6000"),        # 192 kPa absolute: 12.88 psi of boost
    "225889": bytes.fromhex("100c"),        # OBDb lambda 1.00294
    "224A49": bytes.fromhex("ffdf"),        # OBDb timing -3.3
    "22580F": bytes.fromhex("60"),          # OBDb intake 24 C
    "224408": bytes.fromhex("03ce"),        # OBDb oil 97.4 C
    "22586F": bytes.fromhex("03eb"),        # OBDb oil pressure 1.003 bar
    "224300": bytes.fromhex("b6"),          # OBDb coolant 88.5 C, one byte
    "224AB0": bytes.fromhex("ce9a"),        # set point 206.6 kPa: 15.0 psi of boost
    "22402B": BATTERY,
    "224A2E": bytes.fromhex("1b89"),        # OBDb rev limit 7049
}
DSC = {"22DC98": TIRE_FL, "22DC99": TIRE_FL, "22DC9A": TIRE_RL, "22DC9B": TIRE_RL}


def frame(ctrl, body):
    return struct.pack(">IH", len(body), ctrl) + body


def exact(conn, n):
    out = b""
    while len(out) < n:
        data = conn.recv(n - len(out))
        if not data:
            raise EOFError
        out += data
    return out


class M4CollectorTest(unittest.TestCase):
    def run_collector(self, cycles, extra=()):
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("127.0.0.1", 0))
        udp.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(5)
        requests, errors = [], []

        def gateway():
            try:
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(5)
                    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    while True:
                        first = conn.recv(1)
                        if not first:
                            break
                        length, ctrl = struct.unpack(">IH", first + exact(conn, 5))
                        req = exact(conn, length)
                        self.assertEqual(ctrl, 1)
                        self.assertEqual(req[0], 0xF4)
                        ecu, uds = req[1], req[2:]
                        requests.append((ecu, uds.hex().upper()))
                        self.assertIn(uds[0], (0x22, 0x01))     # reads only
                        key = uds.hex().upper()
                        if ecu == 0x12 and key == "0123":
                            reply = bytes([0x41, 0x23, 0x01, 0x90])          # 40.0 bar
                        elif ecu == 0x12 and key in DME:
                            reply = bytes([0x62]) + uds[1:] + DME[key]
                        elif ecu == 0x29 and key in DSC:
                            reply = bytes([0x62]) + uds[1:] + DSC[key]
                        else:
                            reply = bytes([0x7F, uds[0], 0x31])
                        conn.sendall(frame(2, req) + frame(1, bytes([ecu, 0xF4]) + reply))  # ack, reply
            except Exception as exc:  # noqa: BLE001 - surfaced by the test
                errors.append(exc)

        thread = threading.Thread(target=gateway, daemon=True)
        thread.start()
        try:
            result = subprocess.run([BIN, "--host", "127.0.0.1", "--port", str(server.getsockname()[1]),
                                     "--udp-port", str(udp.getsockname()[1]), "--cycles", str(cycles),
                                     *extra], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            udp.setblocking(False)
            packets = []
            while True:
                try:
                    packets.append(udp.recv(1024).decode().split())
                except BlockingIOError:
                    break
            thread.join(3)
            if errors:
                raise errors[0]
            return packets, requests, result.stderr
        finally:
            udp.close()
            server.close()

    def test_s55_profile_extras_and_logging(self):
        with tempfile.TemporaryDirectory() as tmp:
            flag = Path(tmp) / "logging-enabled"
            flag.touch()
            logs = Path(tmp) / "logs"
            packets, requests, stderr = self.run_collector(
                150, ("--log-dir", str(logs), "--log-flag", str(flag)))
            csv = sorted(logs.glob("log-*.csv"))
            self.assertEqual(len(csv), 1, stderr)
            rows = csv[0].read_text().splitlines()

        self.assertIn("F82 M4 S55 profile", stderr)
        asked = {uds for _, uds in requests}
        # The pre-2019 DME refuses 56D7, and DA12 belongs to the EGS: neither is asked for.
        self.assertNotIn("2256D7", asked)
        self.assertNotIn("22DA12", asked)
        self.assertNotIn(0x18, {ecu for ecu, _ in requests})
        for key in ("0123", "224408", "224AB0", "22402B", "224A2E", "22DC98"):
            self.assertIn(key, asked)

        gauges = [p for p in packets if p[0] == "NBG4"]
        extras = [p for p in packets if p[0] == "NBX1"]
        self.assertGreater(len(gauges), 100)
        self.assertGreater(len(extras), 5)
        g = gauges[-1]
        valid = int(g[3])
        values = [float(g[5 + 2 * i]) for i in range(8)]
        for slot in range(8):
            self.assertTrue(valid & (1 << slot), f"slot {slot} missing: {g}")
        self.assertAlmostEqual(values[0], (192 - 0x6735 / 256) * .145037738, places=3)
        self.assertAlmostEqual(values[1], 0x100C * 16 / 65535 * 14.7, places=3)
        self.assertAlmostEqual(values[2], -3.3)
        self.assertAlmostEqual(values[3], 24)
        self.assertAlmostEqual(values[4], 97.4)       # oil temperature in the transmission slot
        self.assertAlmostEqual(values[5], 40)         # rail pressure from PID 23
        self.assertAlmostEqual(values[6], 1.003)
        self.assertAlmostEqual(values[7], 88.5)       # one-byte coolant

        x = extras[-1]
        xvalid = int(x[2])
        xv = [float(x[4 + 2 * i]) for i in range(15)]
        for index in (0, 1, 2, 3, 7, 11):   # target, battery, rev limit, FL tire (read first)
            self.assertTrue(xvalid & (1 << index), f"extra {index} missing: {x}")
        self.assertAlmostEqual(xv[0], (0xCE9A / 256 - 0x6735 / 256) * .145037738, places=3)
        self.assertAlmostEqual(xv[1], 13.469, places=3)
        self.assertAlmostEqual(xv[2], 7049)
        self.assertAlmostEqual(xv[3], 2.05)
        self.assertAlmostEqual(xv[7], 2.075)
        self.assertAlmostEqual(xv[11], 23)

        self.assertTrue(rows[0].startswith("# BoostDeck M4 log"))
        self.assertEqual(rows[1].split(",")[0], "t_s")
        self.assertGreater(len(rows), 20)
        last = rows[-1].split(",")
        self.assertEqual(len(last), 12)
        self.assertAlmostEqual(float(last[5]), 97.4)
        self.assertAlmostEqual(float(last[10]), 13.47, places=2)

    def test_no_flag_no_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            logs = Path(tmp) / "logs"
            self.run_collector(20, ("--log-dir", str(logs), "--log-flag", str(Path(tmp) / "absent")))
            self.assertFalse(logs.exists())


class M4ShiftDeckTest(unittest.TestCase):
    def test_frames_carry_the_speed_sample_time(self):
        """The M4 worker adds kph_ms so the performance timers use the time speed was read."""
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("127.0.0.1", 0))
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(5)

        def gateway():
            conn, _ = server.accept()
            with conn:
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                while True:
                    first = conn.recv(1)
                    if not first:
                        break
                    length, _ = struct.unpack(">IH", first + exact(conn, 5))
                    req = exact(conn, length)
                    ecu, pid = req[1], req[3]
                    data = b"\x1a\xf8" if pid == 0x0C else b"\x58"     # 1726 rpm, 88 km/h
                    conn.sendall(frame(2, req) + frame(1, bytes([ecu, 0xF4, 0x41, pid]) + data))

        threading.Thread(target=gateway, daemon=True).start()
        try:
            result = subprocess.run([str(Path(BIN).with_name("shift-deck-m4")), "--host", "127.0.0.1",
                                     "--port", str(server.getsockname()[1]), "--udp-port",
                                     str(udp.getsockname()[1]), "--cycles", "12"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            udp.settimeout(2)
            frames = [struct.unpack("=QiiQ", udp.recv(64)) for _ in range(12)]
        finally:
            udp.close()
            server.close()
        sampled, rpm, kph, kph_ms = frames[-1]
        self.assertEqual((rpm, kph), (1726, 88))
        self.assertTrue(0 < kph_ms <= sampled)
        self.assertLess(sampled - kph_ms, 200)


if __name__ == "__main__":
    unittest.main()
