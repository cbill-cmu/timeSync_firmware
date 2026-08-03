/* uart.h -- tiny polling UART TX helpers shared by main.c/gps.c. RX is
 * always interrupt-driven (see gps.c) -- these are only ever used for
 * outbound debug/status text, where blocking on TXE is fine. */
#ifndef UART_H
#define UART_H

#include "stm32l4xx.h"

void uart_init(USART_TypeDef *u, uint32_t baud);
void uart_putc(USART_TypeDef *u, char c);
void uart_puts(USART_TypeDef *u, const char *s);
void uart_put_udec(USART_TypeDef *u, uint32_t v);

#endif
