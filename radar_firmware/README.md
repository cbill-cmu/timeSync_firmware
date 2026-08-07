# radar_firmware

Custom TI mmWave MSS/DSS firmware for an **AWR1843AOP**-based board, forked
from TI's mmWave SDK demo to add a frame-sync GPIO pulse (software-clock-driven,
not a hardware trigger -- see [`xwr18xx-custom/mss/mss_main.c`](xwr18xx-custom/mss/mss_main.c)
and the "Hardware Reference" section below) for external timestamp
correlation with an MCU. Derived from the TI mmWave demo
firmware (BSD-3); modifications licensed under similar terms (MIT) -- see
[`../LICENSE`](../LICENSE) and [`../LICENSE-MODIFICATIONS`](../LICENSE-MODIFICATIONS).
See the [main `xwr` documentation](https://radarml.github.io/xwr/firmware)
for background on the upstream project this was forked from.

This is the radar half of the overall time-sync system -- see
[`../docs/system-design.md`](../docs/system-design.md) for how it fits with
the STM32 hub, GPS, and camera.

## Folder Layout

- `xwr18xx-custom/` -- the actively developed firmware (frame-sync GPIO
  additions, CLI/platform tweaks). This is what you build and flash for this
  project.
- `xwr18xx/` -- baseline demo, kept for reference/diffing against upstream.
- `release.sh` -- packages `xwr18xx-custom`'s built `.bin` files into
  `release.tar`.

Host-side scripts for talking to this board over serial
(`debug_console.py`, `sync.py`, `victim.yaml`) live in
[`../host/`](../host/) -- referenced below with paths relative to this
folder.

## Prerequisites

- **TI mmWave SDK 3.6.02.00-LTS** (or a compatible 3.6.x release) installed separately --
  this repo is not built inside the SDK tree, it's built against it via
  `MMWAVE_SDK_INSTALL_PATH`.
- **TI code generation tools** for both cores, installed as part of the SDK setup:
  R4F compiler (MSS / Cortex-R4F) and C6000 compiler (DSS / C674x DSP).
- **Code Composer Studio** (optional but recommended) for JTAG debug/flash; not required
  just to build or to flash via UniFlash.
- **TI UniFlash** (cloud: https://dev.ti.com/uniflash/, or desktop:
  http://www.ti.com/tool/UNIFLASH) for flashing over UART.
- Linux build hosts additionally need:
  ```bash
  sudo dpkg --add-architecture i386
  sudo apt-get update
  sudo apt-get install libc6:i386 libstdc++6:i386 mono-complete -y
  ```
- Python 3 + `uv` for the host-side tooling (`../host/debug_console.py`, `../host/sync.py`):
  ```bash
  uv run python ../host/debug_console.py --config ../host/victim.yaml
  ```

## Downloading / Environment Setup

1. Install the mmWave SDK from TI (`ti.com/tool/mmwave-sdk`), matching the 3.6.x line
   this firmware targets.
2. Set up the SDK build environment (run once per shell session):
   - **Windows:** `%MMWAVE_SDK_INSTALL_PATH%\packages\scripts\windows\setenv.bat`
   - **Linux:** `source $MMWAVE_SDK_INSTALL_PATH/packages/scripts/unix/setenv.sh`

   This must be run from within that `scripts/...` directory, and should print
   `mmWave Build Environment Configured` on success.
3. Clone/pull the parent repo (`git@github.com:cbill-cmu/timeSync_firmware.git`)
   alongside (not inside) the SDK install.

## Building

From `xwr18xx-custom/`, with the SDK environment sourced and these variables set for
the AWR1843AOP:

```bash
export MMWAVE_SDK_DEVICE=awr18xx
export MMWAVE_SDK_DEVICE_TYPE=xwr18xx
export PLATFORM_DEFINE=SOC_XWR18XX
export BOARD_DEVICE=AOP
export XDC_PLATFORM_DEFINE=AWR18XX

gmake clean
gmake all          # builds both mmwDemo and mmwAOPDemo (MSS + DSS)
```

Other useful targets (`gmake help` prints these):

| Target | Effect |
|---|---|
| `mmwDemo` | Build standard MSS+DSS demo image |
| `mmwAOPDemo` | Build the AOP-variant MSS+DSS demo image (this board) |
| `mssDemo` / `dssDemo` | Build just one core |
| `clean` | Remove all built objects/binaries |

### Build output

Successful builds land directly in `xwr18xx-custom/`:

- `xwr18xx_mmw_demo.bin`
- `xwr18xx_mmw_aop_demo.bin` -- use this one for the AOP board.

Each `.bin` is a combined metaImage (MSS + BSS/RADARSS + DSS) ready for UniFlash.

## Flashing

1. **Put the board in flash-programming mode** via the SOP2/SOP1/SOP0 sense-on-power
   jumpers/switches (power off first):

   | SOP2 | SOP1 | SOP0 | Mode |
   |---|---|---|---|
   | 0 | 0 | 1 | Functional (runs app from flash) |
   | 1 | 0 | 1 | Flash programming (bootloader waits for UniFlash) |

   This table is the standard xWR1xxx BoosterPack SOP mapping -- confirm it against
   this board's own schematic, since it's a custom design and may strap SOP
   differently than the reference EVM.
2. Power up the board and identify its UART port. On the stock BoosterPack EVM this
   shows as "XDS110 Class Application/User UART"; on this AOP board (CP2105 bridge) the
   same role is the CP2105's **Enhanced (`if00`)** port -- the same port used for the
   CLI in the Connecting/Running section below. See
   `/dev/serial/by-id/...if00-port0` on Linux, or Device Manager on Windows.
3. Open UniFlash, select the device (AWR1843AOP), point it at that COM port, and load
   `xwr18xx_mmw_aop_demo.bin` as the image. Follow the "UniFlash User Guide for mmWave
   Devices" for the rest of the GUI flow.
4. **Switch back to functional mode** (SOP2=0, SOP0=1) and power-cycle the board to
   boot the newly flashed image.

## Connecting / Running

- CLI/config port: 115200 baud.
- On this board, use the CP2105's **Enhanced (`if00`)** control port for the CLI --
  that's the port `../host/victim.yaml` should reference. `ls -l /dev/serial/by-id/` to
  find it.
- Because the CP2105 loses power (and its `/dev` node) on a full board power cycle,
  use `debug_console.py` instead of a plain terminal to keep logging across resets:
  ```bash
  cp ../host/victim.example.yaml ../host/victim.yaml   # first time only -- fill in your board's port
  uv run python ../host/debug_console.py --config ../host/victim.yaml
  ```
- For radar chirp/frame config and capture parameters, see `../host/victim.yaml`.

## Hardware Reference: Sync/GPIO Pinout & Where to Probe

> Sourced from TI's reference schematics for this chip family -- the xWR1843BOOST EVM
> User's Guide (SPRUIM4B) for the radar-board connector, and the DCA1000EVM Data
> Capture Card User's Guide (SPRUIJ4A) for the DCA1000 side. **This is a custom board,
> not the reference EVM.** The pin-19 mapping below is confirmed against this
> firmware's own code comment; the rest is inherited from the reference design and not
> independently verified against this board's actual schematic/silkscreen. Confirm
> before soldering or probing anything.

### Currently used: the frame-sync pulse

Driven in `MmwDemo_syncToggleClockFxn` ([`xwr18xx-custom/mss/mss_main.c`](xwr18xx-custom/mss/mss_main.c)):

- **`GPIO_31`**, wired to `DMM_DP0` -- physically routed to **pin 19** of the 60-pin HD
  connector (this board's `J11`). This is the output meant for an external MCU to tap.
- **`GPIO_2`** (onboard `DS3` LED) -- mirrors the same toggle, for visual confirmation
  without any external wiring.
- **This is a per-frame pulse, not a per-chirp pulse.** It's driven by a plain
  RTOS `Clock_create()` software timer set to the configured frame period (see
  `MmwDemo_startSensor` in `mss_main.c`) -- there is no real hardware chirp or
  frame-start event backing it (see "Why not a real hardware chirp signal?"
  below). **`../host/victim.yaml` currently sets `frame_length: 1`** (one
  chirp per frame), which makes this pulse chirp-accurate *for that config
  only* -- frame-sync and chirp-sync are the same event when there's exactly
  one chirp per frame. If `frame_length` is ever raised above 1, this pulse
  no longer marks individual chirps, only the group; getting true per-chirp
  timing at that point means either computing each chirp's offset within the
  frame from the profile's `idle_time`/`ramp_end_time`/`tx_start_time` (the
  chirps are evenly spaced, so this is deterministic), or hooking the DSS's
  internal per-chirp ADC interrupt (see below -- untested, and still not a
  hardware-timed edge).
- **Voltage: ~1.8V nominal, measured ~1.72V under load.** This is the chip's shared
  `VIOIN` rail -- every digital I/O pin on the chip is on this same rail, so a 3.3V/5V
  MCU input needs a level shifter (e.g. `74LVC1T45`, or a BSS138 shifter) on this line
  regardless of which pin below you use.

### Why not a real hardware chirp signal? (tried, didn't work)

Two real hardware-level attempts were made before settling on the software-clock
approach above -- kept here so it isn't re-attempted from scratch later:

- **`CHIRP_START`/`FRAME_START` pinmux functions on pin K13 (GPIO_2/DS3).** This
  chip's pinmux has named alternate functions for these signals, and pointing K13 at
  them was confirmed -- via pad-register readback -- to land correctly in hardware.
  But RadarSS never actually drove either signal while chirping; nothing toggled.
  This matches TI's own statement for the related AWR2243 that there is no real
  hardware path bringing a chirp-interrupt out to a pin. See
  `MmwDemo_platformInit` in [`xwr18xx-custom/mss/mmw_platform.c`](xwr18xx-custom/mss/mmw_platform.c).
- **`SYNC_OUT` on ball M3 (pad `PING13_PADBC`, `GPIO_29`).** Same story: pinmux
  landed, but nothing ever toggled it while running. Left pinmuxed as a plain GPIO,
  driven low once at init and otherwise unused.

Given both dead ends, the `Clock_create()` software timer (frame-period only, see
above) is what's actually driving the pulse on this board today -- not a hardware
trigger of any kind, chirp- or frame-level.

### 60-pin HD connector (reference: "J1" on xWR1843BOOST / "J3" on DCA1000EVM; this board: "J11")

| Pin | Signal | | Pin | Signal |
|---|---|---|---|---|
| 1 | 5V | | 2 | unclear in source -- possibly TDO/JTAG, verify visually |
| 3 | 5V | | 4 | NC (per DCA1000-side doc) |
| 5 | TDI (JTAG) | | 6 | TCK (JTAG) |
| 7 | SPI_CS | | 8 | TMS (JTAG) |
| 9 | SPI_CLK | | 10 | HOSTINT |
| 11 | SPI_MOSI | | 12 | SPI_MISO |
| 13 | PGOOD (onboard VIO) | | 14 | NERROUT |
| 15 | DMM_CLK | | 16 | **SYNC_IN** (dedicated HW trigger input) |
| 17 | DMM_SYNC | | 18 | GND |
| **19** | **TRACE_DATA0 / DMM_DP0 -- in use (GPIO_31)** | | 20 | NC |
| 21 | TRACE_DATA1 / DMM_DP1 | | 22 | NC |
| 23 | TRACE_DATA2 / DMM_DP2 | | 24 | GND |
| 25 | TRACE_DATA3 / DMM_DP3 | | 26 | LVDS_FRCLKP |
| 27 | TRACE_DATA4 / DMM_DP4 | | 28 | LVDS_FRCLKN |
| 29 | TRACE_DATA5 / DMM_DP5 | | 30 | GND |
| 31 | TRACE_DATA6 / DMM_DP6 | | 32 | NC |
| 33 | TRACE_DATA7 / DMM_DP7 | | 34 | NC |
| 35 | TRACE_DATA8 / DMM_DP8 | | 36 | GND |
| 37 | TRACE_DATA9 / DMM_DP9 | | 38 | NC |
| 39 | TRACE_DATA10 / DMM_DP10 | | 40 | NC |
| 41 | TRACE_DATA11 / DMM_DP11 | | 42 | GND |
| 43 | TRACE_DATA12 / DMM_DP12 | | 44 | LVDS_CLKP |
| 45 | TRACE_DATA13 / DMM_DP13 | | 46 | LVDS_CLKM |
| 47 | TRACE_DATA14 / DMM_DP14 | | 48 | GND |
| 49 | TRACE_DATA15 / DMM_DP15 | | 50 | LVDS_1P |
| 51 | I2C_SDA | | 52 | LVDS_1M |
| 53 | I2C_SCL | | 54 | GND |
| 55 | RS232RX (Rx into AWR device) | | 56 | LVDS_0P |
| 57 | RS232TX (Tx from AWR device) | | 58 | LVDS_0M |
| 59 | nRESET (`MCU_RSTn`) | | 60 | GND |

The DCA1000EVM's own connector guide marks its side of pin 16 (`SYNC_IN`) and pin 14
(`NERROUT`) as `NC` -- i.e. the DCA1000 FPGA doesn't consume them. These are still real,
live signals driven by the radar; they're just unused by the DCA1000, so there's no
bus-contention risk tapping them whether or not a DCA1000 is connected.

### 20-pin low-speed connector ("J6" in the reference guide -- LaunchPad/BoosterPack-style header)

| Pin | Signal | | Pin | Signal |
|---|---|---|---|---|
| 1 | 3V3 | | 2 | 5V |
| 3 | NC | | 4 | GND |
| 5 | RS232RX (Tx from AWR device) | | 6 | ANA1 (GPADC input) |
| 7 | RS232RX (Rx into AWR device) | | 8 | ANA2 (GPADC input) |
| 9 | **SYNC_IN** | | 10 | ANA3 (GPADC input) |
| 11 | NC | | 12 | ANA4 (GPADC input) |
| 13 | SPI_CLK | | 14 | PGOOD (onboard VIO) |
| 15 | GPIO0 | | 16 | PMIC Enable |
| 17 | SCL | | 18 | **SYNC_OUT** |
| 19 | SDA | | 20 | PMIC CLK OUT |

Pins 5 and 7 both being labeled "RS232RX" is TI's own labeling in the source doc, not a
transcription error here -- worth confirming against your board if you plan to use UART
on this connector.

### Where to actually probe

- **The 60-pin HD connector is a fine-pitch Samtec QTH-030 micro connector** -- not
  practical to probe directly with a scope hook, logic-analyzer clip, or jumper wire.
  Realistic options: tap the mating ribbon cable to the DCA1000 (Samtec
  HQCD-030-02.00), use a QTH-030 breakout board, or find a test pad/via near the
  connector footprint on this board's PCB (if one was routed there).
- **The 20-pin low-speed connector uses standard header pitch** and is the practical
  place to probe with jumper wires or a scope/logic-analyzer clip. It separately breaks
  out `SYNC_IN` (pin 9) and `SYNC_OUT` (pin 18) as dedicated signals -- different nets
  from the 60-pin connector's `SYNC_IN` at pin 16, but the same chip-level `VIOIN`
  voltage domain (still ~1.8V, still needs a level shifter for a 3.3V/5V MCU).
- Regardless of which pin you tap, ground your MCU to the radar board's `GND` (e.g. pin
  18/24/30/... on the 60-pin connector, or pin 4 on the low-speed connector) before
  connecting the signal line.
