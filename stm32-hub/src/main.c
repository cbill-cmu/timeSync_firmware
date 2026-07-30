/* main.c -- bring-up test for the STM32L476RG time-sync hub.
 *
 * Stage 1 of docs/system-design.md: prove the toolchain, vector table,
 * linker script, and flashing flow are correct on real hardware before any
 * timer/UART/interrupt logic is added. Blinks the Nucleo-64 board's user
 * LED (LD2, PA5).
 *
 * Uses a dumb busy-wait delay here, not SysTick/NVIC, to keep this very
 * first hardware test as simple as possible -- fewer moving parts to debug
 * if something's wrong. Switch to a SysTick-driven millisecond counter once
 * this is confirmed working on real hardware.
 */
#include "stm32l4xx.h"

static void delay_loops(volatile uint32_t n) {
  while (n--) {
    __NOP();
  }
}

int main(void) {
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  (void)RCC->AHB2ENR; /* readback: ensure the clock is on before touching GPIOA */

  /* PA5 = LD2 on the Nucleo-64 L476RG: general-purpose output, push-pull. */
  GPIOA->MODER &= ~GPIO_MODER_MODE5_Msk;
  GPIOA->MODER |= (0x1U << GPIO_MODER_MODE5_Pos); /* 01 = output */
  GPIOA->OTYPER &= ~GPIO_OTYPER_OT5; /* push-pull */

  while (1) {
    GPIOA->ODR ^= GPIO_ODR_OD5;
    delay_loops(400000U); /* rough, uncalibrated -- just needs to be visible */
  }
}
