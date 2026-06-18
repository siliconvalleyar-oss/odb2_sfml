# Architecture

## Overview

OBD2_shell is a **hybrid terminal+GUI** Qt5 C++17 automotive diagnostic application.  
It combines a terminal menu interface (`OBD::App`) with self-contained Qt5 graphical windows (`QtDisplay`).

```
┌─────────────────────────────────────────────────────────────────┐
│                        main.cpp                                 │
│  QApplication + OBD::App::run()                                 │
├─────────────────────────────────────────────────────────────────┤
│                      OBD::App (menu loop)                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐│
│  │ ELM327   │  │GMCommands│  │ Logger   │  │ SessionLogger    ││
│  │ (core    │  │(mode 22  │  │ (CSV)    │  │ (pipe-delimited  ││
│  │  serial) │  │ fallback)│  │          │  │  session log)    ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘│
│                                                                 │
│  processOption(1..40) → terminal output                         │
│  processOption(41..44) → QtDisplay window                      │
└─────────────────────────────────────────────────────────────────┘
                          │
                          ▼ (on option 41-44)
┌─────────────────────────────────────────────────────────────────┐
│                    QtDisplay (QMainWindow)                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Dashboard    │  │ GraphWidget  │  │ O2ScopeWidget        │  │
│  │ Widget       │  │ (QPainter)   │  │ (Osciloscopio O2)   │  │
│  │ (4 gauges    │  │ 4 line charts│  │ waveform display    │  │
│  │  + bars)     │  │ or QChart    │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│  QTimer-driven (500ms-1s tick)                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Class Hierarchy

```
QApplication (main.cpp)
 └── OBD::App (no QObject, standalone class)
      ├── owns ELM327        (std::unique_ptr)
      ├── owns GMCommands    (std::unique_ptr)
      ├── owns Logger        (std::unique_ptr)
      ├── owns SessionLogger (std::unique_ptr)
      ├── run() → connect BT → menu loop → processOption()
      └── QApplication::processEvents() for Qt windows

QtDisplay (QMainWindow, launched per-option)
  ├── DashboardWidget (QWidget, QPainter gauges)
  ├── GraphWidget     (QWidget, QPainter line charts)
  │   └── ChartsGraphWidget (if USE_QT5_CHARTS defined, QChart-based)
  └── O2ScopeWidget   (QWidget, QPainter O2 waveform)
```

## Module Responsibilities

### `ELM327`
- Bluetooth RFCOMM socket (Linux) / COM port (Windows)
- AT command negotiation + OBD-II PID request/response cycle
- Adaptive timeout + STOPPED recovery
- Protocol caching (`protocol.cache`)
- All PID getters (RPM, speed, temp, load, MAF, fuel, O2, DTCs, VIN, etc.)

### `GMCommands`
- Mode 22 (UDS) commands with CAN header 7E0/7E8
- Fallback chain: OBD standard PID → Mode 22 PID
- NRC decode per ISO 14229-1

### `OBD::App`
- Console menu (44 options)
- Terminal-based live dashboard, sensor readouts, ASCII graphs
- Launches Qt windows for graphical modes
- CSV + session logging

### `QtDisplay` / `DashboardWidget` / `GraphWidget` / `O2ScopeWidget`
- Self-contained QMainWindow with 3 tabs
- QPainter-based analog gauges, line graphs, O2 waveform
- Optional QChart backend if Qt5 Charts is installed
- Demo mode: generates simulated data without ELM327
- Auto-screenshot on close

## Menu Flow

```
run()
  ├── clearScreen() + printHeader()
  ├── Logger::open() + SessionLogger::open()
  ├── ELM327::connectBT()
  ├── GMCommands(elm)
  ├── signal(SIGINT, handler)
  │
  └── while (running)
        ├── printMenu()
        ├── cin >> option
        └── processOption(option)
              ├── 1:  showAutoScan()           → ELM327::autoScan()
              ├── 2:  showEngineParams()        → getRPM, getSpeed, etc.
              ├── 3-6: readModule()              → AT SH + getDTCs
              ├── 7:  liveDashboard()            → getDashboardFast() loop
              ├── 8:  showAllSensors()           → all PID getters
              ├── 9:  showGraphMode()            → ASCII bar chart loop
              ├── 10: oxygenSensorScope()        → O2 ASCII scope loop
              ├── 11: engineLogger()             → CSV file logger loop
              ├── 12: fullSensorsLog()           → logAllSensorsRaw()
              ├── 13: p0171DiagnosticLog()       → logP0171Diagnostic()
              ├── 14-20: DTC operations          → getDTCs, clearDTCs, etc.
              ├── 21-28: service stubs           → info messages
              ├── 29-36: GM functions            → GMCommands methods
              ├── 37: showVehicleInfo()          → getVIN, checkMIL
              ├── 38: detectModules()            → manual CAN scan
              ├── 39: sendCustomCommand()        → raw send
              ├── 40: showConfigurationMenu()    → sample time, system info
              ├── 41-43: QtDisplay window        → GUI event loop
              ├── 44: QtDisplay demo mode        → simulated data
              └── 0:  return false → exit
```

## Data Flow (Live Dashboard)

```
OBD::App::liveDashboard() loop
  └── while (g_runningLogger)
        ├── ELM327::getDashboardFast()
        │     ├── send("010C", 300ms) → getRPM
        │     ├── send("010D", 300ms) → getSpeed
        │     ├── send("0105", 300ms) → getCoolantTemp
        │     ├── send("0104", 300ms) → getEngineLoad
        │     ├── send("0111", 300ms) → getThrottlePosition
        │     ├── send("010B", 300ms) → getIntakePressure
        │     ├── send("010F", 300ms) → getIntakeTemp
        │     ├── send("010E", 300ms) → getTimingAdvance
        │     ├── send("0110", 300ms) → getMAF
        │     └── send("012F", 300ms) → getFuelLevel
        ├── print to terminal
        └── sleep(m_sampleTimeSec)
```

## Threading

Single-threaded with manual event processing.  
The menu loop calls `QApplication::processEvents()` during Qt window display (options 41-44).  
All ELM327 I/O is synchronous (blocking `send()` with `select()` timeout).  
Continuous operations (dashboard, logger) use `g_runningLogger` atomic flag + signal handler for Ctrl+C.

## Connection Flow

```
fullCleanup() → socket() → connect() → ATZ(2000ms)
  → ATE0, ATL0, ATS0, ATH0 → flush (\r ×3)
  → ATSP{n} or ATSP0 → ATAT1, ATST10, ATAL1
  → ATCF/ATCM (if protocol 6-9)
  → detectAndCacheProtocol()
```

## Key Design Decisions

- **Hybrid terminal+GUI**: Terminal menu for scriptable/remote use; Qt5 for visual analysis
- **Self-contained Qt windows**: `QtDisplay` is a standalone QMainWindow, not embedded in the app
- **QPainter fallback**: If Qt5 Charts is absent, line graphs use QPainter (no dependency)
- **Protocol caching**: `protocol.cache` file avoids ATSP0 re-detection on reconnect
- **Demo mode**: Fully simulated sensor data via `generateDemoData()` and `generateDemoO2()`
- **Service stubs**: Options 21-28 are informational (many advanced services require bidirectional scan tools)
- **Sample time configurable**: Via option 40 (1-30 seconds, default 4s)
