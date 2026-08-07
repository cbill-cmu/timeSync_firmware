#!/usr/bin/env python3
"""Auto-reconnecting logger for the STM32 time-sync hub's debug serial stream.

Mirrors debug_console.py's reconnect-on-drop behavior, but additionally
parses tick-bearing lines ([stm32-hub] heartbeats, [capture] events) and
logs one CSV row per event: the sensor edge's own UTC time, the UTC time of
the nearest real GPS-PPS second boundary, and the difference between them --
this is the direct "how far was this sensor from the nearest PPS edge"
answer, not raw ticks/epoch-nanoseconds.

CSV columns:
  sensor              -- "gps_pps" / "radar_sync" / "camera_sync" / a
                         heartbeat status line ("heartbeat:N")
  sensor_edge_utc      -- UTC time (microsecond precision) this edge
                         happened at
  nearest_pps_edge_utc -- UTC time of the whole-second boundary this edge
                         is being measured against
  offset_from_pps_ms   -- sensor_edge_utc minus nearest_pps_edge_utc, in
                         milliseconds (positive = after that PPS edge,
                         negative = before the next one)
  precision_source     -- see below

precision_source per row:
  - "stm32_pps": the firmware itself supplied a PPS-anchored UTC value in
    the line (a capture-event line's "utc_ns=..." suffix, present once
    timesync.c has a PPS anchor -- see docs/system-design.md's tick->UTC
    mapping). Confirmed working with a PPS-capable module (EVK-M8T);
    microsecond-level hardware-latched precision, not this script's doing.
    Trust these rows for real timing analysis.
  - "host_ntp_fallback": no PPS anchor available yet, or this particular
    line doesn't carry one (e.g. the heartbeat line never has -- only
    capture events do). Falls back to this HOST machine's own wall-clock
    time, captured at the moment the line was received. This assumes the
    host (this PC today; the Jetson in the eventual deployment) is itself
    NTP-synced by the OS -- this script doesn't sync anything itself.

    Precision warning: this is NOT a substitute for PPS. It inherits
    whatever jitter exists in USB buffering, OS scheduling, and this
    script's own read loop -- typically low tens of milliseconds, versus
    PPS's microsecond-level hardware-latched precision. Rows are tagged so
    degraded-precision segments stay identifiable in post-processing --
    don't feed host_ntp_fallback rows into precision timing analysis
    (see pps_offset_stats.py, which filters to stm32_pps rows only).

    uv run python stm32_logger.py --port COM10 --csv log.csv
    uv run python stm32_logger.py --config victim.yaml --csv log.csv
"""
import argparse
import csv
import datetime
import re
import sys
import time

import serial
import serial.tools.list_ports

HEARTBEAT_RE = re.compile(r"\[stm32-hub\] alive \((\d+)\), tick=(\d+):(\d+)")
CAPTURE_RE = re.compile(r"\[capture\] sensor=(\S+) tick=(\d+):(\d+)(?: utc_ns=(-?\d+))?")

NS_PER_S = 1_000_000_000


def find_stlink_port() -> str | None:
    for p in serial.tools.list_ports.comports():
        if "STLink" in (p.description or "") or "ST-Link" in (p.description or ""):
            return p.device
    return None


def parse_tick_line(line: str):
    """Returns (sensor_name, firmware_utc_ns_or_None) if line carries a tick
    value, else None. firmware_utc_ns is the PPS-anchored value the
    firmware itself computed (capture-event lines only, once anchored) --
    when present this is authoritative and should be preferred over any
    host-side fallback timestamp."""
    m = HEARTBEAT_RE.search(line)
    if m:
        heartbeat_n, _hi, _lo = m.groups()
        return f"heartbeat:{heartbeat_n}", None
    m = CAPTURE_RE.search(line)
    if m:
        sensor, _hi, _lo, utc_ns = m.groups()
        return sensor, (int(utc_ns) if utc_ns is not None else None)
    return None


def nearest_pps_edge_ns(utc_ns: int) -> int:
    """UTC nanoseconds of the whole-second boundary closest to utc_ns. Real
    PPS edges always land exactly on a whole second (see timesync.c)."""
    frac = utc_ns % NS_PER_S
    floor_second = utc_ns - frac
    if frac > NS_PER_S // 2:
        return floor_second + NS_PER_S
    return floor_second


def iso_utc(ns: int) -> str:
    return datetime.datetime.fromtimestamp(ns / 1e9, tz=datetime.timezone.utc).isoformat(
        timespec="microseconds")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default=None, help="victim.yaml-style config with stm32_hub.port")
    ap.add_argument("--port", default=None, help="override serial port (else auto-detect ST-LINK VCP)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--csv", default=None, help="log tick-bearing lines here as CSV")
    ap.add_argument("--log", default=None, help="also append all console output to this file")
    ap.add_argument(
        "--retry", type=float, default=0.1,
        help="seconds between reconnect attempts while port is missing")
    args = ap.parse_args()

    if args.port is not None:
        port = args.port
    elif args.config is not None:
        import yaml
        with open(args.config) as fh:
            port = yaml.safe_load(fh)["stm32_hub"]["port"]
    else:
        port = find_stlink_port()
        if port is None:
            print("No ST-LINK VCP found and no --port/--config given.", file=sys.stderr)
            sys.exit(1)

    log_fh = open(args.log, "a") if args.log else None
    csv_fh = open(args.csv, "a", newline="") if args.csv else None
    csv_writer = None
    if csv_fh is not None:
        csv_writer = csv.writer(csv_fh)
        if csv_fh.tell() == 0:
            csv_writer.writerow([
                "sensor", "sensor_edge_utc", "nearest_pps_edge_utc",
                "offset_from_pps_ms", "precision_source",
            ])
            csv_fh.flush()

    def emit(msg: str) -> None:
        line = f"{datetime.datetime.now().isoformat(timespec='milliseconds')} {msg}"
        print(line, flush=True)
        if log_fh is not None:
            log_fh.write(line + "\n")
            log_fh.flush()

    emit(f"[stm32_logger] watching {port} @ {args.baud} baud -- Ctrl-C to quit")

    connected = False
    try:
        while True:
            try:
                with serial.Serial(port, args.baud, timeout=0.2) as ser:
                    if not connected:
                        emit("[stm32_logger] connected")
                        connected = True
                    buf = b""
                    while True:
                        chunk = ser.read(ser.in_waiting or 1)
                        if not chunk:
                            continue
                        recv_utc = datetime.datetime.now(datetime.timezone.utc)
                        buf += chunk
                        while b"\n" in buf:
                            raw, buf = buf.split(b"\n", 1)
                            line = raw.decode("utf-8", errors="replace").rstrip("\r")
                            emit(line)

                            parsed = parse_tick_line(line)
                            if parsed is not None and csv_writer is not None:
                                sensor, fw_utc_ns = parsed
                                if fw_utc_ns is not None:
                                    precision_source = "stm32_pps"
                                    edge_utc_ns = fw_utc_ns
                                else:
                                    precision_source = "host_ntp_fallback"
                                    edge_utc_ns = int(recv_utc.timestamp() * 1e9)

                                pps_edge_ns = nearest_pps_edge_ns(edge_utc_ns)
                                offset_ms = (edge_utc_ns - pps_edge_ns) / 1e6

                                csv_writer.writerow([
                                    sensor,
                                    iso_utc(edge_utc_ns),
                                    iso_utc(pps_edge_ns),
                                    f"{offset_ms:.3f}",
                                    precision_source,
                                ])
                                csv_fh.flush()
            except (serial.SerialException, OSError):
                if connected:
                    emit("[stm32_logger] disconnected -- waiting for device to reappear...")
                    connected = False
                time.sleep(args.retry)
    except KeyboardInterrupt:
        pass
    finally:
        if log_fh is not None:
            log_fh.close()
        if csv_fh is not None:
            csv_fh.close()


if __name__ == "__main__":
    main()
