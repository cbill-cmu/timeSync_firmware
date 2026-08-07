#!/usr/bin/env python3
"""Analyze a stm32_logger.py CSV log: for each sensor, how far (in time)
was each captured edge from the nearest GPS-PPS second boundary, and how
consistent (jittery) was the sensor's own inter-event spacing.

This is the direct test of "the radar chirps at some time, the PPS edge is
at 21:30:20.432, how far away was the radar from that edge" -- run this
against a log captured while GPS has a real PPS anchor (precision_source ==
"stm32_pps" rows) and a waveform generator (or real radar/camera) is
feeding one or more of the other capture channels.

Only stm32_pps-tagged rows are used -- host_ntp_fallback rows carry tens of
milliseconds of host-side jitter and would swamp anything meaningful here.

    uv run python pps_offset_stats.py log.csv
    uv run python pps_offset_stats.py log.csv --sensor radar_sync
"""
import argparse
import csv
import datetime
import statistics
import sys
from collections import defaultdict


def fmt_ms(ms: float) -> str:
    if abs(ms) >= 1.0:
        return f"{ms:.3f} ms"
    return f"{ms * 1000:.1f} us"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv_path")
    ap.add_argument("--sensor", default=None, help="only report this sensor (default: all)")
    args = ap.parse_args()

    # sensor -> list of (edge_utc_datetime, offset_from_pps_ms), time-ordered as read from the CSV
    rows_by_sensor = defaultdict(list)
    with open(args.csv_path, newline="") as fh:
        for row in csv.DictReader(fh):
            if row["precision_source"] != "stm32_pps":
                continue
            sensor = row["sensor"]
            if args.sensor is not None and sensor != args.sensor:
                continue
            edge_dt = datetime.datetime.fromisoformat(row["sensor_edge_utc"])
            rows_by_sensor[sensor].append((edge_dt, float(row["offset_from_pps_ms"])))

    if not rows_by_sensor:
        print("No stm32_pps-tagged rows found -- either the log has no PPS anchor yet, "
              "or --sensor filtered everything out.", file=sys.stderr)
        sys.exit(1)

    for sensor, rows in sorted(rows_by_sensor.items()):
        print(f"\n=== {sensor} ({len(rows)} events) ===")

        if sensor != "gps_pps":
            offsets = [off for _, off in rows]
            print("Offset from nearest PPS edge:")
            print(f"  mean   = {fmt_ms(statistics.mean(offsets))}")
            if len(offsets) > 1:
                print(f"  stddev = {fmt_ms(statistics.stdev(offsets))}")
            print(f"  min    = {fmt_ms(min(offsets))}")
            print(f"  max    = {fmt_ms(max(offsets))}")

        if len(rows) > 1:
            deltas_ms = [(b[0] - a[0]).total_seconds() * 1000 for a, b in zip(rows, rows[1:])]
            print("Inter-event spacing:")
            print(f"  mean   = {fmt_ms(statistics.mean(deltas_ms))}")
            if len(deltas_ms) > 1:
                print(f"  stddev = {fmt_ms(statistics.stdev(deltas_ms))}")
            print(f"  min    = {fmt_ms(min(deltas_ms))}")
            print(f"  max    = {fmt_ms(max(deltas_ms))}")


if __name__ == "__main__":
    main()
