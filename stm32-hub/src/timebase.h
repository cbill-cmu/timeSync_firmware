/* timebase.h -- TIM2 free-running tick counter, the master clock for
 * cross-sensor timestamping. See ../../docs/system-design.md ("Core idea").
 */
#ifndef TIMEBASE_H
#define TIMEBASE_H

#include "stm32l4xx.h"

/* Configures TIM2 as a free-running 1MHz up-counter (1us/tick) with the
 * update interrupt enabled, so tick64 keeps counting correctly across the
 * 32-bit counter's ~71.5 minute rollover. Call once at startup, after
 * enabling RCC clocks. */
void timebase_init(void);

/* Current tick count, race-safe against a concurrent TIM2 update ISR. */
uint64_t timebase_now64(void);

/* Raw software high word, for capture.c to combine with a hardware-latched
 * CCRx value. Prefer timebase_now64() unless you're extending a capture. */
uint32_t timebase_high_word(void);

uint32_t timebase_isr_entry_count(void); /* total TIM2 ISR dispatches since boot */

#endif
