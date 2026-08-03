# stm32-hub

STM32 (Nucleo-64, **STM32L476RG**) firmware for the time-sync hub described
in [`../docs/system-design.md`](../docs/system-design.md). Bare-metal,
register-level C -- CMSIS core + device headers only, no HAL/CubeMX, built
with a plain Makefile (no IDE required).

## Status

**Stages 1-3 are done and confirmed working on real hardware**: LD2 blinks,
the ublox EVK-7P GPS module is talking over UART and its NMEA sentences
parse cleanly (zero checksum errors across sustained multi-minute runs), and
the TIM2 free-running timebase + generic capture-ISR scaffolding from
`../docs/system-design.md` is in place and correct. Source layout:

- `src/uart.c`/`.h` -- polling UART TX helpers (`uart_init`/`uart_putc`/
  `uart_puts`/`uart_put_udec`), shared by `main.c` and `gps.c`.
- `src/gps.c`/`.h` -- USART1 RX ISR (ring buffer) + NMEA 0183 sentence
  assembler/checksum validator/field parser (`$GPRMC`, `$GPGGA`), talker-ID
  agnostic (matches on sentence type, not the `GP`/`GN`/... prefix).
- `src/timebase.c`/`.h` -- TIM2 as a free-running 1MHz/32-bit tick counter,
  software-extended to 64 bits via the update-interrupt high word.
- `src/capture.c`/`.h` -- generic TIM2 input-capture ISR + ring buffer +
  `sensor_table[]`, per system-design.md's extensibility contract.
- `src/main.c` -- orchestrates the above; periodic status heartbeat over
  USART2 with GPS/capture/timebase counters and the latest parsed fix.

Stage 1 (bring-up) exists purely to prove the toolchain, vector table,
linker script, and flash flow are correct before any interrupt-driven timing
code goes on top of them.

- `src/startup_stm32l476rg.S` -- vector table transcribed from ST's official
  `cmsis_device_l4` startup file and diffed line-for-line against it (all
  101 vector entries and all 91 weak handler aliases match exactly). Do not
  hand-edit the vector table; re-pull from
  https://github.com/STMicroelectronics/cmsis_device_l4 instead.
- `linker/stm32l476rg.ld` -- **RAM is deliberately limited to 96K
  (SRAM1 only, `0x20000000`-`0x20017FFF`), not the full 128K.** See
  "SRAM2 gotcha" below before changing this.
- `vendor/CMSIS/` -- unmodified, official ST/ARM CMSIS core + STM32L4 device
  headers (register struct definitions only, no HAL driver code). Vendored
  from `STMicroelectronics/cmsis_device_l4` and `ARM-software/CMSIS_5`.
- `src/main.c` -- bring-up test: blinks the Nucleo's user LED (LD2 / PA5) via
  a busy-wait delay. Confirms GPIO + the whole build/link/flash chain work
  on real hardware. (Uses a busy-wait rather than SysTick for now, purely to
  keep this first test minimal -- switch to a SysTick-driven millisecond
  counter as part of the next stage.)

Build verified end-to-end (`make all`): `.isr_vector` lands at `0x08000000`,
the ELF entry point matches `Reset_Handler`'s address exactly. Flashed and
verified on **three different physical Nucleo L476RG boards** via
`make flash` (`device idcode = 0x10076415` -- STM32L47/L48xx Rev 4,
`flash size = 1024 KiB`, `RDP level 0`, `Verified OK` on every flash).

### The SRAM2 gotcha (read before touching RAM size)

The very first version of this bring-up used the full 128K RAM (SRAM1 +
SRAM2, matching the chip's datasheet total) and reproducibly HardFaulted on
real hardware within its first few hundred instructions -- confirmed on a
true cold power-on (no debugger attached) and reproduced identically across
three separate physical boards, ruling out a damaged chip or a debug-session
artifact. Reading the CPU's own fault-status registers (`CFSR`/`HFSR`)
pinned it down precisely: `BFSR.STKERR` -- a bus fault while the hardware
was pushing an exception frame *onto the stack itself*. The stack
(`_estack`) was placed at the very top of the full 128K region, which lands
inside SRAM2's `0x20018000`-`0x2001FFFF` alias -- and every fault's `msp`
landed exactly there.

Limiting the linker script to 96K (SRAM1 only, `_estack = 0x20018000`)
fixed it completely -- confirmed via live register inspection (`CFSR`
cleanly `0x00000000`, `RCC->AHB2ENR` correctly written, `GPIOA->ODR`
observed toggling) and visually (LD2 blinking) on real hardware.

**This was not root-caused further** -- it's unresolved *why* the SRAM2
alias faults (missing enable step for this project, a documented erratum,
or something else) -- just worked around by avoiding it. Practical
implications for later stages:

- Keep all RAM usage (stack, `.bss`, `.data`, any future buffers -- GPS
  UBX/NMEA parse buffers, the capture engine's event ring buffer, etc.)
  within the 96K SRAM1 budget until this is actually root-caused. 96K is
  still generous for what this hub needs.
- If SRAM2's extra 32K is ever needed, treat it as a research task first
  (check RM0351 for any SRAM2-specific clock/enable requirements, check the
  STM32L476 errata sheet), not just a matter of bumping the linker script's
  `LENGTH` back to 128K.

### GPS baud: 115200, not the documented 9600 default

The u-blox 7-series NMEA default is 9600 baud, but *this specific EVK-7P
module* only decodes cleanly at **115200** -- confirmed by sweeping USART1
through {9600, 4800, 19200, 38400, 57600, 115200} while watching for
readable NMEA structure. At every rate except 115200 the module produced
well-timed (~once/sec, matching real NMEA cadence), but 100%-corrupted
traffic -- consistent with correct wiring/electrical connection but wrong
baud, not a hardware fault. Likely explanation: a previous test/config
session changed it via u-center, and the EVK's backup battery retained the
setting across power cycles. `GPS_BAUD` in `main.c` is hardcoded to 115200;
if you swap in a different/factory-reset module, re-run the sweep before
assuming this value still applies.

### The TIM2 forced-update race (read before touching `timebase_init()`)

`timebase_init()` needs `TIM2->PSC` to apply immediately rather than
waiting for the timer's first natural overflow (PSC is always
shadow-buffered in hardware) -- the standard fix is forcing an update event
via `TIM2->EGR = TIM_EGR_UG`. The first version of this code wrote `EGR:UG`
then immediately cleared `SR` before enabling the update interrupt, on the
assumption the clear would race ahead of anything worth clearing. It didn't:
there's real propagation latency between writing `EGR:UG` and `SR:UIF`
actually becoming set, so the clear ran *before* the flag existed (a no-op),
and the delayed-but-genuine `UIF` from that same forced event fired the
instant `DIER:UIE` was enabled a few lines later -- incrementing the
tick-high word for an event that only latched `PSC`, not a real 32-bit
overflow. Symptom on hardware: `tick_high` read `1` (i.e. the tick counter
appeared to be ~71 minutes ahead) within the first ~15 seconds after reset,
while the counter's steady-state rate was correct throughout -- confirmed
via a temporary `TIM2_IRQHandler` entry counter that stayed at exactly `1`
for the whole test, ruling out an interrupt storm, plus a direct register
dump (`PSC=3 ARR=4294967295 DIER=7 CR1=1`, all correct) that ruled out a
configuration bug.

Fix: poll `TIM2->SR & TIM_SR_UIF` until the hardware actually confirms the
forced update happened, *then* clear it, *then* enable the interrupt --
eliminating the race instead of guessing at a delay. See the comment in
`timebase_init()`.

## Building

Targets native Ubuntu (or any Linux host) -- needs `arm-none-eabi-gcc` on
`PATH`. Two ways to get it:

- **apt (simplest):**
  ```bash
  sudo apt update
  sudo apt install gcc-arm-none-eabi
  ```
- **The tarball already in your 18-349 repo**
  (`arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz`) is the
  Linux x86_64-hosted ARM GNU Toolchain build -- it runs natively on Ubuntu
  (this is what was used to verify the build below; only Windows/Git-Bash
  couldn't run it directly). Extract it and add its `bin/` to `PATH`:
  ```bash
  mkdir -p ~/opt && tar -xf arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz -C ~/opt
  export PATH="$HOME/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:$PATH"
  ```

```bash
make            # build/stm32-hub.elf + .bin
make size       # print .text/.data/.bss
make clean
```

## Flashing

```bash
sudo apt install openocd   # if not already installed (the 18-349 OpenOCD build also works)
make flash                 # openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program ... verify reset exit"
```

The Nucleo's onboard ST-LINK handles both flashing and the debug UART -- no
external programmer needed.

**USB permissions on Ubuntu:** if `make flash` fails with something like
`libusb_open() failed` or `Access denied (insufficient permissions)`,
OpenOCD can't open the ST-LINK's USB device as your regular user. Fix once,
not per-flash:
```bash
# openocd's own package usually ships the udev rules already; if flashing
# still fails after installing openocd, install them explicitly:
sudo cp /usr/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```
Then unplug/replug the board. As a quick one-off check (not a real fix),
`sudo make flash` will also work if you just want to confirm the rest of
the flow is fine.

## Next steps (in order -- each gets hardware-verified before the next)

1. ~~**GPS bring-up**~~ -- done. USART1 RX is interrupt-driven, NMEA
   sentences parse and checksum-validate cleanly at 115200 baud.
2. ~~**Timebase**~~ -- done. TIM2 free-running 1MHz/32-bit tick, 64-bit
   software extension, verified correct (see the forced-update race note
   above).
3. ~~**Generic capture scaffolding**~~ -- done. TIM2 CH1 (`PA0`, GPS PPS)
   and CH2 (`PA1`, reserved for radar `SYNC_OUT`) are both configured and
   ISR-driven; adding radar means wiring its (level-shifted) `SYNC_OUT` to
   `PA1` and nothing else, per system-design.md's extensibility contract.
   No real capture events observed yet on either channel, which is expected
   -- GPS PPS needs a fix first (see below) and radar isn't wired in yet.
4. **GPS PPS capture + UTC anchoring**: blocked on confirming whether this
   EVK-7P revision's `TIMEPULSE1` is actually wired to the test connector at
   all (older EVK-7 boards may not be -- see the GPS wiring section below),
   and on getting an actual satellite fix to verify PPS timing against.
   Until then, `capture.c`'s GPS_PPS channel is wired and ready but
   untested with real edges.
5. **Radar SYNC_OUT wiring**: connect via the level shifter (radar's
   `VIOIN` rail is ~1.8V, see `../radar_firmware/README.md`) to `PA1` once
   available -- this is the first real test of the capture path's
   sensor-agnosticism.
6. **Bench test channel(s)**: PB10/PB11 (TIM2_CH3/CH4) reserved for
   waveform-generator testing or the camera strobe -- not wired into
   `capture.c`'s `sensor_table[]` yet.
7. **Binary streaming format**: framed `seq, sensor_id, tick64, utc_ns,
   flags` records per system-design.md, replacing the current
   human-readable heartbeat/capture text once there's real UTC-anchored
   data worth framing.

## Pin plan

| Signal | Pin | Peripheral | Notes |
|---|---|---|---|
| GPS UART TX (STM32&rarr;GPS RXD1) | PA9 | USART1_TX | Nucleo-64 CN10 `D8`; GPS_BAUD=115200 (see above, not the 9600 default) |
| GPS UART RX (STM32&larr;GPS TXD1) | PA10 | USART1_RX | Nucleo-64 CN10 `D2` |
| GPS PPS (timepulse) | PA0 | TIM2_CH1 (input capture) | Nucleo-64 CN8 `A0`; `SENSOR_GPS_PPS` in `capture.h` -- wired + ISR-ready, no edges seen yet (needs a fix, and confirmation this EVK-7P revision exposes `TIMEPULSE1` at all) |
| Radar `SYNC_OUT` (via level shifter) | PA1 | TIM2_CH2 (input capture) | Nucleo-64 CN8 `A1`; `SENSOR_RADAR_SYNC` in `capture.h` -- wired + ISR-ready, not physically connected yet |
| Bench test channel / spare (camera later) | PB10 | TIM2_CH3 (input capture) | Nucleo-64 CN10 `D6`; not yet added to `capture.c`'s `sensor_table[]` |
| Spare | PB11 | TIM2_CH4 (input capture) | Nucleo-64 CN10 `D3`; not yet added to `capture.c`'s `sensor_table[]` |
| Event stream out | PA2/PA3 | USART2 (ST-LINK VCP) | No extra wiring -- same USB cable as flashing; DEBUG_BAUD=115200 |
| User LED (bring-up test) | PA5 | GPIO output | Onboard `LD2` |

This is the standard STM32L476RG LQFP64 alternate-function mapping for
these peripherals -- verify against the
[Nucleo-64 board User Manual (UM1724)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)
pinout diagram for your specific board revision before wiring, since a few
Nucleo pins are reconfigurable via solder-bridge jumpers and could
theoretically be repurposed on a given board.

### GPS (ublox EVK-7P) wiring

Confirmed against the official **u-blox EVK-7/8/M8 User Guide (UBX-14002502)**,
saved at [`../docs/reference/evk78m8_userguide.pdf`](../docs/reference/evk78m8_userguide.pdf)
-- this board does *not* have separate `TXD1`/`RXD1`-labeled pins; it has a
14-pin test connector (numbered right-to-left on the front panel) with a
slide switch multiplexing pins 2/3 between UART and SPI:

| Pin | Label | Wire to |
|---|---|---|
| 14 | `V5_IN` | STM32 5V |
| 13 / 1 | `GND` | STM32 GND (both are ground; either works) |
| 3 | `TxD / MISO` | STM32 `PA10` (USART1_RX) -- module output |
| 2 | `RxD / MOSI` | STM32 `PA9` (USART1_TX) -- module input |
| 8 | `TIMEPULSE1` | STM32 `PA0`, **if present** -- see caveat below |

**Slide switch must be in the I2C position**, not SPI -- in the SPI
position, pins 2/3 carry SPI `MISO`/`MOSI` instead of UART, and the module
stops responding on the 14-pin connector's UART entirely (confirmed: this
was the actual root cause of an earlier "GPS sends nothing" session --
nothing wrong with the wiring itself, the switch was in the wrong position).

**`TIMEPULSE1` caveat**: only present on the test connector on newer
EVK-8N/EVK-M8 revisions. Older EVK-7 boards may not expose it at all -- the
label would read `RESERVED0` instead if so. Check your board's silkscreen
before wiring `PA0`; if it says `RESERVED0`, PPS-based UTC anchoring isn't
possible with this particular board without a hardware mod.

Power each board from its own supply/USB; only tie grounds together unless
the manual explicitly says otherwise.
