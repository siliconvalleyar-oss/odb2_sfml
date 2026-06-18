# Installation

## Requirements

### Linux (Debian/Ubuntu)
```bash
sudo apt install build-essential qtbase5-dev libbluetooth-dev cmake
# Opcional: Qt5 Charts para gráficos QChart avanzados
sudo apt install libqt5charts5-dev
# Opcional: Doxygen para documentación
sudo apt install doxygen graphviz
```

### Linux (Fedora/RHEL)
```bash
sudo dnf install gcc-c++ qt5-qtbase-devel bluez-libs-devel cmake
# Opcional
sudo dnf install qt5-qtcharts-devel doxygen graphviz
```

### Windows (MSYS2)
```bash
pacman -S mingw-w64-x86_64-qt5 mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc
# Opcional
pacman -S mingw-w64-x86_64-qt5-charts
```

## Build

### CMake (recomendado)
```bash
git clone <repo-url> obd2_shell
cd obd2_shell
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./elm327_app_v11
```

### Makefile directo (alternativa rápida)
```bash
cd obd2_shell
make clean && make -j$(nproc)
./bin/elm327_app_v11
```

## Run

```bash
./bin/elm327_app_v11
```

## Permissions (Linux Bluetooth)

Agregue su usuario al grupo `bluetooth`:
```bash
sudo usermod -aG bluetooth $USER
# Cerrar sesión y volver a entrar, o ejecutar:
newgrp bluetooth
```

## Documentación Doxygen

```bash
cd obd2_shell
make docs
# Luego abrir en navegador:
# docs/doxygen/html/index.html
```

## Primera conexión

1. Empareje el ELM327 vía Bluetooth del sistema
2. Anote la dirección MAC (ej: `00:1D:A5:07:23:6E`)
3. Ejecute `./bin/elm327_app_v11`
4. Confirme la MAC cuando se solicite
5. Use el menú: **Opción 41** para Dashboard Qt5, **Opción 44** para Demo

## Modo Demo (sin hardware)

Seleccione **Opción 44** en el menú principal para simular sensores y fallas sin ELM327.

## Troubleshooting

| Problema | Solución |
|---|---|
| `socket: Operation not permitted` | Agregar usuario al grupo `bluetooth` |
| `rfcomm: Cannot open device` | Ejecutar `sudo rfcomm release all` o `bash/clear_rfcomm.sh` |
| `connect: Connection refused` | Verificar ELM327 encendido y emparejado |
| `STOPPED` frecuente | Aumentar tiempo de muestreo en Opción 40 |
| `Qt5 not found` al compilar | Instalar `qtbase5-dev` |
| `fatal error: bluetooth/bluetooth.h` | Instalar `libbluetooth-dev` |
| Ventana Qt5 no aparece | Verificar `$DISPLAY` o usar `-platform wayland` |
