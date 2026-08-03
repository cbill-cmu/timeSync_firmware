#include "uart.h"

void uart_init(USART_TypeDef *u, uint32_t baud) {
  u->BRR = SystemCoreClock / baud;
  u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void uart_putc(USART_TypeDef *u, char c) {
  while (!(u->ISR & USART_ISR_TXE)) {
  }
  u->TDR = (uint8_t)c;
}

void uart_puts(USART_TypeDef *u, const char *s) {
  while (*s) {
    uart_putc(u, *s++);
  }
}

void uart_put_udec(USART_TypeDef *u, uint32_t v) {
  char digits[11];
  int i = 0;
  do {
    digits[i++] = (char)('0' + (v % 10U));
    v /= 10U;
  } while (v && i < 10);
  while (i > 0) {
    uart_putc(u, digits[--i]);
  }
}
