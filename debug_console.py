#!/usr/bin/env python3
"""Auto-reconnecting serial console for watching boot output across power cycles.

The AWR1843AOP's CP2105 UART bridge loses power on a full power cycle, so
plain terminals (screen/picocom) just die when the device node disappears.
This script instead polls for the port to (re)appear and keeps logging
across drops, so you can power-cycle the board freely and not miss anything.

    uv run python debug_console.py --config victim.yaml
    uv run python debug_console.py --port /dev/serial/by-id/... --log boot.log
"""
import argparse
import datetime
import sys
import time

import serial
import yaml


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="victim.yaml")
    ap.add_argument("--port", default=None, help="override serial port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", default=None, help="also append output to this file")
    ap.add_argument(
        "--retry", type=float, default=0.1,
        help="seconds between reconnect attempts while port is missing")
    args = ap.parse_args()

    if args.port is not None:
        port = args.port
    else:
        with open(args.config) as fh:
            port = yaml.safe_load(fh)["radar"]["port"]

    log_fh = open(args.log, "a") if args.log else None

    def emit(msg: str) -> None:
        line = f"{datetime.datetime.now().isoformat(timespec='milliseconds')} {msg}"
        print(line, flush=True)
        if log_fh is not None:
            log_fh.write(line + "\n")
            log_fh.flush()

    emit(f"[console] watching {port} @ {args.baud} baud -- Ctrl-C to quit")

    connected = False
    try:
        while True:
            try:
                with serial.Serial(port, args.baud, timeout=0.2) as ser:
                    if not connected:
                        emit("[console] connected")
                        connected = True
                    buf = b""
                    while True:
                        chunk = ser.read(ser.in_waiting or 1)
                        if not chunk:
                            continue
                        buf += chunk
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            emit(line.decode("utf-8", errors="replace").rstrip("\r"))
            except (serial.SerialException, OSError):
                if connected:
                    emit("[console] disconnected -- waiting for device to reappear...")
                    connected = False
                time.sleep(args.retry)
    except KeyboardInterrupt:
        pass
    finally:
        if log_fh is not None:
            log_fh.close()


if __name__ == "__main__":
    main()
