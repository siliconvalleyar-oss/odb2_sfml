---
name: obd2-shell
description: OBD-II Scanner Profesional v11 — Qt5 C++17 ELM327 automotive diagnostic app with terminal menu interface (44 options), QPainter-based Qt5 graphical windows (dashboard, line charts, O2 scope), GM mode 22 commands, DTC diagnostics, logging, and demo mode.
license: MIT
metadata:
  language: C++17
  framework: Qt5
  build: cmake
---

## Overview

Hybrid terminal+GUI Qt5 C++17 automotive diagnostic scanner. Connects to ELM327 Bluetooth adapters, reads OBD-II PIDs via terminal menu, and launches Qt5 graphical windows for real-time visualization. Includes GM-specific mode 22 commands, DTC management, logging, and a fully simulated demo mode.

## Build & Run

```bash
# CMake (recomended)
mkdir -p build && cd build
cmake .. && make -j$(nproc) && ./elm327_app_v11

# Makefile direct
make clean && make && ./bin/elm327_app_v11
```

## Architecture

```
QApplication (main.cpp)
 └── OBD::App (terminal menu loop)
      ├── ELM327     — Bluetooth RFCOMM + OBD-II PID I/O
      ├── GMCommands — Mode 22 (UDS) with fallback chain
      ├── Logger     — CSV file logger
      └── SessionLogger — Pipe-delimited session log

QtDisplay (QMainWindow, launched on options 41-44)
  ├── DashboardWidget (4 analog gauges + bars)
  ├── GraphWidget     (4 line charts, QPainter or QChart)
  └── O2ScopeWidget   (O2 waveform)
```

## Key Files

| File | Purpose |
|---|---|
| `include/elm327.hpp` | ELM327 interface (377 lines) |
| `src/elm327.cpp` | ELM327 implementation |
| `include/gm_commands.hpp` | GM mode 22 interface |
| `src/gm_commands.cpp` | GM commands + fallback |
| `include/obd2_app.hpp` | OBD::App menu class |
| `src/obd2_app.cpp` | Menu implementation (44 options) |
| `include/qt_display.hpp` | Qt5 display widgets |
| `src/qt_display.cpp` | Dashboard, graphs, O2 scope |
| `CMakeLists.txt` | CMake build system |
| `Makefile` | Alternative direct Makefile |

## Menu Options

- **1**: Auto Scan (full CAN module scan)
- **2**: Motor / PCM parameters
- **3-6**: Read modules (TCM, ABS, Airbag, BCM)
- **7**: Live dashboard (terminal, Ctrl+C to stop)
- **8**: All sensors
- **9**: ASCII multiparameter graph
- **10**: O2 ASCII oscilloscope
- **11**: CSV engine logger
- **12-13**: Full sensor log + P0171 diagnostic
- **14-20**: DTC operations (read, clear, pending, permanent, freeze frame, monitor, O2 test)
- **21-28**: Service stubs (Oil, EPB, BMS, SAS, DPF, throttle, ABS, injectors)
- **29-36**: GM advanced (adaptations, km, cat temp, fuel pressure, torque, voltage, trans temp, history)
- **37**: Vehicle info (VIN, MIL)
- **38**: Detect CAN modules
- **39**: Custom command
- **40**: Configuration (sample time, system info)
- **41**: Dashboard Qt5 (analog gauges)
- **42**: Graphs Qt5 (line charts, QPainter or QChart)
- **43**: O2 Scope Qt5 (waveform)
- **44**: Demo Qt5 (simulated data, no ELM327)
- **0**: Exit

## Connection Flow

```
fullCleanup() → socket() → connect() → ATZ(2000ms)
  → ATE0, ATH0, ATS0, ATL0 → flush(\r×3) → ATSP{n}
  → ATAT1, ATST10, ATAL1 → ATCF/ATCM (if proto 6-9)
  → detectAndCacheProtocol()
```

## STOPPED Recovery

```
STOPPED → penalty += 100ms (max 1000ms)
  → wake(\r) → ATD → (ATZ fallback) → flush ×5
  → re-apply AT config → retry → decay -25ms after 10 OK
```

## Coding Conventions

- camelCase methods, `m_` member prefix, PascalCase classes
- Error: return `-1`, `"No disponible"`, or empty string; `catch(...)` in parsers
- Debug log: `std::cout << "[monitor] ..."`
- Theme: `#141620` bg, `#DCE1F0` text, `#3C8CFF` accent

## Common Tasks

### Add a new menu option
1. Add title in `getOptionTitle()` array (include/obd2_app.hpp ~line 253)
2. Add entry in `printMenu()` (include/obd2_app.hpp ~line 312)
3. Add case in `processOption()` switch (include/obd2_app.hpp ~line 380)
4. Implement the handler method in include/obd2_app.hpp

### Add a new Qt display widget
1. Add class to `include/qt_display.hpp`
2. Implement QPainter `paintEvent()` in `src/qt_display.cpp`
3. Add tab to `QtDisplay` constructor
4. Wire into menu option in `obd2_app.hpp`

### Fix connectivity issue
1. Check `connectBT()` AT sequence in elm327.cpp
2. Verify `splitResponse()` handles response format
3. Check CAN filters only for protocols 6-9
4. Add delays between commands if ELM327 enters STOPPED
