#include "debug_uart.h"
#include "uart.h"

/* 2048 bytes is generous relative to actual load: at 115200 baud USART2
 * can carry ~11,520 bytes/sec. Measured worst-case debug traffic (GPS NMEA
 * mirror at 9600 baud's ~960 bytes/sec, plus a 3rd capture channel firing
 * at tens of Hz with ~75-byte lines, plus the occasional heartbeat) is
 * comfortably under that on average -- the problem this module fixes was
 * burstiness (e.g. two capture events landing microseconds apart), not
 * sustained overload, so a buffer this size only needs to absorb short
 * bursts, not indefinitely outrun the wire. */
#define DEBUG_TX_BUF_SIZE 2048U
static volatile uint8_t tx_buf[DEBUG_TX_BUF_SIZE];
static volatile uint32_t tx_head = 0; /* written only by debug_putc() */
static volatile uint32_t tx_tail = 0; /* written only by the ISR */

void debug_uart_init(uint32_t baud) {
  /* PA2/PA3 = USART2 (ST-LINK VCP), AF7. */
  GPIOA->MODER &= ~(GPIO_MODER_MODE2_Msk | GPIO_MODER_MODE3_Msk);
  GPIOA->MODER |= (0x2U << GPIO_MODER_MODE2_Pos) | (0x2U << GPIO_MODER_MODE3_Pos);
  GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
  GPIOA->AFR[0] |= (7U << GPIO_AFRL_AFSEL2_Pos) | (7U << GPIO_AFRL_AFSEL3_Pos);

  uart_init(USART2, baud);
  NVIC_EnableIRQ(USART2_IRQn); /* TXEIE itself stays off until there's something queued */
}

void debug_putc(char c) {
  uint32_t next_head = (tx_head + 1U) % DEBUG_TX_BUF_SIZE;
  while (next_head == tx_tail) {
    /* Ring buffer full -- wait for the ISR to drain some. Guarantees no
     * silent data loss; should be rare to never given the sizing above. */
  }
  tx_buf[tx_head] = (uint8_t)c;
  tx_head = next_head;
  USART2->CR1 |= USART_CR1_TXEIE; /* idempotent -- (re)starts draining if it had stopped */
}

void debug_puts(const char *s) {
  while (*s) {
    debug_putc(*s++);
  }
}

void debug_put_udec(uint32_t v) {
  char digits[11];
  int i = 0;
  do {
    digits[i++] = (char)('0' + (v % 10U));
    v /= 10U;
  } while (v && i < 10);
  while (i > 0) {
    debug_putc(digits[--i]);
  }
}

void debug_put_udec64(uint64_t v) {
  char digits[21];
  int i = 0;
  do {
    digits[i++] = (char)('0' + (uint32_t)(v % 10U));
    v /= 10U;
  } while (v && i < 20);
  while (i > 0) {
    debug_putc(digits[--i]);
  }
}

void debug_put_sdec64(int64_t v) {
  if (v < 0) {
    debug_putc('-');
    v = -v;
  }
  debug_put_udec64((uint64_t)v);
}

void USART2_IRQHandler(void) {
  if (USART2->ISR & USART_ISR_TXE) {
    if (tx_tail != tx_head) {
      uint8_t c = tx_buf[tx_tail];
      tx_tail = (tx_tail + 1U) % DEBUG_TX_BUF_SIZE;
      USART2->TDR = c; /* writing TDR clears TXE */
    } else {
      USART2->CR1 &= ~USART_CR1_TXEIE; /* nothing queued -- stop interrupting until debug_putc() re-enables it */
    }
  }
}
