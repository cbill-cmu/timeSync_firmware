#include "timebase.h"

/* Software high word extending TIM2's 32-bit hardware counter to 64 bits.
 * Written only by TIM2_IRQHandler's update-event handling. */
static volatile uint32_t tick_high = 0;
static volatile uint32_t isr_entry_count = 0; /* total TIM2 ISR dispatches, for sanity-checking ISR load once real sensors are generating capture events */

uint32_t timebase_isr_entry_count(void) { return isr_entry_count; }

void timebase_init(void) {
  RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
  (void)RCC->APB1ENR1;

  TIM2->PSC = (SystemCoreClock / 1000000U) - 1U; /* 1MHz tick regardless of SYSCLK */
  TIM2->ARR = 0xFFFFFFFFU;                        /* free-running, full 32-bit range */
  TIM2->EGR = TIM_EGR_UG;                          /* latch PSC/ARR before starting --
                                                     * PSC is always shadow-buffered, so
                                                     * without this it wouldn't apply
                                                     * until the first natural overflow */
  while (!(TIM2->SR & TIM_SR_UIF)) {
    /* EGR:UG's update event has real propagation latency -- clearing UIF
     * before it's actually set (racing ahead of the hardware) leaves a
     * delayed-but-real UIF to fire the instant DIER:UIE is enabled below,
     * incrementing tick_high for an event that latched PSC, not a genuine
     * 32-bit overflow. Poll until the hardware confirms it before clearing. */
  }
  TIM2->SR = ~TIM_SR_UIF; /* now a real clear -- no race */
  TIM2->DIER |= TIM_DIER_UIE;
  NVIC_EnableIRQ(TIM2_IRQn);
  TIM2->CR1 = TIM_CR1_CEN;
}

uint64_t timebase_now64(void) {
  uint32_t high1, low, high2;
  do {
    high1 = tick_high;
    low = TIM2->CNT;
    high2 = tick_high;
  } while (high1 != high2); /* CNT rolled over between the two high reads -- retry */
  return ((uint64_t)high2 << 32) | low;
}

/* Shared by capture.c so a captured CCRx value (already hardware-latched at
 * the true edge instant) can be extended to 64 bits using the same high
 * word timebase.c owns, without exposing tick_high itself. */
uint32_t timebase_high_word(void) {
  return tick_high;
}

extern void capture_tim2_isr(uint32_t sr_snapshot);

void TIM2_IRQHandler(void) {
  isr_entry_count++;

  /* Single SR read, shared with capture_tim2_isr() below, so it can tell
   * whether a CCxIF in THIS snapshot raced against the UIF we're about to
   * handle (edge latched just before rollover, ISR running just after). */
  uint32_t sr = TIM2->SR;

  if (sr & TIM_SR_UIF) {
    TIM2->SR = ~TIM_SR_UIF; /* SR flags clear on write-0; write-1 elsewhere is a no-op */
    tick_high++;
  }

  capture_tim2_isr(sr);
}
