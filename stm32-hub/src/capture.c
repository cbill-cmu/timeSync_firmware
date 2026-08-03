#include "capture.h"
#include "timebase.h"

static const char *const SENSOR_NAMES[SENSOR_COUNT] = {
    [SENSOR_GPS_PPS] = "gps_pps",
    [SENSOR_RADAR_SYNC] = "radar_sync",
};

#define CAPTURE_RING_SIZE 32U
static volatile capture_event_t capture_ring[CAPTURE_RING_SIZE];
static volatile uint32_t capture_head = 0; /* written only by the ISR */
static volatile uint32_t capture_tail = 0; /* written only by capture_pop() */
static volatile uint32_t dropped = 0;

void capture_init(void) {
  /* PA0 = TIM2_CH1, PA1 = TIM2_CH2, both AF1. Pull-down so a floating
   * radar-sync pin (not wired yet) idles low instead of chattering. */
  GPIOA->MODER &= ~(GPIO_MODER_MODE0_Msk | GPIO_MODER_MODE1_Msk);
  GPIOA->MODER |= (0x2U << GPIO_MODER_MODE0_Pos) | (0x2U << GPIO_MODER_MODE1_Pos);
  GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL0_Msk | GPIO_AFRL_AFSEL1_Msk);
  GPIOA->AFR[0] |= (1U << GPIO_AFRL_AFSEL0_Pos) | (1U << GPIO_AFRL_AFSEL1_Pos);
  GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD0_Msk | GPIO_PUPDR_PUPD1_Msk);
  GPIOA->PUPDR |= (0x2U << GPIO_PUPDR_PUPD0_Pos) | (0x2U << GPIO_PUPDR_PUPD1_Pos);

  /* CC1S/CC2S = 01: map CH1/CH2 directly to their own input (TI1/TI2), the
   * usual "just capture this pin" case rather than the cross-channel XOR
   * tricks used for e.g. PWM input measurement. */
  TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S_Msk | TIM_CCMR1_CC2S_Msk);
  TIM2->CCMR1 |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
  /* Rising edge, no filtering/prescaling -- default CCER/CCMR1 polarity and
   * filter bits are already 0, nothing else to set there. */
  TIM2->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E;
  TIM2->DIER |= TIM_DIER_CC1IE | TIM_DIER_CC2IE;
}

static void push(uint8_t sensor_id, uint32_t sr_snapshot, uint32_t ccr) {
  uint32_t high = timebase_high_word();
  /* If this capture's CCxIF and the timebase update's UIF landed in the
   * same SR snapshot, a large raw ccr means the edge was hardware-latched
   * just before the rollover -- attribute it to the pre-increment high
   * word (timebase.c already bumped tick_high before calling us), not the
   * one that now applies to "now". Sensor rates here (GPS 1Hz, radar up to
   * ~1.6kHz) are far below the ~71.5min rollover period, so this is a rare
   * edge case, but system-design.md is explicit that it must not be
   * misread rather than just accepted as a known bug. */
  if ((sr_snapshot & TIM_SR_UIF) && ccr > 0x80000000U) {
    high -= 1U;
  }

  uint64_t tick = ((uint64_t)high << 32) | ccr;
  uint32_t next_head = (capture_head + 1U) % CAPTURE_RING_SIZE;
  if (next_head != capture_tail) {
    capture_ring[capture_head].sensor_id = sensor_id;
    capture_ring[capture_head].tick = tick;
    capture_head = next_head;
  } else {
    dropped++; /* main loop isn't draining fast enough -- shouldn't happen at these rates */
  }
}

void capture_tim2_isr(uint32_t sr_snapshot) {
  if (sr_snapshot & TIM_SR_CC1IF) {
    uint32_t ccr = TIM2->CCR1; /* hardware-latched at the true edge instant */
    push(SENSOR_GPS_PPS, sr_snapshot, ccr);
    TIM2->SR = ~TIM_SR_CC1IF;
  }
  if (sr_snapshot & TIM_SR_CC2IF) {
    uint32_t ccr = TIM2->CCR2;
    push(SENSOR_RADAR_SYNC, sr_snapshot, ccr);
    TIM2->SR = ~TIM_SR_CC2IF;
  }
}

int capture_pop(capture_event_t *out) {
  if (capture_tail == capture_head) {
    return 0;
  }
  *out = capture_ring[capture_tail];
  capture_tail = (capture_tail + 1U) % CAPTURE_RING_SIZE;
  return 1;
}

uint32_t capture_dropped(void) {
  return dropped;
}

const char *capture_sensor_name(uint8_t sensor_id) {
  if (sensor_id >= SENSOR_COUNT) {
    return "?";
  }
  return SENSOR_NAMES[sensor_id];
}
