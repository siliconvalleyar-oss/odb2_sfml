# Changelog

## v11.1 (2026-06-18)

### Added
- Full documentation suite: ARCHITECTURE.md, CHANGELOG.md, TODO.md, INSTALL.md, CODE_OF_CONDUCT.md
- Updated README.md with comprehensive features, build, and usage documentation
- opencode skill definition for AI-assisted development

## v11.0

### Added
- Option 44: Demo Qt5 with simulated data (no ELM327 needed)
- QChart-based graphs when Qt5 Charts is installed (`USE_QT5_CHARTS`)
- CAN filter/mask conditional on protocol (ATCF/ATCM only for protocols 6-9)
- ATH0 (Headers OFF) added to all connection and recovery paths
- Buffer flush (`\r` ×3) after ATZ in connectBT()
- `detectAndCacheProtocol()` with protocol.cache persistence
- `logP0171Diagnostic()` method for lean mixture diagnosis
- DTC read for individual modules (TCM, ABS, Airbag, BCM)

### Fixed
- ELM327 STOPPED state recovery with adaptive penalty and full reconfiguration
- Increased ATZ timeout 1000ms → 2000ms
- Dashboard delays increased to prevent STOPPED state
- Demo mode no longer auto-starts; must be explicitly selected (option 44)
- Regex escape warning in history panel code
- Removed dead `readRaw()` function and other dead code

### Removed
- `sources/elm327.cpp.bak` backup file
- Dead `readRaw()` method
- Dead `lookupDTC()` function
- Empty/unreachable conditional blocks

## v9.0

### Added
- Qt5 graphical interface with 3 display modes (Dashboard, Graphs, O2 Scope)
- Dark Fusion theme with QPainter antialiasing
- Auto-screenshot on window close
- GM mode 22 commands with NRC decode
- CAN module scan (8 modules: 7E0-7E7)
- CSV logging for all sensors
- Session logger with pipe-delimited format

### Fixed
- Bug crítico de parseo de respuestas OBD concatenadas
- RPM scale auto-adjustment
- Added Salir button in dashboard
- Timeout adaptativo mejorado

## v7.0

### Added
- Initial Qt5 port from SFML
- QTabWidget with 3 panels (Dashboard, Graphs, O2 Scope)
- Terminal menu interface (options 1-40)
- ELM327 Bluetooth connection with AT command negotiation
- Basic OBD-II PID reading

## v1.0

### Added
- Initial project with SFML graphical library
- OBD2 ELM327 library
- Basic scanner functionality
