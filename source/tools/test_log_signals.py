import unittest

from log_signals import SIGNALS, decode, row


class LogSignalsTest(unittest.TestCase):
    def test_reads_only(self):
        for _, request, _ in SIGNALS:
            self.assertTrue(request.startswith(("22", "01")), request)

    def test_this_cars_idle_capture(self):
        """Replies read from the F82 M4 at idle on 2026-11-23 (docs/M4_S55.md)."""
        replies = {"baro": "64a1", "boost_abs": "3243", "boost_target_abs": "64a0", "lambda": "0fda",
                   "rail": "07c8", "oil_temp": "00eb", "coolant": "6b", "oil_abs": "0d19",
                   "timing1": "ffdf", "rpm": "0ab0", "speed": "00", "intake": "60"}
        values = {name: decode(name, bytes.fromhex(data)) for name, data in replies.items()}
        line = row(12.345, values)
        self.assertEqual(line["t_s"], "12.35")
        self.assertEqual(line["rpm"], "684")
        self.assertEqual(line["speed_mph"], "0.0")
        self.assertEqual(line["boost_psi"], "-0.02")
        self.assertEqual(line["afr"], "14.56")
        self.assertEqual(line["rail_bar"], "199.2")
        self.assertEqual(line["oil_c"], "23.5")
        self.assertEqual(line["coolant_c"], "32.2")
        self.assertEqual(line["oil_psi"], "34.0")
        self.assertEqual(line["timing1_deg"], "-3.3")
        self.assertEqual(line["timing2_deg"], "")       # not read yet: blank, never a guess

    def test_sentinels_and_engine_off(self):
        self.assertIsNone(decode("lambda", bytes.fromhex("ffeb")))      # engine off
        self.assertIsNone(decode("timing1", bytes.fromhex("ffff")))
        self.assertIsNone(decode("coolant", bytes.fromhex("ff")))
        self.assertAlmostEqual(decode("coolant", bytes.fromhex("0bf8")), 33.25)
        self.assertIsNone(decode("rpm", bytes.fromhex("0a")))
        line = row(0, {"oil_abs": decode("oil_abs", bytes.fromhex("0406")), "baro": 100.629})
        self.assertEqual(line["oil_psi"], "0.3")    # 1.030 abs - 1.006 ambient


if __name__ == "__main__":
    unittest.main()
