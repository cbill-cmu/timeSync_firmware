/* main.c -- STM32L476RG time-sync hub.
 *
 * Orchestrates the pieces described in ../docs/system-design.md:
 *   uart.c        generic blocking UART TX helpers (used internally by
 *                 debug_uart.c/gps.c for peripheral setup)
 *   debug_uart.c  buffered, TXE-interrupt-driven USART2 (ST-LINK VCP) TX --
 *                 see its header for why blocking writes weren't enough
 *   gps.c         USART1 RX ISR + NMEA 0183 sentence parser (GPS_BAUD, found
 *                 by sweeping candidates against this specific EVK-7P -- see
 *                 README.md's GPS bring-up section)
 *   timebase.c    TIM2 free-running 1MHz tick counter, the master clock
 *   capture.c     generic TIM2 input-capture ISR + ring buffer + sensor table
 *
 * USART2 (fixed at DEBUG_BAUD, the ST-LINK VCP) carries: the raw NMEA
 * mirror from gps.c, a periodic parsed-fix summary, and any capture events
 * (GPS PPS on TIM2_CH1/PA0, radar SYNC_OUT on TIM2_CH2/PA1, camera SYNC_OUT
 * on TIM2_CH3/PB10 -- nothing else needs to change to wire a sensor in, per
 * system-design.md's extensibility contract).
 *
 * Wiring:
 *   GPS TXD1 (module output) -> STM32 PA10 (USART1_RX)
 *   GPS RXD1 (module input)  <- STM32 PA9  (USART1_TX)
 *   GPS GND                  -- common ground with the STM32, required
 *   GPS TIMEPULSE1 -> STM32 PA0 (confirmed working with a PPS-capable
 *     module, e.g. EVK-M8T -- the original EVK-7P didn't expose this)
 *   Radar SYNC_OUT (via level shifter, VIOIN is ~1.8V) -> STM32 PA1, once
 *     wired -- not connected yet.
 *   Camera SYNC_OUT -> STM32 PB10, once wired -- not connected yet.
 */
#include "stm32l4xx.h"
#include "debug_uart.h"
#include "gps.h"
#include "timebase.h"
#include "capture.h"
#include "timesync.h"

#define DEBUG_BAUD 115200U
#define GPS_BAUD 9600U /* EVK-M8T documented default -- different module than the
                         * EVK-7P this was tuned for; adjust if this doesn't decode
                         * cleanly (see stm32-hub/README.md's GPS baud sweep note) */

static void print_fixed7(int32_t value_1e7) {
  if (value_1e7 < 0) {
    debug_putc('-');
    value_1e7 = -value_1e7;
  }
  uint32_t v = (uint32_t)value_1e7;
  debug_put_udec(v / 10000000U);
  debug_putc('.');
  uint32_t frac = v % 10000000U;
  uint32_t divisor = 1000000U;
  while (divisor > 0U) {
    debug_putc((char)('0' + (frac / divisor) % 10U));
    divisor /= 10U;
  }
}

static void print_udec2(uint32_t v) {
  debug_putc((char)('0' + (v / 10U) % 10U));
  debug_putc((char)('0' + v % 10U));
}

static void print_fix_summary(void) {
  gps_fix_t fix;
  gps_get_fix(&fix);

  debug_puts("[gps] fix=");
  debug_puts(fix.fix_valid ? "YES" : "no");

  debug_puts(" time=");
  if (fix.has_time) {
    print_udec2(fix.hh);
    debug_putc(':');
    print_udec2(fix.mm);
    debug_putc(':');
    print_udec2(fix.ss);
  } else {
    debug_puts("--:--:--");
  }

  debug_puts(" date=");
  if (fix.has_date) {
    debug_put_udec(fix.year);
    debug_putc('-');
    print_udec2(fix.month);
    debug_putc('-');
    print_udec2(fix.day);
  } else {
    debug_puts("----------");
  }

  debug_puts(" sats=");
  if (fix.has_satellites) {
    debug_put_udec(fix.satellites);
  } else {
    debug_puts("--");
  }

  debug_puts(" lat=");
  if (fix.has_position) {
    print_fixed7(fix.lat_1e7);
  } else {
    debug_puts("--");
  }
  debug_puts(" lon=");
  if (fix.has_position) {
    print_fixed7(fix.lon_1e7);
  } else {
    debug_puts("--");
  }
  debug_puts("\r\n");
}

int main(void) {
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void)RCC->AHB2ENR; /* readback: ensure clocks are on before touching GPIOA/USARTs */

  debug_uart_init(DEBUG_BAUD);
  gps_init(GPS_BAUD);
  timebase_init();
  capture_init();
  __enable_irq();

  debug_puts("\r\n[stm32-hub] up -- GPS_BAUD=");
  debug_put_udec(GPS_BAUD);
  debug_puts(" DEBUG_BAUD=");
  debug_put_udec(DEBUG_BAUD);
  debug_puts("\r\n");

  uint32_t heartbeat_count = 0;
  uint32_t idle_loops = 0;
  while (1) {
    gps_service();
    timesync_poll();

    capture_event_t ev;
    while (capture_pop(&ev)) {
      if (ev.sensor_id == SENSOR_GPS_PPS) {
        timesync_note_pps(ev.tick);
      }

      debug_puts("[capture] sensor=");
      debug_puts(capture_sensor_name(ev.sensor_id));
      debug_puts(" tick=");
      debug_put_udec((uint32_t)(ev.tick >> 32));
      debug_putc(':');
      debug_put_udec((uint32_t)ev.tick);

      int64_t utc_ns;
      if (timesync_tick_to_utc_ns(ev.tick, &utc_ns)) {
        debug_puts(" utc_ns=");
        debug_put_sdec64(utc_ns);
      }
      debug_puts("\r\n");
    }

    idle_loops++;
    if (idle_loops >= 400000U) { /* rough, uncalibrated -- roughly once/second */
      idle_loops = 0;
      heartbeat_count++;
      uint64_t now = timebase_now64();
      debug_puts("[stm32-hub] alive (");
      debug_put_udec(heartbeat_count);
      debug_puts("), tick=");
      debug_put_udec((uint32_t)(now >> 32));
      debug_putc(':');
      debug_put_udec((uint32_t)now);
      debug_puts(" tim2_isr_entries=");
      debug_put_udec(timebase_isr_entry_count());
      debug_puts(", USART1 bytes_seen=");
      debug_put_udec(gps_bytes_seen());
      debug_puts(" uart_errors=");
      debug_put_udec(gps_uart_errors());
      debug_puts(" rx_dropped=");
      debug_put_udec(gps_rx_dropped());
      debug_puts(" sentences=");
      debug_put_udec(gps_sentences_seen());
      debug_puts(" checksum_errors=");
      debug_put_udec(gps_checksum_errors());
      debug_puts(" capture_dropped=");
      debug_put_udec(capture_dropped());
      debug_puts(" pps_anchor=");
      debug_puts(timesync_has_anchor() ? "YES" : "no");
      debug_puts("\r\n");

      print_fix_summary();
    }
  }
}
