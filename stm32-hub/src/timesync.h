/* timesync.h -- PPS-anchored tick->UTC mapping. See
 * ../../docs/system-design.md ("GPS is special only in that it is also the
 * thing that converts ticks into real UTC"): pair a GPS_PPS capture event's
 * tick with the UTC second that edge marks the start of, then every other
 * sensor's tick converts to UTC via that one linear anchor.
 *
 * Status: implemented and internally consistent, but NOT yet verified
 * against a real PPS edge -- capture.c's GPS_PPS channel (TIM2_CH1/PA0)
 * has never seen a real pulse as of this writing (needs PA0 wired to the
 * GPS module's TIMEPULSE1 pin, and confirmation this EVK-7P revision
 * exposes it at all -- see stm32-hub/README.md). Until then
 * timesync_has_anchor() simply stays 0 and every tick is relative-only,
 * per system-design.md's "flagged relative-only, no UTC yet" philosophy.
 */
#ifndef TIMESYNC_H
#define TIMESYNC_H

#include "stm32l4xx.h"

/* Call when a SENSOR_GPS_PPS capture event pops from capture.c's ring
 * buffer. Records this as the pending edge to pair with the next GPS time
 * update seen by timesync_poll(). */
void timesync_note_pps(uint64_t tick);

/* Call every main loop iteration. Watches gps_get_fix() for a new
 * time/date value; if a PPS edge is pending (see timesync_note_pps), pairs
 * it with that UTC second to (re)anchor the tick->UTC mapping. */
void timesync_poll(void);

int timesync_has_anchor(void);

/* Converts a tick64 to UTC nanoseconds-since-epoch via the current linear
 * anchor. Returns 1 and fills *out_utc_ns if anchored, 0 (leaves *out_utc_ns
 * untouched) if there's no anchor yet. */
int timesync_tick_to_utc_ns(uint64_t tick, int64_t *out_utc_ns);

/* Diagnostics for the status heartbeat. */
uint64_t timesync_anchor_tick(void);
int64_t timesync_anchor_utc_ns(void);

#endif
