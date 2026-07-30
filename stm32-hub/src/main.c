/* main.c -- bring-up test for the STM32L476RG time-sync hub.
 *
 * Stage 1 of docs/system-design.md: prove the toolchain, vector table,
 * linker script, and flashing flow are correct on real hardware before any
 * timer/UART/interrupt logic is added. This blinks the Nucleo-64 board's
 * user LED (LD2, PA5) using a SysTick-driven millisecond counter -- nothing
 * GPS/timebase/capture-related yet.
 */
#include "stm32l4xx.h"

static volatile uint32_t g_ms_ticks = 0;

void SysTick_Handler(void) {
  g_ms_ticks++;
}

static void delay_ms(uint32_t ms) {
  uint32_t start = g_ms_ticks;
  while ((uint32_t)(g_ms_ticks - start) < ms) {
    __NOP();
  }
}

int main(void) {
  /* SystemCoreClock defaults to 4,000,000 (reset default: MSI @ 4 MHz) --
   * no clock tree changes yet, see system_stm32l4xx.c. */
  SysTick_Config(SystemCoreClock / 1000U); /* 1 ms tick */

  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  (void)RCC->AHB2ENR; /* readback: ensure the clock is on before touching GPIOA */

  /* PA5 = LD2 on the Nucleo-64 L476RG: general-purpose output, push-pull. */
  GPIOA->MODER &= ~GPIO_MODER_MODE5_Msk;
  GPIOA->MODER |= (0x1U << GPIO_MODER_MODE5_Pos); /* 01 = output */
  GPIOA->OTYPER &= ~GPIO_OTYPER_OT5; /* push-pull */

  while (1) {
    GPIOA->ODR ^= GPIO_ODR_OD5;
    delay_ms(500);
  }
}
