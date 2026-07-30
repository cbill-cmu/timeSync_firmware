# timeSync_firmware

Multi-sensor time-sync system: an **AWR1843AOP** radar, a ZED camera capture
card, and a ublox GPS timing module all feed a common STM32 hub, which
timestamps every sensor's signal against a GPS-disciplined clock and streams
the result over serial. See [`docs/system-design.md`](docs/system-design.md)
for the full architecture.

## Repository Layout

- [`docs/`](docs) -- system design and architecture docs; start with
  [`docs/system-design.md`](docs/system-design.md).
- [`radar_firmware/`](radar_firmware) -- the AWR1843AOP radar firmware
  (build/flash/pinout instructions, prerequisites, etc.). See
  [`radar_firmware/README.md`](radar_firmware/README.md).
- [`sensors/`](sensors) -- STM32-hub-side per-sensor driver/integration
  modules (GPS, radar-sync capture, camera-strobe capture, and future
  sensors). See [`sensors/README.md`](sensors/README.md).
- [`stm32-hub/`](stm32-hub) -- the STM32 (Nucleo-64, STM32L476RG) firmware
  that owns the shared timebase, generic input-capture pipeline, and serial
  streaming. See [`stm32-hub/README.md`](stm32-hub/README.md).
- [`host/`](host) -- host-side serial tooling (`debug_console.py`, `sync.py`,
  `victim.yaml`) used for talking to the radar and, going forward, the STM32
  hub. Usage is documented alongside the board it's used with -- see
  [`radar_firmware/README.md`](radar_firmware/README.md) for the radar CLI
  workflow.

## License

The custom radar firmware is derived from TI's mmWave demo firmware (BSD-3);
modifications are licensed under similar terms (MIT) -- see `LICENSE` and
`LICENSE-MODIFICATIONS`.
