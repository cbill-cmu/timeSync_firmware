#!/usr/bin/env python3
"""Start ONE radar framing so SYNC_OUT (ball M3 / pin 24 / TP17) pulses.

SYNC_OUT only toggles while the radar is actively running frames. This just
configures + starts the radar and holds it running until Ctrl-C, giving the
scope something to trigger on.

Default frame_period is 500 ms (2 Hz) -- deliberately SLOW so the pulse is
trivial to find. Once you see it, re-run with --frame-period 50 for the real
20 Hz used in the study.

    uv run python syncout_test.py --config victim.yaml
    uv run python syncout_test.py --config victim.yaml --frame-period 50
"""
import argparse, logging, signal, sys
import yaml


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="victim.yaml")
    ap.add_argument("--port", default=None, help="override serial port")
    ap.add_argument("--frame-period", type=float, default=500.0,
                    help="ms between frames; large = easy to see (default 500 = 2 Hz)")
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s [syncout] %(message)s")
    log = logging.getLogger("syncout")

    with open(args.config) as fh:
        cfg = yaml.safe_load(fh)["radar"]
    port = args.port or cfg["port"]

    import xwr.radar as xradar
    RadarClass = getattr(xradar, cfg.get("device", "AWR1843"))
    r = RadarClass(port=port, name="syncout")
    r.setup(
        frequency=cfg["frequency"], idle_time=cfg["idle_time"],
        adc_start_time=cfg["adc_start_time"], ramp_end_time=cfg["ramp_end_time"],
        tx_start_time=cfg["tx_start_time"], freq_slope=cfg["freq_slope"],
        adc_samples=cfg["adc_samples"], sample_rate=cfg["sample_rate"],
        frame_length=cfg["frame_length"], frame_period=args.frame_period,
    )
    r.start()
    hz = 1000.0 / args.frame_period
    log.info("RUNNING: frame_period=%.0f ms (%.2f Hz).", args.frame_period, hz)
    log.info("SYNC_OUT should now pulse once per frame -> expect a pulse every %.0f ms.",
             args.frame_period)
    log.info("Ctrl-C to stop (watch the scope trace go flat -- that confirms it's the radar).")

    stopped = {"d": False}
    def sh(*_):
        if not stopped["d"]:
            stopped["d"] = True
            try: r.stop()
            finally: log.info("stopped -- SYNC_OUT should go flat now.")
        sys.exit(0)
    signal.signal(signal.SIGINT, sh); signal.signal(signal.SIGTERM, sh)
    signal.pause()


if __name__ == "__main__":
    main()