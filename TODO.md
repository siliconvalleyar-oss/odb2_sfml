# TODO

## High Priority

- [ ] Replace blocking `write()` return-value warnings (`[[nodiscard]]`)
- [ ] Fix member initialization order warnings in headers
- [ ] Add proper error handling for `readModule()` when module doesn't respond

## Medium Priority

- [ ] Implement multi-PID aggregate requests to reduce command count and STOPPED risk
- [ ] Add threaded I/O to prevent UI freeze during auto-scan and VIN read
- [ ] Add reconnection logic when Bluetooth disconnects unexpectedly
- [ ] Support for CAN FD (ISO 15765-2) / ISO 13400 (DoIP)
- [ ] Add ELM327 firmware version check on connection
- [ ] Implement bidirectional control for service modes (Oil Reset, EPB, BMS, SAS, DPF, ABS)
- [ ] Add `-h`/`--help` CLI flag and `--demo` flag
- [ ] Detect Bluetooth adapter presence before attempting connection

## Low Priority

- [ ] Add unit tests for `splitResponse()`, PID decoders, DTC parsing
- [ ] Internationalization (i18n)
- [ ] Dark/light theme toggle for Qt windows
- [ ] Package as AppImage / Flatpak / Snap
- [ ] Windows MSYS2 installer
- [ ] Mobile-responsive Qt Quick interface
- [ ] Support for Bluetooth LE (BLE) ELM327 adapters
- [ ] Add `protocol.cache` to `.gitignore`
- [ ] Live graphing for fuel trims as separate Qt chart
- [ ] Configurable PID selection GUI (currently hardcoded)

## Known Issues

- `O2ScopeWidget::tick()` may cause division by zero if timer fires before first data point
- Demo mode DTC database duplication between `obd2_app.cpp` and alternate implementation
- Terminal dashboard loop (option 7) uses configurable sample time (default 4s), which may feel slow
- Continuous options (7, 9, 10, 11, 13) rely on `g_runningLogger` atomic + SIGINT handler; on Windows this may not work
- `sendRaw()` and `send()` share ~80% identical select()/ReadFile code (code duplication)
- Service options 21-28 are stubs with informational messages only (require bidirectional scan tools)
