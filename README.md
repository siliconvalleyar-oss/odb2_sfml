# OBD-II Scanner Profesional v11 — Qt5

Escáner de diagnóstico automotriz OBD-II con **interfaz de menú interactivo por terminal** y **ventanas gráficas Qt5** (dashboard analógico, gráficos de líneas, osciloscopio O2). Conexión Bluetooth ELM327.

## Características

### Diagnóstico del vehículo
- Auto Scan: escaneo completo de módulos CAN (ECU, TCM, ABS, Airbag, BCM, etc.)
- Lectura de sensores: RPM, velocidad, temperatura motor, carga, MAF, presión, avance, combustible
- Sensores de oxígeno: voltaje O2 B1S1-B2S4 con estado pobre/rica/normal y Fuel Trims
- Códigos DTC: leer (Modo 03), borrar (Modo 04), pendientes (Modo 07), permanentes (Modo 0A)
- Freeze Frame (Modo 02) y Monitoreo OBD (Modo 06)
- Información del vehículo: VIN, MIL, protocolo

### Interfaz gráfica Qt5 (Opciones 41-44)
| Opción | Ventana | Descripción |
|--------|---------|-------------|
| 41 | Dashboard | 4 indicadores analógicos (RPM, velocidad, temperatura, carga) + barras + valores |
| 42 | Gráficos | 4 gráficos de líneas en tiempo real (RPM, carga, acelerador, MAF) |
| 43 | Osciloscopio O2 | Forma de onda del sensor O2 B1S1 con referencias pobre/rica |
| 44 | Demo Qt5 | Datos simulados sin ELM327 |

Todas las ventanas Qt5 tienen tema oscuro profesional, QPainter antialiasing, captura automática de pantalla al cerrar (PNG en `screenshots/`), y pestañas intercambiables.

### Funciones avanzadas GM
- Reset de adaptativos ECU (Modo 22 F10E/F10D/F11C)
- Kilometraje ECU (01A6 → 22 F190 fallback)
- Temperatura catalizador, presión combustible, torque motor
- Voltaje ECU, temperatura transmisión, historial errores

### Servicios especiales
- Reset de aceite (Oil Life), freno de mano eléctrico (EPB)
- Registro de batería (BMS), calibración SAS
- Filtro de partículas (DPF), reaprendizaje acelerador
- Purga ABS, codificación de inyectores

### Logging
- Logger CSV de parámetros del motor
- Log completo de sensores (todos los PIDs)
- Diagnóstico P0171 con duración configurable
- Session logging detallado (opciones, comandos, resultados)
- Timeout adaptativo (penalidad STOPPED)

## Requisitos

```bash
# Ubuntu/Debian
sudo apt install build-essential qtbase5-dev libbluetooth-dev cmake
# Opcional: Qt5 Charts para gráficos avanzados
sudo apt install libqt5charts5-dev
```

## Compilación

### CMake (recomendado)
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./elm327_app_v11
```

### Makefile directo
```bash
make clean && make
./bin/elm327_app_v11
```

### Documentación Doxygen
```bash
sudo apt install doxygen graphviz
make docs
# Abrir docs/doxygen/html/index.html
```

## Uso

```bash
./bin/elm327_app_v11
```

El programa presenta un menú interactivo numerado:
```
 1. Auto Scan (Escaneo Completo)
 2. Motor / PCM (Parámetros)
 ...
41. Dashboard Qt5 (Gráfico)
42. Gráficos Qt5 (Líneas)
43. Osciloscopio O2 Qt5
44. Demo Qt5 (Datos Simulados)
 0. Salir
Opción:
```

Seleccione 41-44 para las ventanas gráficas, o cualquier otra opción para diagnóstico por terminal. Presione Ctrl+C para salir de loops continuos (dashboard, logger).

## Estructura del proyecto

```
├── CMakeLists.txt          # Sistema de compilación CMake
├── Makefile                # Makefile tradicional alternativo
├── Doxyfile                # Configuración Doxygen
├── include/
│   ├── elm327.hpp          # Driver ELM327 (conexión, comandos AT/STD)
│   ├── gm_commands.hpp     # Comandos GM (Modo 22)
│   ├── logger.hpp          # Logger CSV + Session Logger
│   ├── obd2_app.hpp        # Aplicación principal con menú
│   └── qt_display.hpp      # Interfaz gráfica Qt5
├── src/
│   ├── main.cpp            # Entry point
│   ├── elm327.cpp          # Implementación ELM327
│   ├── gm_commands.cpp     # Implementación GM
│   ├── logger.cpp          # Implementación logging
│   ├── obd2_app.cpp        # Implementación menú
│   └── qt_display.cpp      # Implementación widgets Qt5
├── bash/
│   └── clear_rfcomm.sh     # Script liberación puerto Bluetooth
├── build/                  # Directorio de compilación CMake
├── bin/                    # Binario compilado
├── obj/                    # Objetos de compilación
├── logs/                   # Logs CSV de sesiones
└── screenshots/            # Capturas de pantalla automáticas
```

## Solución de problemas

- **No conecta**: Verifique Bluetooth encendido, MAC correcta, y ELM327 encendido
- **STOPPED frecuente**: Aumente tiempo de muestreo en Opción 40 (Configuración)
- **Permisos Linux**: Agregue usuario al grupo `bluetooth` o ejecute con `sudo`
- **Qt5 no encontrado**: `sudo apt install qtbase5-dev`
- **Datos intermitentes**: Use `bash/clear_rfcomm.sh` antes de conectar

## Licencia

Código abierto — Proyecto Freebuff.
