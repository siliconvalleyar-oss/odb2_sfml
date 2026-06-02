#!/usr/bin/env bash
# ============================================================================
# install_dependencies_obd2_qt.sh
# Instala las dependencias necesarias para compilar obd2_v7 (Qt5)
# Proyecto: ELM327 OBD-II Scanner v7 — Interfaz gráfica Qt5
# ============================================================================
#
# Uso:
#   chmod +x install_dependencies_obd2_qt.sh
#   ./install_dependencies_obd2_qt.sh
#
# Dependencias que instala:
#   - build-essential (g++, make)
#   - cmake
#   - qtbase5-dev (Qt5 Widgets)
#   - libqt5charts5-dev (Qt5 Charts — opcional, gráficos QChart)
#   - libbluetooth-dev (bluez — comunicación Bluetooth con ELM327)
#   - pkg-config
#
# Soporta: Ubuntu, Debian y derivados (apt)
# ============================================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================================${NC}"
echo -e "${BLUE}  Instalando dependencias para OBD-II Scanner v7 (Qt5)${NC}"
echo -e "${BLUE}========================================================${NC}"
echo ""

# Detectar si se necesita sudo (evita problemas si ya se ejecuta como root)
if [[ $EUID -eq 0 ]]; then
    SUDO=""
else
    SUDO="sudo"
fi

if [[ $EUID -ne 0 ]]; then
    echo -e "${YELLOW}[!] Se requiere sudo para instalar paquetes.${NC}"
    echo ""
fi

# Detectar gestor de paquetes
if command -v apt &> /dev/null; then
    PKG_MANAGER="apt"
    INSTALL_CMD="apt install -y"
    echo -e "${GREEN}[✓] Detectado: apt (Debian/Ubuntu)${NC}"
elif command -v apt-get &> /dev/null; then
    PKG_MANAGER="apt-get"
    INSTALL_CMD="apt-get install -y"
    echo -e "${GREEN}[✓] Detectado: apt-get (Debian/Ubuntu)${NC}"
else
    echo -e "${RED}[✗] No se detectó un gestor de paquetes compatible.${NC}"
    echo -e "${RED}    Este script solo soporta sistemas basados en Debian/Ubuntu.${NC}"
    exit 1
fi

echo ""

# Actualizar lista de paquetes
if [[ $EUID -ne 0 ]]; then
    echo -e "${YELLOW}[*] Actualizando lista de paquetes...${NC}"
    $SUDO apt update 2>&1 | tail -3
fi

# ---- Paquetes requeridos ----
REQUIRED_PKGS=(
    "build-essential"       # g++, make
    "cmake"                 # Sistema de compilación
    "qtbase5-dev"           # Qt5 Widgets (QApplication, QPainter, etc.)
    "libbluetooth-dev"      # Bluetooth RFCOMM para ELM327
    "pkg-config"            # Para detección de librerías
)

# ---- Paquetes opcionales ----
OPTIONAL_PKGS=(
    "libqt5charts5-dev"     # Qt5 Charts (gráficos QChart avanzados)
    "doxygen"               # Generación de documentación
    "graphviz"              # Diagramas DOT para Doxygen
)

echo -e "${YELLOW}--- Paquetes requeridos ---${NC}"
for pkg in "${REQUIRED_PKGS[@]}"; do
    if dpkg -s "$pkg" &> /dev/null; then
        echo -e "  ${GREEN}[✓]${NC} $pkg — ya instalado"
    else
        echo -e "  ${YELLOW}[ ]${NC} $pkg — instalando..."
        $SUDO $INSTALL_CMD "$pkg" 2>&1 | tail -3
    fi
done

echo ""
echo -e "${YELLOW}--- Paquetes opcionales ---${NC}"
for pkg in "${OPTIONAL_PKGS[@]}"; do
    if dpkg -s "$pkg" &>/dev/null; then
        echo -e "  ${GREEN}[✓]${NC} $pkg — ya instalado"
    else
        echo -e "  ${YELLOW}[ ]${NC} $pkg — opcional, instalando..."
        $SUDO $INSTALL_CMD "$pkg" 2>&1 | tail -3 || echo -e "  ${RED}[✗]${NC} $pkg — no se pudo instalar (opcional, se omite)"
    fi
done

echo ""
echo -e "${BLUE}========================================================${NC}"
echo -e "${GREEN}  Verificación de instalación${NC}"
echo -e "${BLUE}========================================================${NC}"
echo ""

# Verificar compilador C++
if command -v g++ &> /dev/null; then
    echo -e "  ${GREEN}[✓]${NC} g++ — $(g++ --version | head -1)"
else
    echo -e "  ${RED}[✗]${NC} g++ — NO INSTALADO"
fi

# Verificar cmake
if command -v cmake &> /dev/null; then
    echo -e "  ${GREEN}[✓]${NC} cmake — $(cmake --version | head -1)"
else
    echo -e "  ${RED}[✗]${NC} cmake — NO INSTALADO"
fi

# Verificar Qt5 (moc)
if command -v moc &> /dev/null; then
    echo -e "  ${GREEN}[✓]${NC} moc (Qt5 Meta-Object Compiler) — $(moc --version 2>&1 | head -1)"
else
    echo -e "  ${YELLOW}[!]${NC} moc — NO INSTALADO (incluido en qtbase5-dev)"
fi

# Verificar bluez
if pkg-config --exists bluez 2>/dev/null; then
    echo -e "  ${GREEN}[✓]${NC} bluez — $(pkg-config --modversion bluez)"
else
    echo -e "  ${YELLOW}[!]${NC} bluez — NO DETECTADO"
fi

# Verificar Qt5 Charts
if pkg-config --exists Qt5Charts 2>/dev/null; then
    echo -e "  ${GREEN}[✓]${NC} Qt5 Charts — $(pkg-config --modversion Qt5Charts)"
else
    echo -e "  ${YELLOW}[!]${NC} Qt5 Charts — no instalado (se usará QPainter como fallback)"
fi

echo ""
echo -e "${GREEN}✅ Instalación completada.${NC}"
echo ""
echo -e "${YELLOW}Para compilar:${NC}"
echo "  cd src/obd2_v7"
echo "  mkdir -p build && cd build"
echo "  cmake .."
echo "  make -j\$(nproc)"
echo ""
echo -e "${YELLOW}O usando Makefile directo:${NC}"
echo "  cd src/obd2_v7"
echo "  make clean && make"
echo "  ./bin/elm327_app_v7"
echo ""
