# timeSync_firmware

Custom TI mmWave MSS/DSS firmware for an **AWR1843AOP**-based board, forked from TI's
mmWave SDK demo to add a hardware frame-sync pulse (see
[`xwr18xx-custom/mss/mss_main.c`](xwr18xx-custom/mss/mss_main.c)) for external
timestamp correlation with an MCU. Derived from the TI mmWave demo firmware (BSD-3);
modifications licensed under similar terms (MIT) -- see `LICENSE` and
`LICENSE-MODIFICATIONS`. See the [main `xwr` documentation](https://radarml.github.io/xwr/firmware)
for background on the upstream project this was forked from.

## Repository Layout

- `xwr18xx-custom/` -- the actively developed firmware (frame-sync GPIO additions,
  CLI/platform tweaks). This is what you build and flash for this project.
- `xwr18xx/` -- baseline demo, kept for reference/diffing against upstream.
- `debug_console.py` -- reconnecting serial console for watching MSS boot/CLI output.
  The AWR1843AOP's CP2105 USB-UART bridge drops off the bus on a full power cycle, so
  plain terminals (`screen`/`picocom`) die when the device node disappears; this script
  polls and reconnects automatically.
- `victim.yaml` -- radar serial port and chirp config used by `debug_console.py` /
  `sync.py`. Its own header comment says to copy it from a `victim.example.yaml`
  template and treats it as gitignored/machine-specific, but neither is actually true
  right now: there is no `victim.example.yaml` in the repo, and `victim.yaml` itself is
  tracked in git (not listed in `.gitignore`). Until that's cleaned up, edit it in place
  and be aware the current committed values (including the machine-specific serial
  port path) are shared, not local-only.
- `sync.py` -- host-side capture/sync script.
- `release.sh` -- packages built `.bin` files into `release.tar`.
  **Known gap:** it currently copies from `xwr18xx/`, but building `xwr18xx-custom`
  produces its `.bin` files directly inside `xwr18xx-custom/` (see Build Output below).
  Update `release.sh`'s paths before relying on it for a `-custom` release.

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
- Python 3 + `uv` for the host-side tooling (`debug_console.py`, `sync.py`):
  ```bash
  uv run python debug_console.py --config victim.yaml
  ```

## Downloading / Environment Setup

1. Install the mmWave SDK from TI (`ti.com/tool/mmwave-sdk`), matching the 3.6.x line
   this firmware targets.
2. Set up the SDK build environment (run once per shell session):
   - **Windows:** `%MMWAVE_SDK_INSTALL_PATH%\packages\scripts\windows\setenv.bat`
   - **Linux:** `source $MMWAVE_SDK_INSTALL_PATH/packages/scripts/unix/setenv.sh`

   This must be run from within that `scripts/...` directory, and should print
   `mmWave Build Environment Configured` on success.
3. Clone/pull this repo (`git@github.com:cbill-cmu/timeSync_firmware.git`) alongside
   (not inside) the SDK install.

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
   shows as "XDS110 Class Application/User UART"; on this AOP board (CP2105 bridge)
   it shows as the CP2105's **Standard (`if00`)** port -- see
   `/dev/serial/by-id/...if00-port0` on Linux, or Device Manager on Windows.
3. Open UniFlash, select the device (AWR1843AOP), point it at that COM port, and load
   `xwr18xx_mmw_aop_demo.bin` as the image. Follow the "UniFlash User Guide for mmWave
   Devices" for the rest of the GUI flow.
4. **Switch back to functional mode** (SOP2=0, SOP0=1) and power-cycle the board to
   boot the newly flashed image.

## Connecting / Running

- CLI/config port: 115200 baud.
- On this board, use the CP2105's **Enhanced (`if00`)** control port for the CLI --
  that's the port `victim.yaml` should reference. `ls -l /dev/serial/by-id/` to find it.
- Because the CP2105 loses power (and its `/dev` node) on a full board power cycle,
  use `debug_console.py` instead of a plain terminal to keep logging across resets:
  ```bash
  uv run python debug_console.py --config victim.yaml   # edit the port field first
  ```
- For radar chirp/frame config and capture parameters, see `victim.yaml`.
