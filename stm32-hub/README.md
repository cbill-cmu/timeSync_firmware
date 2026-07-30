# stm32-hub

STM32 (Nucleo-64, STM32L476RG) firmware for the time-sync hub described in
[`../docs/system-design.md`](../docs/system-design.md). Owns the generic,
sensor-agnostic infrastructure:

- `timebase` -- TIM2 free-running tick counter, 64-bit extension, overflow
  handling.
- `capture` -- generic input-capture ISR, lock-free event ring buffer, and
  the `sensor_table[]` that wires in modules from [`../sensors`](../sensors).
- `stream` -- event framing + USB-CDC transmission to the host.

Per-sensor logic (GPS UBX/NMEA parsing, tick-to-UTC mapping, etc.) lives in
[`../sensors`](../sensors), not here -- this folder should only need to
change when the core capture/timebase/streaming pipeline itself changes.

Empty for now -- project scaffolding comes next.
