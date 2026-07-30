# sensors

Per-sensor driver/integration modules for the STM32 time-sync hub (see
[`../docs/system-design.md`](../docs/system-design.md)). Each subsensor is a
self-contained unit the hub in [`../stm32-hub`](../stm32-hub) pulls in and
registers with the generic capture engine -- adding a new sensor means adding
a subfolder here plus one entry in the hub's `sensor_table[]`, not touching
the hub's core code.

Planned layout, one subfolder per sensor:

- `gps/` -- ublox timing module: UBX/NMEA parsing over UART, PPS handling.
- `radar_sync/` -- STM32-side capture of the AWR1843AOP's frame-sync pulse
  (the radar's own firmware lives in [`../radar_firmware`](../radar_firmware)
  -- this is just the hub-side capture of its output signal).
- `camera_strobe/` -- STM32-side capture of the ZED camera capture card's
  hardware strobe/trigger output.

Empty for now -- filled in as each sensor integration is worked through.
