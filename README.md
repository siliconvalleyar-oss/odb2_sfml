# 🚗 OBD-II Scanner Profesional v9 — Qt5

Escáner de diagnóstico automotriz OBD-II con **interfaz gráfica Qt5** y conexión Bluetooth ELM327.

> **Actualizado desde v7**: Corregido bug crítico de parseo de respuestas OBD concatenadas.
> Ahora velocidad, temperatura, carga, MAF y demás sensores se leen correctamente.
> Se agregó botón **Salir** en el dashboard Qt y escala automática de RPM.

## ✨ Características

### 📡 Diagnóstico del vehículo
- **Auto Scan**: Escaneo completo de todos los módulos CAN del vehículo
- **Lectura de sensores**: RPM, velocidad, temperatura motor, carga, MAF, presión, avance, combustible
- **Sensores de oxígeno**: Voltaje B1S1–B2S4 con estado pobre/rica/normal y Fuel Trims
- **Códigos DTC**: Leer, borrar, pendientes (Modo 07), permanentes (Modo 0A)
- **Freeze Frame**: Captura de datos en el momento del fallo (Modo 02)
- **Monitoreo OBD**: Tests internos de monitoreo (Modo 06) y prueba de O2 (Modo 08)

### 🎨 Interfaz gráfica Qt5 (Opción 41-43)
| Opción | Ventana | Descripción |
|--------|---------|-------------|
| **41** | Dashboard | 4 indicadores analógicos (RPM, velocidad, temperatura, carga) + barras + valores digitales |
| **42** | Gráficos | 4 gráficos de líneas en tiempo real (RPM, carga, acelerador, MAF) |
| **43** | Osciloscopio O2 | Forma de onda del sensor O2 B1S1 con referencias pobre/rica/normal |

Todas las ventanas Qt5 tienen:
- Tema oscuro profesional
- QPainter antialiasing
- Captura automática de pantalla al cerrar (PNG en `screenshots/`)
- Pestañas intercambiables sin recargar datos

### 🛠️ Servicios especiales
- Reset de aceite (Oil Life)
- Freno de mano eléctrico (EPB)
- Registro de batería (BMS)
- Calibración SAS / DPF / ABS
- Reaprendizaje de acelerador
- Codificación de inyectores

### ⚙️ Funciones avanzadas GM
- Reset de adaptativos ECU
- Kilometraje, temp. catalizador, presión combustible
- Torque motor, voltaje ECU, temp. transmisión
- Historial de errores GM vía Modo 22

### 📊 Logging
- Logger CSV de parámetros del motor
- Log completo de sensores (todos los PIDs)
- Diagnóstico P0171 con duración configurable
- Session logging detallado (opciones, comandos, resultados)
- Timeout adaptativo (penalidad STOPPED)

## 📦 Requisitos

```bash
# Ubuntu/Debian
sudo apt install qtbase5-dev libbluetooth-dev cmake build-essential
```

| Paquete | Versión mínima | Propósito |
|---------|---------------|-----------|
| Qt5 Widgets | 5.9 | Interfaz gráfica QPainter |
| libbluetooth-dev | 5.x | Conexión Bluetooth ELM327 |
| cmake | 3.14 | Sistema de compilación |
| g++ (o clang++) | C++17 | Compilador |

## 🔧 Compilación

```bash
# Desde la raíz del proyecto
cd src/obd2_v7
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Ejecutar
./elm327_app_v7
```

### Compilación rápida (make tradicional)

También disponible un Makefile directo:

```bash
cd src/obd2_v7
make clean && make
./bin/elm327_app_v7
```

## 🚀 Uso

```bash
# 1. Conectar el adaptador ELM327 por Bluetooth
# 2. Ejecutar el programa
./bin/elm327_app_v7

# 3. En el menú, seleccionar opción:
#    41 - Dashboard Qt5 (indicadores analógicos)
#    42 - Gráficos Qt5 (líneas en tiempo real)
#    43 - Osciloscopio O2 Qt5 (onda de sensor)
#    ... o cualquier otra función de diagnóstico

# 4. Las capturas de pantalla se guardan en:
#    screenshots/qt_YYYYMMDD_HHMMSS.png

# 5. Presionar ESC o cerrar la ventana para volver al menú
```

### Ejemplo de sesión

```
================== ESCANER PROFESIONAL OBD2 ==================

--- SISTEMAS DEL VEHICULO ---
  1. Auto Scan (Escaneo Completo)
  2. Motor / PCM (Parametros)
  ...

--- INTERFAZ GRAFICA QT5 ---
 41. Dashboard Qt5 (Grafico)
 42. Graficos Qt5 (Lineas)
 43. Osciloscopio O2 Qt5

  0. Salir
Opcion: 41
```

## 📐 Documentación Doxygen

```bash
sudo apt install doxygen graphviz
make docs
# Abrir docs/doxygen/html/index.html en el navegador
```

La documentación incluye diagramas DOT del flujo de conexión Bluetooth, recuperación STOPPED, y menú principal.

## 🏗️ Estructura del proyecto

```
obd2_v7/
├── CMakeLists.txt          # Sistema de compilación CMake + Qt5
├── Makefile                # Makefile tradicional alternativo
├── Doxyfile                # Configuración Doxygen
├── README.md               # Este archivo
├── include/
│   ├── elm327.hpp          # Driver ELM327 (conexión, comandos AT/STD)
│   ├── gm_commands.hpp     # Comandos específicos GM (Modo 22)
│   ├── logger.hpp          # Logger CSV + Session Logger
│   ├── obd2_app.hpp        # Aplicación principal con menú
│   └── qt_display.hpp      # Interfaz gráfica Qt5 (Dashboard, Gráficos, O2 Scope)
├── src/
│   ├── main.cpp            # Entry point (QApplication init)
│   ├── elm327.cpp          # Implementación del driver ELM327
│   ├── gm_commands.cpp     # Implementación de comandos GM
│   ├── logger.cpp          # Implementación de logging
│   ├── obd2_app.cpp        # Stub de compatibilidad
│   └── qt_display.cpp      # Widgets Qt5 con QPainter
├── build/                  # Directorio de compilación
├── logs/                   # Logs CSV de sesiones
└── screenshots/            # Capturas de pantalla automáticas
```

## 📊 Comparativa con obd2_v6 (SFML)

| Característica | obd2_v6 (SFML) | obd2_v7 (Qt5) |
|---------------|----------------|----------------|
| Biblioteca gráfica | SFML 2.6 | Qt5 Widgets |
| Dashboard | 4 gauges (SFML RenderWindow) | 4 gauges (QPainter antialiasing) |
| Gráficos | Ventana 960×720 | Pestañas intercambiables (QTabWidget) |
| O2 Scope | Ventana 800×500 | Osciloscopio embebido en pestañas |
| Screenshots | `capture()` (deprecado) | `grab()` Qt nativo |
| Estilo | Tema oscuro manual | Tema oscuro + QSS stylesheet |
| Compilación | Makefile + pkg-config | CMake + find_package(Qt5) |

## 📝 Licencia

Código abierto — Proyecto Freebuff.
