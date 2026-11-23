#!/usr/bin/env python3
"""Log F82 M4 / S55 engine signals to CSV from a laptop on wired ENET. Read only.

  py tools/log_signals.py --local 169.254.x.x --gateway 169.254.61.31

Press Ctrl+C to stop. Every request is a UDS 22 or OBD 01 read on the DME (0x12); the tool sends nothing
else. Signal provenance and this car's capture: docs/M4_S55.md.
"""

import argparse
import csv
import socket
import sys
import time
from datetime import datetime
from pathlib import Path

from probe_signals import enet_address, exchange

PSI_PER_KPA = 0.145037738

# name, request, every Nth cycle (1 = every cycle)
FAST, SLOW = 1, 10
SIGNALS = (
    ("rpm", "010C", FAST), ("speed", "010D", FAST), ("boost_abs", "224205", FAST),
    ("boost_target_abs", "224AB0", FAST), ("lambda", "225889", FAST), ("rail", "0123", FAST),
    ("timing1", "224A49", FAST), ("timing2", "224A4A", FAST), ("timing3", "224A4C", FAST),
    ("timing4", "224A4D", FAST),
    ("baro", "224201", SLOW), ("intake", "22580F", SLOW), ("oil_temp", "224408", SLOW),
    ("coolant", "224300", SLOW), ("oil_abs", "22586F", SLOW),
)
COLUMNS = ("t_s", "rpm", "speed_mph", "boost_psi", "boost_target_psi", "afr", "rail_bar",
           "timing1_deg", "timing2_deg", "timing3_deg", "timing4_deg",
           "intake_c", "oil_c", "coolant_c", "oil_psi")


def be16(data):
    return data[0] << 8 | data[1]


def decode(name, data):
    """A decoded value, or None when the reply is short, a sentinel or implausible."""
    try:
        if name == "rpm":
            return be16(data) / 4 if len(data) == 2 else None
        if name == "speed":
            return data[0] if len(data) == 1 else None
        if len(data) == 1 and name in ("intake", "coolant"):
            return None if data[0] == 0xFF else data[0] * .75 - 48
        if len(data) != 2:
            return None
        raw = be16(data)
        if name in ("boost_abs", "boost_target_abs", "baro"):
            value = raw / (128 if name == "boost_abs" else 256)
            return value if 20 <= value <= 450 else None
        if name == "lambda":
            value = raw * 16 / 65535
            return value if .5 <= value <= 2 else None
        if name == "rail":
            return raw / 10
        if name.startswith("timing"):
            if raw == 0xFFFF:
                return None
            value = (raw - 65536 if raw & 0x8000 else raw) / 10
            return value if -64 <= value <= 64 else None
        if name == "coolant":   # 2019+ DMEs answer with two bytes, in tenths of a kelvin
            return None if raw == 0xFFFF else raw / 10 - 273.15
        if name == "oil_temp":
            return None if raw == 0xFFFF else raw / 10
        if name == "oil_abs":
            return None if raw == 0xFFFF else raw / 1000
    except IndexError:
        return None
    return None


def row(elapsed, values):
    """One CSV row from the latest decoded values; blanks where a value is missing."""
    v = values.get
    baro = v("baro")
    out = {"t_s": f"{elapsed:.2f}"}

    def put(column, value, fmt="{:.2f}"):
        out[column] = "" if value is None else fmt.format(value)

    put("rpm", v("rpm"), "{:.0f}")
    put("speed_mph", None if v("speed") is None else v("speed") / 1.609344, "{:.1f}")
    put("boost_psi", None if None in (v("boost_abs"), baro) else (v("boost_abs") - baro) * PSI_PER_KPA)
    put("boost_target_psi",
        None if None in (v("boost_target_abs"), baro) else (v("boost_target_abs") - baro) * PSI_PER_KPA)
    put("afr", None if v("lambda") is None else v("lambda") * 14.7)
    put("rail_bar", v("rail"), "{:.1f}")
    for cylinder in range(1, 5):
        put(f"timing{cylinder}_deg", v(f"timing{cylinder}"), "{:.1f}")
    put("intake_c", v("intake"), "{:.1f}")
    put("oil_c", v("oil_temp"), "{:.1f}")
    put("coolant_c", v("coolant"), "{:.1f}")
    # 586F is absolute on the S55: gauge pressure is it minus ambient.
    put("oil_psi", None if None in (v("oil_abs"), baro) else max(0, v("oil_abs") - baro / 100) * 14.5037738,
        "{:.1f}")
    return out


def connect(args):
    connection = socket.create_connection((args.gateway, 6801), timeout=2, source_address=(args.local, 0))
    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return connection


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--local", required=True, type=enet_address, help="laptop's wired ENET IPv4")
    parser.add_argument("--gateway", required=True, type=enet_address, help="diagnostic gateway IPv4")
    parser.add_argument("--out", type=Path, help="CSV path (default: m4-log-<date-time>.csv here)")
    parser.add_argument("--minutes", type=float, default=0, help="stop after this long (0: until Ctrl+C)")
    args = parser.parse_args()
    if args.local == args.gateway:
        parser.error("use separate local and gateway IPs")
    out = args.out or Path(f"m4-log-{datetime.now():%Y%m%d-%H%M%S}.csv")
    values, start, cycle, rows, next_print = {}, time.monotonic(), 0, 0, 0.0
    connection = None
    print(f"Logging to {out.resolve()}  (Ctrl+C to stop)", flush=True)
    with open(out, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS)
        writer.writeheader()
        try:
            while not args.minutes or time.monotonic() - start < args.minutes * 60:
                try:
                    if connection is None:
                        connection = connect(args)
                    for name, request, every in SIGNALS:
                        if cycle % every:
                            continue
                        result = exchange(connection, 0x12, bytes.fromhex(request))
                        values[name] = decode(name, bytes.fromhex(result["data"])) if "data" in result else None
                except (OSError, TimeoutError, ValueError) as error:
                    print(f"Connection problem ({error}); retrying", file=sys.stderr, flush=True)
                    if connection is not None:
                        connection.close()
                    connection = None
                    time.sleep(1)
                    continue
                elapsed = time.monotonic() - start
                line = row(elapsed, values)
                writer.writerow(line)
                rows += 1
                cycle += 1
                if elapsed >= next_print:
                    handle.flush()
                    print(f"{elapsed:7.1f}s  rpm {line['rpm'] or '--':>5}  boost {line['boost_psi'] or '--':>6} psi"
                          f"  AFR {line['afr'] or '--':>5}  oil {line['oil_c'] or '--':>5} C"
                          f"  rows {rows}", flush=True)
                    next_print = elapsed + 1
        except KeyboardInterrupt:
            pass
        finally:
            if connection is not None:
                connection.close()
    print(f"Saved {rows} rows to {out.resolve()}")


if __name__ == "__main__":
    main()
