/* capture.h -- generic TIM2 input-capture ISR + ring buffer + sensor table.
 * See ../../docs/system-design.md ("Extensibility contract"): adding a new
 * sensor should mean "wire it to a free TIM2 channel + add a table entry
 * here", nothing else.
 */
#ifndef CAPTURE_H
#define CAPTURE_H

#include "stm32l4xx.h"

/* NOTE on pin assignment: system-design.md's original diagram put PPS/radar
 * /camera on TIM2 CH1-4 = PA0-PA3. PA2/PA3 are already committed to USART2
 * (ST-LINK VCP, our only debug output) -- can't reuse them. CH1 (PA0) and
 * CH2 (PA1) are still free and used below; CH3/CH4 (camera, sensor #4+)
 * will need different pins when that sensor is actually added. */
enum {
  SENSOR_GPS_PPS = 0,   /* TIM2_CH1 / PA0 -- GPS TIMEPULSE1, if this EVK
                          revision has it wired to the test connector at
                          all (older EVK-7 boards may not -- see README) */
  SENSOR_RADAR_SYNC = 1, /* TIM2_CH2 / PA1 -- reserved for the radar's
                            SYNC_OUT once the level shifter is wired in.
                            Nothing else needs to change to add it. */
  SENSOR_COUNT
};

typedef struct {
  uint8_t sensor_id;
  uint64_t tick;
} capture_event_t;

/* Configures PA0/PA1 as TIM2 CH1/CH2 input-capture (rising edge, internal
 * pull-down so an unconnected pin -- e.g. radar before it's wired -- can't
 * chatter into spurious captures). Call after timebase_init(). */
void capture_init(void);

/* Pops the oldest pending capture event into *out. Returns 1 if one was
 * available, 0 if the ring buffer was empty. Call from the main loop. */
int capture_pop(capture_event_t *out);

/* Number of events dropped because the ring buffer was full when a capture
 * ISR fired -- should stay 0 in normal operation at these sensor rates. */
uint32_t capture_dropped(void);

const char *capture_sensor_name(uint8_t sensor_id);

/* ISR-side entry point, called by TIM2_IRQHandler (owned by timebase.c)
 * with the SR snapshot it already read, so this can tell whether a CCxIF
 * here raced against the same update event timebase.c is handling. */
void capture_tim2_isr(uint32_t sr_snapshot);

#endif
