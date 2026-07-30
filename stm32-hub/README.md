# stm32-hub

STM32 (Nucleo-64, **STM32L476RG**) firmware for the time-sync hub described
in [`../docs/system-design.md`](../docs/system-design.md). Bare-metal,
register-level C -- CMSIS core + device headers only, no HAL/CubeMX, built
with a plain Makefile (no IDE required).

## Status

**Stage 1 (bring-up) is done and confirmed working on real hardware --
LD2 blinks.** Nothing GPS/timer/capture-related exists yet -- this stage
exists purely to prove the toolchain, vector table, linker script, and flash
flow are correct *before* any interrupt-driven timing code goes on top of
them.

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

1. **GPS bring-up**: USART1 RX interrupt + a minimal NMEA line echo to
   confirm the ublox EVK-7P is talking over UART before writing a real
   parser.
2. **Timebase**: TIM2 configured as a free-running 32-bit tick counter (see
   `../docs/system-design.md`), with the overflow/64-bit-extension handling
   described there.
3. **GPS PPS capture + UTC anchoring**: TIM2 input-capture on the PPS pin,
   paired with parsed UBX/NMEA time to build the tick&rarr;UTC mapping.
4. **Generic capture channel(s) for bench testing**: one or two more TIM2
   input-capture channels driven by a waveform generator, standing in for
   the radar/camera pulses until the level shifter and ZED camera research
   are ready -- this is what exercises the sensor-agnostic capture path
   end-to-end.
5. **Streaming**: frame captured events out over USART2 (the ST-LINK Virtual
   COM Port -- no extra wiring, appears as a COM port over the same USB
   cable already used to flash).

## Pin plan

| Signal | Pin | Peripheral | Notes |
|---|---|---|---|
| GPS UART TX (STM32&rarr;GPS RXD1) | PA9 | USART1_TX | Nucleo-64 CN10 `D8` |
| GPS UART RX (STM32&larr;GPS TXD1) | PA10 | USART1_RX | Nucleo-64 CN10 `D2` |
| GPS PPS (timepulse) | PA0 | TIM2_CH1 (input capture) | Nucleo-64 CN8 `A0` |
| Bench test channel #1 (waveform gen. now; radar later) | PA1 | TIM2_CH2 (input capture) | Nucleo-64 CN8 `A1` |
| Bench test channel #2 / spare (camera later) | PB10 | TIM2_CH3 (input capture) | Nucleo-64 CN10 `D6` |
| Spare | PB11 | TIM2_CH4 (input capture) | Nucleo-64 CN10 `D3` |
| Event stream out | PA2/PA3 | USART2 (ST-LINK VCP) | No extra wiring -- same USB cable as flashing |
| User LED (bring-up test) | PA5 | GPIO output | Onboard `LD2` |

This is the standard STM32L476RG LQFP64 alternate-function mapping for
these peripherals -- verify against the
[Nucleo-64 board User Manual (UM1724)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)
pinout diagram for your specific board revision before wiring, since a few
Nucleo pins are reconfigurable via solder-bridge jumpers and could
theoretically be repurposed on a given board.

### GPS (ublox EVK-7P) wiring

The EVK-7P breaks out the NEO-7P module's `VCC`, `GND`, `TXD1`/`RXD1`
(UART1), and `TIMEPULSE` (PPS) signals on a pin header -- these should be
directly labeled on the board's silkscreen. I don't have your exact board
revision's header pin numbering memorized with confidence, so **before
wiring**, confirm against your EVK-7P's own labels / the
[u-blox EVK-7 User Guide (UBX-13003736)](https://www.u-blox.com) for:

- Which physical pin is `TXD1` (module output &rarr; wire to STM32 `PA10`),
  `RXD1` (module input &larr; wire to STM32 `PA9`), `TIMEPULSE`/`PPS`
  (&rarr; STM32 `PA0`), and `GND` (common ground with the STM32 -- required).
- The I/O logic level the EVK-7P actually drives these pins at. u-blox 7
  modules are typically ~3.0V nominal VCC and I/O; confirm this against the
  EVK-7P datasheet before assuming direct compatibility with the L476's
  3.3V inputs -- same caution as the radar's `VIOIN` rail in
  [`../radar_firmware/README.md`](../radar_firmware/README.md).
- Do **not** power the EVK-7P from the STM32 or vice versa unless the
  manual explicitly says the board is designed for that -- power each board
  from its own supply/USB and only tie grounds together, unless confirmed
  otherwise.
