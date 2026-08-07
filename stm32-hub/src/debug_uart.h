/* debug_uart.h -- interrupt-driven, buffered USART2 (ST-LINK VCP) TX.
 *
 * Why this exists: main.c/gps.c used to call uart.c's blocking uart_putc()
 * directly on USART2, which busy-waits on TXE for every single byte
 * (~87us/byte at 115200 baud). Once a third capture channel and continuous
 * GPS NMEA mirroring were both writing to that same link, the main loop
 * spent so long blocked inside those writes that the GPS RX ring buffer
 * (gps.c) and the capture ring buffer (capture.c) both overflowed under
 * real load -- confirmed via `rx_dropped`/`capture_dropped` both climbing
 * during a live test. Root cause: printing had become the bottleneck, not
 * TX bandwidth (well under it on average -- see debug_uart.c).
 *
 * Fix: queue bytes into a RAM ring buffer (fast, non-blocking in the
 * common case) and let a USART2 TXE interrupt drain it in the background,
 * so the main loop's print calls return almost immediately instead of
 * blocking on the wire. debug_putc() still blocks if the ring buffer
 * itself is momentarily full (guarantees zero data loss at this layer
 * rather than silently dropping), but that should be rare to never given
 * the throughput math in debug_uart.c.
 */
#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include "stm32l4xx.h"

/* Configures PA2/PA3 as USART2 AF7, brings up USART2 at baud, and sets up
 * the TXE-interrupt-driven output ring buffer. Call after enabling
 * RCC/GPIOA clocks, before any debug_put*() call. */
void debug_uart_init(uint32_t baud);

void debug_putc(char c);
void debug_puts(const char *s);
void debug_put_udec(uint32_t v);
void debug_put_udec64(uint64_t v);
void debug_put_sdec64(int64_t v);

#endif
