/* main.c -- stage 2: GPS UART bring-up for the STM32L476RG time-sync hub.
 *
 * Goal: prove the ublox EVK-7P is wired correctly and actually talking,
 * before writing a real NMEA/UBX parser. This is a dumb byte passthrough:
 * every byte received on USART1 (from the GPS) is immediately forwarded
 * out USART2 (the ST-LINK Virtual COM Port), so you can watch raw NMEA
 * sentences show up in a serial terminal on the host.
 *
 * No interrupts, no buffering, no LED -- deliberately minimal, matching
 * stage 1's philosophy of proving one thing at a time. See
 * ../docs/system-design.md and this folder's README for what comes next
 * (timebase, PPS capture, real parsing).
 *
 * Wiring (see README.md's pin table for the full picture):
 *   GPS TXD1 (module output) -> STM32 PA10 (USART1_RX)
 *   GPS RXD1 (module input)  <- STM32 PA9  (USART1_TX)   -- not used yet,
 *     wired now so UBX config commands can be added later without rewiring
 *   GPS GND                  -- common ground with the STM32, required
 * Confirm the EVK-7P's actual header pin numbering and I/O voltage against
 * its own manual/silkscreen before connecting -- see README.md.
 *
 * Watching the output: USART2 is the same USB cable used to flash (ST-LINK
 * VCP). While the ST-LINK is usbipd-attached to WSL, it will NOT show up
 * as a Windows COM port -- find its device node in WSL instead
 * (`ls /dev/ttyACM*`) and point a terminal (e.g. `../host/debug_console.py
 * --port /dev/ttyACM0 --baud 9600`, or `picocom /dev/ttyACM0 -b 9600`) at
 * it. ublox 7-series modules default to 9600 baud NMEA -- adjust BAUD below
 * if this module's been reconfigured.
 */
#include "stm32l4xx.h"

#define BAUD 9600U

static void uart_init(USART_TypeDef *u, uint32_t baud) {
  u->BRR = SystemCoreClock / baud;
  u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart_putc(USART_TypeDef *u, char c) {
  while (!(u->ISR & USART_ISR_TXE)) {
  }
  u->TDR = (uint8_t)c;
}

static void uart_puts(USART_TypeDef *u, const char *s) {
  while (*s) {
    uart_putc(u, *s++);
  }
}

static int uart_rx_ready(USART_TypeDef *u) {
  return (u->ISR & USART_ISR_RXNE) != 0;
}

static char uart_getc(USART_TypeDef *u) {
  return (char)(u->RDR & 0xFFU);
}

int main(void) {
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void)RCC->AHB2ENR; /* readback: ensure clocks are on before touching GPIOA/USARTs */

  /* PA2/PA3 = USART2 (ST-LINK VCP, debug output). PA9/PA10 = USART1 (GPS).
   * AF7 on all four -- alternate function mode (10), then AF selector 7. */
  GPIOA->MODER &= ~(GPIO_MODER_MODE2_Msk | GPIO_MODER_MODE3_Msk |
                     GPIO_MODER_MODE9_Msk | GPIO_MODER_MODE10_Msk);
  GPIOA->MODER |= (0x2U << GPIO_MODER_MODE2_Pos) | (0x2U << GPIO_MODER_MODE3_Pos) |
                  (0x2U << GPIO_MODER_MODE9_Pos) | (0x2U << GPIO_MODER_MODE10_Pos);

  GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
  GPIOA->AFR[0] |= (7U << GPIO_AFRL_AFSEL2_Pos) | (7U << GPIO_AFRL_AFSEL3_Pos);
  GPIOA->AFR[1] &= ~(GPIO_AFRH_AFSEL9_Msk | GPIO_AFRH_AFSEL10_Msk);
  GPIOA->AFR[1] |= (7U << GPIO_AFRH_AFSEL9_Pos) | (7U << GPIO_AFRH_AFSEL10_Pos);

  uart_init(USART2, BAUD);
  uart_init(USART1, BAUD);

  uart_puts(USART2, "\r\n[stm32-hub] USART1 (GPS) -> USART2 passthrough starting\r\n");

  while (1) {
    if (uart_rx_ready(USART1)) {
      uart_putc(USART2, uart_getc(USART1));
    }
  }
}
