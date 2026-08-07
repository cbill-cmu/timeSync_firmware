#include "timesync.h"
#include "gps.h"

static volatile uint64_t pending_pps_tick = 0;
static volatile int pending_pps = 0;

static volatile int has_anchor = 0;
static volatile uint64_t anchor_tick = 0;
static volatile int64_t anchor_utc_ns = 0;

/* Last GPS time/date timesync_poll() has already reacted to, so it only
 * treats a genuinely NEW second (not the same sentence re-read, or a
 * different sentence type repeating the same second) as a boundary. */
static int last_valid = 0;
static uint8_t last_hh, last_mm, last_ss, last_day, last_month;
static uint16_t last_year;

void timesync_note_pps(uint64_t tick) {
  pending_pps_tick = tick;
  pending_pps = 1; /* overwrites any older unconsumed edge -- only the most recent matters */
}

/* Days since 1970-01-01 for a Gregorian civil date. Standard integer-only
 * algorithm (Howard Hinnant's days_from_civil) -- avoids needing libc's
 * <time.h> in this freestanding build. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= (m <= 2) ? 1 : 0;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);              /* [0, 399] */
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365] */
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;   /* [0, 146096] */
  return era * 146097 + (int64_t)doe - 719468;
}

void timesync_poll(void) {
  gps_fix_t fix;
  gps_get_fix(&fix);
  if (!fix.has_time || !fix.has_date) {
    return;
  }

  int is_new = !last_valid || fix.hh != last_hh || fix.mm != last_mm ||
               fix.ss != last_ss || fix.day != last_day ||
               fix.month != last_month || fix.year != last_year;
  if (!is_new) {
    return;
  }
  last_valid = 1;
  last_hh = fix.hh;
  last_mm = fix.mm;
  last_ss = fix.ss;
  last_day = fix.day;
  last_month = fix.month;
  last_year = fix.year;

  if (!pending_pps) {
    return;
  }
  pending_pps = 0;

  /* This NMEA sentence's time field is the UTC second the pending PPS edge
   * marked the start of (u-blox emits the sentence shortly after the PPS
   * edge for that second, with the edge itself defining the .000 instant).
   * Sub-second alignment beyond that isn't modeled here. */
  int64_t days = days_from_civil(fix.year, fix.month, fix.day);
  int64_t epoch_s = days * 86400 + (int64_t)fix.hh * 3600 + (int64_t)fix.mm * 60 + fix.ss;

  anchor_tick = pending_pps_tick;
  anchor_utc_ns = epoch_s * 1000000000LL;
  has_anchor = 1;
}

int timesync_has_anchor(void) {
  return has_anchor;
}

int timesync_tick_to_utc_ns(uint64_t tick, int64_t *out_utc_ns) {
  if (!has_anchor) {
    return 0;
  }
  /* 1 tick == 1000ns at the confirmed 1MHz timebase rate (see timebase.c).
   * Signed delta via unsigned subtraction is intentional -- correct
   * two's-complement reinterpretation even if tick precedes anchor_tick,
   * as long as the true difference fits in int64_t (true here: ticks
   * involved are always within a tiny window of "now"). */
  int64_t delta_ticks = (int64_t)(tick - anchor_tick);
  *out_utc_ns = anchor_utc_ns + delta_ticks * 1000LL;
  return 1;
}

uint64_t timesync_anchor_tick(void) {
  return anchor_tick;
}

int64_t timesync_anchor_utc_ns(void) {
  return anchor_utc_ns;
}
