/* gps.h -- ISR-driven USART1 RX + NMEA 0183 sentence parser for the ublox
 * EVK-7P. See ../../docs/system-design.md ("GPS is special only in that it
 * is also the thing that converts ticks into real UTC") -- this module is
 * step 1 (decode what the module actually sends); PPS-anchored tick->UTC
 * mapping is a separate step once a real fix is available to verify it.
 */
#ifndef GPS_H
#define GPS_H

#include "stm32l4xx.h"

typedef struct {
  int fix_valid; /* RMC status=='A' or GGA fix quality > 0 */

  int has_time;
  uint8_t hh, mm, ss;

  int has_date;
  uint8_t day, month;
  uint16_t year;

  int has_position;
  int32_t lat_1e7; /* degrees * 1e7, +N/-S */
  int32_t lon_1e7; /* degrees * 1e7, +E/-W */

  int has_satellites;
  uint8_t satellites;
} gps_fix_t;

/* Configures PA9/PA10 as USART1 AF7, brings up USART1 at baud with RX
 * interrupt enabled. Call after enabling RCC/GPIOA clocks. */
void gps_init(uint32_t baud);

/* Drains whatever USART1 has received since the last call: feeds bytes
 * into the NMEA sentence assembler/parser, and if raw_echo_uart is
 * non-NULL, also forwards each raw byte there (e.g. USART2) so you can
 * watch unparsed sentences in a terminal. Call every main loop iteration. */
void gps_service(USART_TypeDef *raw_echo_uart);

/* Returns 1 and clears the flag if fix/time/date/position changed since
 * the last call to gps_fix_updated() or gps_get_fix(); 0 otherwise. */
int gps_fix_updated(void);

/* Copies the most recently parsed fix state into *out (always available,
 * independent of gps_fix_updated()'s edge-triggered flag). */
void gps_get_fix(gps_fix_t *out);

uint32_t gps_bytes_seen(void);
uint32_t gps_uart_errors(void);   /* USART1 framing/noise/overrun/parity errors */
uint32_t gps_rx_dropped(void);    /* raw ring buffer overrun (ISR outrunning gps_service) */
uint32_t gps_sentences_seen(void);
uint32_t gps_checksum_errors(void);

#endif
