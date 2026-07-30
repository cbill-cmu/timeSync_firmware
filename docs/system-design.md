# System Design: STM32 Multi-Sensor Time Sync Hub

## Goal

Add a ublox GPS timing module to an STM32 (Nucleo-64, **STM32L476RG**) that sits
alongside the AWR1843AOP radar (this repo's existing firmware, which already
drives a hardware frame-sync pulse — see `xwr18xx-custom/mss/mss_main.c`) and a
ZED camera capture card (hardware strobe/trigger output confirmed available).
The STM32 timestamps events from all three sources against a common,
GPS-disciplined clock, and streams the timestamped record log out over serial.
The design must stay simple enough that adding a fourth, fifth, ... digital
sensor later requires no architectural change.

## Core idea: one free-running counter, everything else is just an edge on it

Rather than timestamping GPS, radar, and camera separately and correlating
after the fact, the STM32 keeps **one 32-bit hardware timer (TIM2) as a
free-running tick counter**, and every sensor's pulse — GPS PPS, radar
`SYNC_OUT`, camera strobe, and any future sensor — is captured as a **hardware
input-capture event against that same counter**. Because every edge is latched
by hardware off the identical clock, there is no software jitter and no
per-sensor skew to correct for: cross-comparing two sensors' timestamps is
just subtraction.

```
GPS PPS        ──► TIM2 CH1 (PA0) ─┐
Radar sync     ──► TIM2 CH2 (PA1) ─┤── all latch the SAME 32-bit counter (1 MHz tick / 1µs resolution)
Camera strobe  ──► TIM2 CH3 (PA2) ─┤
(future #1)    ──► TIM2 CH4 (PA3) ─┘
(future #5+)   ──► TIM5 CH1-4, hardware-synced to TIM2 at reset so it never drifts relative to TIM2
```

GPS is special only in that it is *also* the thing that converts "ticks" into
real UTC:

- ublox timing modules emit a PPS pulse aligned to the UTC-second boundary,
  plus a UBX message (`NAV-PVT` / `TIM-TP`) over UART identifying which second
  that edge corresponds to.
- Each PPS edge is hardware-captured as a tick value; paired with the UTC
  second from the GPS message, this forms an **anchor point**
  `(tick, utc_second)`.
- Two consecutive anchors give the *measured* tick frequency (should be
  ≈1,000,000/s at a 1 MHz prescale) — this both disciplines the tick→UTC
  mapping and doubles as a GPS lock-quality signal (a jittery measured
  frequency means a bad fix).
- Every other sensor's captured tick is run through the same linear mapping:

  ```
  utc_ns(tick) = utc_ns(last_pps) + (tick − tick_at_last_pps) × (1e9 / measured_freq)
  ```

That one function *is* the time-sync mechanism. Radar and camera event
timestamps become directly comparable UTC-nanosecond values, and adding a 4th
sensor later needs zero new math.

## ISR discipline

This is where glitches usually creep in, so it's worth being explicit:

- Capture ISRs do **only**: read the hardware-latched `CCRx` register and push
  `{sensor_id, tick32}` into a small lock-free single-producer ring buffer. No
  UTC math, no serial TX, inside an ISR.
- A main-loop (or low-priority task) drains the ring buffer, performs the
  tick→UTC conversion, and frames the result for serial output.
- The 32-bit counter @ 1 MHz overflows every ~71 minutes. Handle this with a
  TIM2 update interrupt that increments a software high-word, using the
  standard read-high / read-low / read-high-again race-safe pattern so a
  capture landing exactly at rollover is never misread.
- Before first GPS lock, events are still captured (no data loss) but flagged
  `relative-only, no UTC yet`. The same flag applies during GPS holdover, so
  degraded segments are identifiable in post-processing rather than silently
  wrong.
- Radar's `SYNC_OUT` / GPIO pulse sits on the ~1.8V `VIOIN` rail (see this
  repo's main `README.md`) — a level shifter to the L476's 3.3V inputs is
  required regardless of which pin is tapped. Confirm whether the camera
  capture card's strobe output is already 3.3V-compatible or needs the same
  treatment.

## Extensibility contract

Adding sensor #4, #5, ... should never touch the capture/timebase/streaming
code:

1. Wire the pulse to a free input-capture pin.
2. Add one entry to a `sensor_table[]` (name, timer/channel, edge polarity,
   expected nominal rate for drop detection).
3. Done. The generic capture ISR, ring buffer, UTC mapper, and serial framer
   are already sensor-agnostic, keyed only by `sensor_id`.

## Streaming

Framed records go out over USB-CDC (or a spare UART) as
`seq, sensor_id, tick64, utc_ns, flags` — a simple binary struct + length +
CRC is sufficient at these rates (radar up to ~1.6 kHz per this repo's
`victim.yaml` `frame_period`, camera in the tens of Hz, GPS at 1 Hz — well
within USB-CDC or UART throughput). A host-side script mirroring
`debug_console.py`'s reconnect-on-drop logic receives and logs the stream to
CSV/parquet.

## Proposed repo layout

The current repo has the radar firmware duplicated under a stale nested
`firmware/` directory (contents differ from the root copies and it's missing
a built `.bin`); that gets cleaned up manually. Once clean, the target shape
to grow into:

```
timeSync_firmware/
  firmware/
    radar/              # current xwr18xx-custom (rename), xwr18xx baseline optional
    stm32-hub/           # new STM32L476RG project -- the time-sync hub
      Src/
        timebase.c/.h     # TIM2 tick counter, 64-bit extension, overflow handling
        gps.c/.h          # UBX/NMEA parser, PPS anchor + drift model, tick->UTC
        capture.c/.h      # generic input-capture ISR + ring buffer + sensor_table[]
        sensors_config.c  # the one file touched to add/remove a sensor
        stream.c/.h       # framing + USB CDC TX
      docs/pinout.md
  host/
    debug_console.py  sync.py  stm32_logger.py (new)  victim.example.yaml
  docs/
    system-design.md   # this file
```

## Open items to work through next

- STM32 pin assignment table (GPS PPS, GPS UART RX/TX, radar sync in, camera
  strobe in, spares) — needs the actual ublox module part number and its PPS
  pin characteristics (idle level, pulse width, polarity).
- Level-shifting plan for radar (and camera, if needed) signals into the
  L476's 3.3V domain.
- UBX message set/rate to request from the ublox module (`NAV-PVT` +
  `TIM-TP` at minimum) and the exact PPS-to-message alignment assumption to
  verify against that module's interface manual.
- Binary framing format details (field widths, CRC choice) for the serial
  stream, and the matching `stm32_logger.py` host script.
- TIM5 hardware-sync-to-TIM2 setup, once/if a 5th+ sensor is added.
