# stm32-hub

STM32 (Nucleo-64, **STM32L476RG**) firmware for the time-sync hub described
in [`../docs/system-design.md`](../docs/system-design.md). Bare-metal,
register-level C -- CMSIS core + device headers only, no HAL/CubeMX, built
with a plain Makefile (no IDE required).

## Status

**Everything through 3-channel PPS-anchored hardware capture is done and
confirmed working end-to-end on real hardware.** Current setup: an EVK-M8T
GPS module (swapped in from the original EVK-7P specifically because it
exposes a real `TIMEPULSE1` pin -- see "GPS wiring" below) provides both a
full 3D fix (position/time/date all parsing correctly) and a working PPS
anchor (`pps_anchor=YES`, capture events firing at 1Hz with computed
`utc_ns`). A waveform generator standing in for the radar/camera confirmed
the whole pipeline -- capture, PPS anchoring, zero data loss, zero spurious
edges -- against a real 30Hz signal (see "Debug UART TX bottleneck" and
"Input capture bounce filter" below for the two real bugs this exposed and
fixed). `../docs/system-design.md`'s TIM2 free-running timebase + generic
capture-ISR architecture is fully in place. Source layout:

- `src/uart.c`/`.h` -- generic blocking UART TX helpers (`uart_init`/
  `uart_putc`/`uart_puts`/`uart_put_udec`), used internally by
  `debug_uart.c` and `gps.c` for peripheral setup.
- `src/debug_uart.c`/`.h` -- buffered, `TXE`-interrupt-driven USART2 (ST-LINK
  VCP) TX. Owns PA2/PA3 setup. See "Debug UART TX bottleneck" below for why
  a plain blocking `uart_putc()` wasn't enough once a 3rd capture channel
  and continuous GPS mirroring were both writing to the same link.
- `src/gps.c`/`.h` -- USART1 RX ISR (ring buffer) + NMEA 0183 sentence
  assembler/checksum validator/field parser (`$GPRMC`, `$GPGGA`), talker-ID
  agnostic (matches on sentence type, not the `GP`/`GN`/... prefix --
  confirmed working against both plain `$GP*` and combined `$GN*`/`$GL*`
  GNSS sentences).
- `src/timebase.c`/`.h` -- TIM2 as a free-running 1MHz/32-bit tick counter,
  software-extended to 64 bits via the update-interrupt high word.
- `src/capture.c`/`.h` -- generic TIM2 input-capture ISR + ring buffer +
  `sensor_table[]`, per system-design.md's extensibility contract. Three
  live channels: `gps_pps` (`PA0`), `radar_sync` (`PA1`), `camera_sync`
  (`PB10`). Includes a digital noise filter (`ICxF`) on all three channels
  -- see "Input capture bounce filter" below.
- `src/timesync.c`/`.h` -- PPS-anchored tick&rarr;UTC mapping: pairs a
  `SENSOR_GPS_PPS` capture event's tick with the UTC second the next parsed
  GPS time update reports, then converts any tick to UTC nanoseconds via
  that linear anchor. **Confirmed working against a real PPS-capable
  module** (`pps_anchor=YES`, sustained over multi-minute runs).
- `src/main.c` -- orchestrates the above; periodic status heartbeat over
  USART2 with GPS/capture/timebase/anchor counters and the latest parsed
  fix.
- `../host/stm32_logger.py` -- host-side auto-reconnecting logger; parses
  tick-bearing lines and logs one CSV row per event: the sensor edge's own
  UTC time, the nearest real PPS second boundary's UTC time, and the
  difference between them (`offset_from_pps_ms`) -- tagged `stm32_pps`
  (hardware-anchored, trustworthy) or `host_ntp_fallback` (host wall-clock,
  tens-of-ms jitter). See "Running the full pipeline" below for the exact
  commands and what each column means.
- `../host/pps_offset_stats.py` -- reads a `stm32_logger.py` CSV and
  reports, per sensor, the offset-from-PPS-edge and inter-event-spacing
  statistics (mean/stddev/min/max). This is the direct "how far was the
  radar/camera from the nearest PPS edge, with statistics" tool.

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

### GPS baud: check it per module, don't assume

`GPS_BAUD` in `main.c` is currently **9600** -- the EVK-M8T (in current use,
see "GPS wiring" below) decodes cleanly at its documented default, no
surprises. This was NOT true of the original EVK-7P: despite 9600 being the
documented u-blox 7-series NMEA default, that specific module only decoded
cleanly at **115200** -- confirmed by sweeping USART1 through {9600, 4800,
19200, 38400, 57600, 115200} while watching for readable NMEA structure. At
every rate except 115200 that module produced well-timed (~once/sec,
matching real NMEA cadence) but 100%-corrupted traffic -- consistent with
correct wiring/electrical connection but wrong baud, not a hardware fault.
Likely explanation: a previous test/config session changed it via
u-center, and the EVK's backup battery retained the setting across power
cycles.

**Takeaway: if you swap GPS modules, re-run this sweep rather than assuming
either the documented default or whatever's currently in `main.c` still
applies.** A module's actual configured baud isn't always its
documented/factory default.

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

### The NMEA field-offset bug (why fixes silently parsed as "no fix")

Early testing showed the raw NMEA mirror streaming perfectly (`checksum_errors=0`
throughout) while the parsed `[gps]` summary line insisted `fix=no
time=--:--:-- lat=-- lon=--` no matter what -- including once a real
satellite fix was live and the raw `$GPRMC`/`$GPGGA` sentences clearly
showed `A`/quality `1` and valid coordinates. `gps.c`'s sentence dispatch
was calling `parse_rmc(&line_buf[6])`/`parse_gga(&line_buf[6])`, but index 6
is the comma right after `GPRMC`/`GPGGA` (`$GPRMC` occupies indices 0-5) --
field 0 actually starts at index 7. `nth_field(body, 0)` on the
off-by-one body pointer returned the leading comma itself, which
`field_empty()` correctly (but uselessly) sees as always-empty; every other
field index was shifted the same way, so e.g. the "satellites" field
(index 6) was actually reading fix-quality's single digit. Fixed by
starting both parsers at `&line_buf[7]`. Confirmed against a real live fix
after the fix: `fix=YES time=21:18:13 date=2026-08-06 sats=11
lat=40.4442073 lon=-79.9474663`, matching the raw NMEA exactly.

### Debug UART TX bottleneck (why events and GPS bytes were silently dropped)

Once a 3rd capture channel (`camera_sync`, driven by a waveform generator at
~44Hz including bounce artifacts -- see below) was added on top of the
continuous GPS NMEA mirror, live testing showed real data loss:
`capture_dropped=3154` and `rx_dropped=590` (with `checksum_errors=18`)
over a 60-second run. Root cause: every debug-output consumer (raw NMEA
mirror, capture-event lines, heartbeat) funneled through blocking,
byte-at-a-time `uart_putc()` calls on USART2 -- printing one capture-event
line took ~6.5ms at 115200 baud, and with events arriving faster than that
during bursts, the main loop stalled long enough for both the GPS RX ring
buffer (`gps.c`) and the capture ring buffer (`capture.c`) to overflow.
Critically, the *timestamp precision itself* wasn't affected (the capture
ISR still latches the exact tick before anything gets queued for
printing) -- whole events were being discarded outright, not recorded with
a wrong time.

Fix: `debug_uart.c` replaces direct USART2 writes with a 2048-byte RAM ring
buffer drained by a `TXE`-interrupt handler in the background, so
`debug_putc()` returns almost immediately instead of blocking on the wire.
Confirmed fixed against the same load that caused the drops:
`capture_dropped=0 rx_dropped=0 checksum_errors=0` sustained over a 60s run.

### Input capture bounce filter (why ~32% of camera_sync edges were fake)

With `debug_uart.c` fixed and a clean 30Hz/50%-duty square wave from a
waveform generator wired to `PB10`, inter-event tick-delta analysis showed
a real 30Hz peak (`~33,500` ticks apart, matching expectations) *plus* a
second cluster of spurious near-simultaneous captures (`<5000` ticks apart)
making up 31.8% of all detected edges -- consistent with signal
bounce/ringing crossing the logic threshold twice per real edge, not a
firmware bug.

Fix: enabled TIM2's built-in input capture digital filter (`ICxF` in
`CCMR1`/`CCMR2`) on all three channels, code `0b0011` = `fSAMPLING=fCK_INT,
N=8` (~2us debounce at this chip's ~4MHz `fCK_INT`) -- verified against the
STM32L4 reference manual (RM0351, `TIMx_CCMR1`/`TIMx_CCMR2` register
description) rather than assumed from memory. Confirmed fixed: re-running
the same 30Hz test afterward showed a single clean peak with **inter-event
spacing stddev collapsing from 15,551.6 ticks (68% relative jitter) to 6.3
ticks (0.02%)**, all 1,764 events landing in a 38-tick-wide band around the
true period, zero spurious edges.

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
   sentences parse and checksum-validate cleanly.
2. ~~**Timebase**~~ -- done. TIM2 free-running 1MHz/32-bit tick, 64-bit
   software extension, verified correct (see the forced-update race note
   above).
3. ~~**Generic capture scaffolding**~~ -- done. All three channels (`gps_pps`
   on `PA0`, `radar_sync` on `PA1`, `camera_sync` on `PB10`) configured and
   ISR-driven, per system-design.md's extensibility contract.
4. ~~**GPS PPS capture + UTC anchoring**~~ -- done and **confirmed working**.
   The original EVK-7P board couldn't do this (its silkscreen reads
   `RESERVED0`, not `TIMEPULSE1` -- the time pulse signal isn't routed to
   its test connector at all); swapping to an EVK-M8T (which does expose
   `TIMEPULSE1`) fixed this permanently. `pps_anchor=YES`, capture events
   firing at 1Hz with correct `utc_ns`, sustained over multi-minute runs.
   See "GPS wiring" below for the EVK-M8T pinout.
5. ~~**Camera channel bench test**~~ -- done. A waveform generator (30Hz,
   50% duty, 3.3V/0V square wave) wired to `PB10` confirmed the full
   capture -> PPS-anchor -> UTC pipeline end to end: 1,764 clean edges over
   60s, zero drops, zero spurious captures (see the two bug writeups
   above). This is the same path radar/camera will use once wired in.
6. **Radar SYNC_OUT wiring**: connect via the level shifter (radar's
   `VIOIN` rail is ~1.8V, see `../radar_firmware/README.md`) to `PA1` once
   available. Firmware side needs no changes -- same channel, same code
   path already proven with the waveform generator.
7. **Camera SYNC_OUT wiring**: connect the real ZED camera's strobe/trigger
   output to `PB10` once available, replacing the waveform generator stand-in.
8. **Binary streaming format**: framed `seq, sensor_id, tick64, utc_ns,
   flags` records per system-design.md, replacing the current
   human-readable heartbeat/capture text -- worth doing once there's a real
   consumer (e.g. the Jetson) parsing the stream programmatically rather
   than a human reading it live.

## Pin plan

| Signal | Pin | Peripheral | Notes |
|---|---|---|---|
| GPS UART TX (STM32&rarr;GPS RXD1) | PA9 | USART1_TX | Nucleo-64 CN10 `D8`; GPS_BAUD=9600 (EVK-M8T's documented default -- was 115200 for the old EVK-7P, a different physical module) |
| GPS UART RX (STM32&larr;GPS TXD1) | PA10 | USART1_RX | Nucleo-64 CN10 `D2` |
| GPS PPS (timepulse) | PA0 | TIM2_CH1 (input capture) | Nucleo-64 CN8 `A0`; `SENSOR_GPS_PPS` in `capture.h` -- **confirmed working** with the EVK-M8T (pin 8 = real `TIMEPULSE 1`, unlike the EVK-7P's `RESERVED0`). `pps_anchor=YES`, capture events firing at 1Hz with `utc_ns` populated. |
| Radar `SYNC_OUT` (via level shifter) | PA1 | TIM2_CH2 (input capture) | Nucleo-64 CN8 `A1`; `SENSOR_RADAR_SYNC` in `capture.h` -- wired + ISR-ready, not physically connected yet |
| Camera `SYNC_OUT` (ZED) | PB10 | TIM2_CH3 (input capture) | Nucleo-64 CN10 `D6`; `SENSOR_CAMERA_SYNC` in `capture.h` -- **confirmed working**, bench-tested with a waveform generator (30Hz square wave) standing in for the real camera: 1,764 clean events over 60s, zero drops, zero spurious edges. Wire the real camera's `SYNC_OUT` here to replace the stand-in. |
| Spare | PB11 | TIM2_CH4 (input capture) | Nucleo-64 CN10 `D3`; not yet added to `capture.c`'s `sensor_table[]` |
| Event stream out | PA2/PA3 | USART2 (ST-LINK VCP) | No extra wiring -- same USB cable as flashing; DEBUG_BAUD=115200 |
| User LED (bring-up test) | PA5 | GPIO output | Onboard `LD2` |

This is the standard STM32L476RG LQFP64 alternate-function mapping for
these peripherals -- verify against the
[Nucleo-64 board User Manual (UM1724)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)
pinout diagram for your specific board revision before wiring, since a few
Nucleo pins are reconfigurable via solder-bridge jumpers and could
theoretically be repurposed on a given board.

### GPS (ublox EVK-M8T) wiring -- current board, PPS confirmed working

**Quick answer**: pin 3 (`TxD / MISO`) &rarr; STM32 `PA10`. Pin 2
(`RxD / MOSI`) &rarr; STM32 `PA9`. Pin 8 (`TIMEPULSE 1`) &rarr; STM32 `PA0`.
Slide switch must be in the **I2C** position (not SPI) for pins 2/3 to
actually carry UART TxD/RxD instead of SPI MISO/MOSI.

Confirmed against the official **EVK-M8T User Guide (UBX-14041540)** --
same 14-pin test connector layout (numbered right-to-left on the front
panel) as the EVK-7/8/M8 family, but with **real, working `TIMEPULSE 1` and
`TIMEPULSE 2` signals** where the EVK-7P had `RESERVED0`/`RESERVED1` (see
below):

| Pin | Label | Wire to |
|---|---|---|
| 14 | `V5_IN` | STM32 5V |
| 13 / 1 | `GND` | STM32 GND (both are ground; either works) |
| 8 | `TIMEPULSE 1` | STM32 `PA0` (TIM2_CH1) -- **confirmed working**, `pps_anchor=YES` |
| 3 | `TxD / MISO` | STM32 `PA10` (USART1_RX) -- module output |
| 2 | `RxD / MOSI` | STM32 `PA9` (USART1_TX) -- module input |

**Slide switch must be in the I2C position**, not SPI -- in the SPI
position, pins 2/3 carry SPI `MISO`/`MOSI` instead of UART, and the module
stops responding on the 14-pin connector's UART entirely.

GPS_BAUD is **9600** for this module (its documented default -- see "GPS
baud" above). Power each board from its own supply/USB; only tie grounds
together unless the manual explicitly says otherwise.

### GPS (ublox EVK-7P) wiring -- original board, no PPS, kept for reference

The original board used for this project's early GPS bring-up. Its UART
wiring is otherwise identical to the EVK-M8T above (pin 3 &rarr; `PA10`,
pin 2 &rarr; `PA9`, same I2C-not-SPI switch requirement), but confirmed
against the official **u-blox EVK-7/8/M8 User Guide (UBX-14002502)**, saved
at [`../docs/reference/evk78m8_userguide.pdf`](../docs/reference/evk78m8_userguide.pdf):

**`TIMEPULSE1` caveat -- CONFIRMED NOT AVAILABLE on this board.** Checked
the silkscreen: pin 8 reads `RESERVED0`, not `TIMEPULSE1`. Per the user
guide, older EVK-7 revisions don't route the time pulse signal to the test
connector at all -- it's still generated internally (drives the
front-panel LED's 1Hz blink once locked), just not accessible from any
pin. **PPS-based UTC anchoring is not possible with this physical board**
without a hardware modification (e.g. tapping the LED's own PCB trace/via
directly -- untested, not attempted, real risk of board damage). This is
the reason the project switched to the EVK-M8T above. GPS_BAUD for this
specific module was **115200**, not its documented 9600 default -- see
"GPS baud" above.

## Running the full pipeline: wiring + every command

End-to-end walkthrough: wire everything up, build, flash, watch it live,
log to CSV with UTC timestamps, and compute PPS-offset statistics. This is
the exact sequence used to validate the 3-channel capture pipeline against
a waveform generator (see the two bug writeups above).

### 1. Wiring

| Signal | Board pin | STM32 pin |
|---|---|---|
| GPS TxD (module output) | EVK-M8T pin 3 | `PA10` |
| GPS RxD (module input) | EVK-M8T pin 2 | `PA9` |
| GPS TIMEPULSE 1 | EVK-M8T pin 8 | `PA0` |
| GPS GND | EVK-M8T pin 13 or 1 | STM32 `GND` |
| GPS power | EVK-M8T pin 14 (`V5_IN`) | STM32 `5V` |
| Radar `SYNC_OUT` (via level shifter) | -- | `PA1` |
| Camera `SYNC_OUT` (or a waveform generator standing in for it) | -- | `PB10` |
| Flash + debug console | -- | USB cable into the Nucleo's ST-LINK port (no extra wiring) |

Double-check the EVK-M8T's slide switch is in the **I2C** position (not
SPI) -- see "GPS wiring" above. Power the GPS module and STM32 from
separate supplies/USB ports; only tie grounds together.

### 2. Build and flash

Needs `arm-none-eabi-gcc` and `openocd` on `PATH` -- see "Building"/
"Flashing" above for how to get them. If your ST-LINK is behind WSL2 (this
project's actual dev setup), attach it first:
```powershell
# Windows PowerShell, one-time per session (or after replugging the board)
usbipd list                        # find the busid, e.g. 2-2
usbipd attach --wsl --busid 2-2
```
Then build and flash from the Linux side:
```bash
cd stm32-hub
make            # build/stm32-hub.elf + .bin
make flash      # openocd program + verify + reset
```
If you're on native Linux with the ST-LINK directly attached, skip the
`usbipd` step -- `make flash` alone is enough.

Detach from WSL afterward so the ST-LINK's virtual COM port becomes
visible to Windows tools (`monitor_com.ps1`, `stm32_logger.py` if you run
it against a Windows COM port instead of `/dev/ttyACM0`):
```powershell
usbipd detach --busid 2-2
```

### 3. Watch it live (quick sanity check)

From Windows PowerShell, using the COM port directly:
```powershell
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process -Force
& "..\host\monitor_com.ps1" -Seconds 30
```
Press the STM32's black **NRST** button right after it prints
`Listening...` to catch a clean boot banner. You should see raw NMEA
sentences, `[gps] fix=...` summaries, `[capture] sensor=... tick=...
utc_ns=...` lines once PPS is anchored, and a `[stm32-hub] alive (...)`
heartbeat roughly every few seconds with `pps_anchor=YES` and all the
`*_dropped`/`*_errors` counters at `0`.

Add `-Raw` for a hex+ASCII byte dump instead (useful for diagnosing
framing/signal issues), or `-Seconds N` for a longer/shorter capture.

### 4. Log to CSV with UTC timestamps

`stm32_logger.py` (needs `pip install pyserial`, and `pyyaml` only if using
`--config`) parses the live stream and writes one CSV row per event:
```bash
cd host
python3 stm32_logger.py --port /dev/ttyACM0 --csv capture_log.csv
# or on native Windows Python: --port COM10
```
Press Ctrl-C to stop (it flushes and closes the CSV cleanly). Runs
indefinitely, auto-reconnecting if the port drops.

CSV columns:

| Column | Meaning |
|---|---|
| `sensor` | `gps_pps`, `radar_sync`, `camera_sync`, or a heartbeat line (`heartbeat:N`) |
| `sensor_edge_utc` | UTC time (microsecond precision) that sensor's edge actually happened at |
| `nearest_pps_edge_utc` | UTC time of the whole-second GPS-PPS boundary this edge is measured against |
| `offset_from_pps_ms` | `sensor_edge_utc` minus `nearest_pps_edge_utc`, in milliseconds (+ = after that edge, - = before the next one) |
| `precision_source` | `stm32_pps` (real hardware-anchored precision, trust these) or `host_ntp_fallback` (host wall-clock, tens-of-ms jitter -- see the module docstring for why) |

This is the direct "how far was the sensor from the nearest PPS edge"
answer, already computed per row -- no post-processing needed to read it.

### 5. Compute offset/jitter statistics

```bash
python3 pps_offset_stats.py capture_log.csv                    # all sensors
python3 pps_offset_stats.py capture_log.csv --sensor camera_sync  # just one
```
Only uses `stm32_pps`-tagged rows (fallback rows are too imprecise to be
meaningful here). Reports, per sensor:
- **Offset from nearest PPS edge**: mean/stddev/min/max, in ms/us -- how
  consistently the sensor's edges land relative to true GPS time. For a
  sensor with no fixed phase relationship to GPS seconds (e.g. an
  independent free-running waveform generator), expect this to spread
  close to uniformly across the full second (theoretical stddev for a
  uniform &plusmn;500ms spread is `1000/sqrt(12) ~= 288.7ms`).
- **Inter-event spacing**: mean/stddev/min/max time between consecutive
  edges of that sensor -- how consistent the sensor's own rate is, and
  (via the stddev specifically) whether captures look clean or show signs
  of bounce/noise (a clean signal has 3-4 orders of magnitude tighter
  stddev than one with bounce artifacts -- see "Input capture bounce
  filter" above for a worked example).
