/* main.c -- STM32L476RG time-sync hub.
 *
 * Orchestrates the pieces described in ../docs/system-design.md:
 *   uart.c      generic polling UART TX helpers
 *   gps.c       USART1 RX ISR + NMEA 0183 sentence parser (GPS_BAUD, found
 *               by sweeping candidates against this specific EVK-7P -- see
 *               README.md's GPS bring-up section)
 *   timebase.c  TIM2 free-running 1MHz tick counter, the master clock
 *   capture.c   generic TIM2 input-capture ISR + ring buffer + sensor table
 *
 * USART2 (fixed at DEBUG_BAUD, the ST-LINK VCP) carries: the raw NMEA
 * mirror from gps.c, a periodic parsed-fix summary, and any capture events
 * (GPS PPS on TIM2_CH1/PA0, radar SYNC_OUT reserved on TIM2_CH2/PA1 --
 * nothing else needs to change to wire the radar in, per system-design.md's
 * extensibility contract).
 *
 * Wiring:
 *   GPS TXD1 (module output) -> STM32 PA10 (USART1_RX)
 *   GPS RXD1 (module input)  <- STM32 PA9  (USART1_TX)
 *   GPS GND                  -- common ground with the STM32, required
 *   GPS TIMEPULSE1 (if this EVK-7P revision has it wired to the test
 *     connector -- older ones may not, see README.md) -> STM32 PA0
 *   Radar SYNC_OUT (via level shifter, VIOIN is ~1.8V) -> STM32 PA1, once
 *     wired -- not connected yet.
 */
#include "stm32l4xx.h"
#include "uart.h"
#include "gps.h"
#include "timebase.h"
#include "capture.h"

#define DEBUG_BAUD 115200U
#define GPS_BAUD 115200U

static void print_fixed7(USART_TypeDef *u, int32_t value_1e7) {
  if (value_1e7 < 0) {
    uart_putc(u, '-');
    value_1e7 = -value_1e7;
  }
  uint32_t v = (uint32_t)value_1e7;
  uart_put_udec(u, v / 10000000U);
  uart_putc(u, '.');
  uint32_t frac = v % 10000000U;
  uint32_t divisor = 1000000U;
  while (divisor > 0U) {
    uart_putc(u, (char)('0' + (frac / divisor) % 10U));
    divisor /= 10U;
  }
}


static void print_fix_summary(USART_TypeDef *u) {
  gps_fix_t fix;
  gps_get_fix(&fix);

  uart_puts(u, "[gps] fix=");
  uart_puts(u, fix.fix_valid ? "YES" : "no");

  uart_puts(u, " time=");
  if (fix.has_time) {
    uart_put_udec(u, fix.hh);
    uart_putc(u, ':');
    uart_put_udec(u, fix.mm);
    uart_putc(u, ':');
    uart_put_udec(u, fix.ss);
  } else {
    uart_puts(u, "--:--:--");
  }

  uart_puts(u, " date=");
  if (fix.has_date) {
    uart_put_udec(u, fix.year);
    uart_putc(u, '-');
    uart_put_udec(u, fix.month);
    uart_putc(u, '-');
    uart_put_udec(u, fix.day);
  } else {
    uart_puts(u, "----------");
  }

  uart_puts(u, " sats=");
  if (fix.has_satellites) {
    uart_put_udec(u, fix.satellites);
  } else {
    uart_puts(u, "--");
  }

  uart_puts(u, " lat=");
  if (fix.has_position) {
    print_fixed7(u, fix.lat_1e7);
  } else {
    uart_puts(u, "--");
  }
  uart_puts(u, " lon=");
  if (fix.has_position) {
    print_fixed7(u, fix.lon_1e7);
  } else {
    uart_puts(u, "--");
  }
  uart_puts(u, "\r\n");
}

int main(void) {
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void)RCC->AHB2ENR; /* readback: ensure clocks are on before touching GPIOA/USARTs */

  /* PA2/PA3 = USART2 (ST-LINK VCP, debug output), AF7. */
  GPIOA->MODER &= ~(GPIO_MODER_MODE2_Msk | GPIO_MODER_MODE3_Msk);
  GPIOA->MODER |= (0x2U << GPIO_MODER_MODE2_Pos) | (0x2U << GPIO_MODER_MODE3_Pos);
  GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
  GPIOA->AFR[0] |= (7U << GPIO_AFRL_AFSEL2_Pos) | (7U << GPIO_AFRL_AFSEL3_Pos);
  uart_init(USART2, DEBUG_BAUD);

  gps_init(GPS_BAUD);
  timebase_init();
  capture_init();
  __enable_irq();

  uart_puts(USART2, "\r\n[stm32-hub] up -- GPS_BAUD=");
  uart_put_udec(USART2, GPS_BAUD);
  uart_puts(USART2, " DEBUG_BAUD=");
  uart_put_udec(USART2, DEBUG_BAUD);
  uart_puts(USART2, "\r\n");

  uint32_t heartbeat_count = 0;
  uint32_t idle_loops = 0;
  while (1) {
    gps_service(USART2);

    capture_event_t ev;
    while (capture_pop(&ev)) {
      uart_puts(USART2, "[capture] sensor=");
      uart_puts(USART2, capture_sensor_name(ev.sensor_id));
      uart_puts(USART2, " tick=");
      uart_put_udec(USART2, (uint32_t)(ev.tick >> 32));
      uart_putc(USART2, ':');
      uart_put_udec(USART2, (uint32_t)ev.tick);
      uart_puts(USART2, "\r\n");
    }

    idle_loops++;
    if (idle_loops >= 400000U) { /* rough, uncalibrated -- roughly once/second */
      idle_loops = 0;
      heartbeat_count++;
      uint64_t now = timebase_now64();
      uart_puts(USART2, "[stm32-hub] alive (");
      uart_put_udec(USART2, heartbeat_count);
      uart_puts(USART2, "), tick=");
      uart_put_udec(USART2, (uint32_t)(now >> 32));
      uart_putc(USART2, ':');
      uart_put_udec(USART2, (uint32_t)now);
      uart_puts(USART2, " tim2_isr_entries=");
      uart_put_udec(USART2, timebase_isr_entry_count());
      uart_puts(USART2, ", USART1 bytes_seen=");
      uart_put_udec(USART2, gps_bytes_seen());
      uart_puts(USART2, " uart_errors=");
      uart_put_udec(USART2, gps_uart_errors());
      uart_puts(USART2, " rx_dropped=");
      uart_put_udec(USART2, gps_rx_dropped());
      uart_puts(USART2, " sentences=");
      uart_put_udec(USART2, gps_sentences_seen());
      uart_puts(USART2, " checksum_errors=");
      uart_put_udec(USART2, gps_checksum_errors());
      uart_puts(USART2, " capture_dropped=");
      uart_put_udec(USART2, capture_dropped());
      uart_puts(USART2, "\r\n");

      print_fix_summary(USART2);
    }
  }
}
