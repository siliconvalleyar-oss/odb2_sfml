#!/bin/bash

# ==========================================
#   BLUETOOTH / RFCOMM FIX TOOL
# ==========================================

MAC="00:1D:A5:07:23:6E"
DEV="/dev/rfcomm0"

require_root() {
    if [ "$EUID" -ne 0 ]; then
        echo "[ERROR] Ejecutar con sudo"
        exit 1
    fi
}

pause() {
    read -p "Presione ENTER para continuar..."
}

status() {
    echo "========== STATUS =========="
    echo "[INFO] rfcomm:"
    rfcomm
    echo ""

    echo "[INFO] Procesos rfcomm:"
    ps aux | grep rfcomm | grep -v grep
    echo ""

    echo "[INFO] Uso del dispositivo:"
    lsof | grep rfcomm
    fuser -v $DEV 2>/dev/null
    echo ""
}

kill_processes() {
    echo "[INFO] Matando procesos..."
    killall -9 rfcomm 2>/dev/null
    killall -9 bluetoothd 2>/dev/null
    sleep 1
    echo "[OK] Procesos eliminados"
}

release_rfcomm() {
    echo "[INFO] Liberando rfcomm..."
    rfcomm release all 2>/dev/null
}

restart_bluetooth() {
    echo "[INFO] Reiniciando Bluetooth..."
    systemctl stop bluetooth
    rfkill block bluetooth
    sleep 1
    rfkill unblock bluetooth
    systemctl start bluetooth
    echo "[OK] Bluetooth reiniciado"
}

reload_module() {
    echo "[INFO] Recargando módulo rfcomm..."
    modprobe -r rfcomm 2>/dev/null
    sleep 1
    modprobe rfcomm
    echo "[OK] Módulo recargado"
}

full_cleanup() {
    echo "========== LIMPIEZA PROFUNDA =========="

    kill_processes
    release_rfcomm
    restart_bluetooth

    echo "[INFO] Intentando liberar módulo..."
    modprobe -r rfcomm 2>/dev/null

    if lsmod | grep -q rfcomm; then
        echo "[WARNING] rfcomm sigue en uso"
        echo "[TIP] Puede requerir reboot"
    else
        echo "[OK] rfcomm liberado"
    fi

    modprobe rfcomm
    echo "[OK] Sistema limpio"
}

connect_device() {
    echo "[INFO] Conectando a $MAC..."

    rfcomm release all 2>/dev/null

    echo "[INFO] Ejecutando conexión..."
    rfcomm connect 0 $MAC 1
}

# ================= MENU =================
require_root

while true; do
    clear
    echo "========================================"
    echo "   BLUETOOTH RFCOMM FIX TOOL"
    echo "========================================"
    echo "1) Ver estado"
    echo "2) Matar procesos rfcomm/bluetooth"
    echo "3) Liberar rfcomm"
    echo "4) Reiniciar Bluetooth"
    echo "5) Recargar módulo rfcomm"
    echo "6) LIMPIEZA PROFUNDA (recomendada)"
    echo "7) Conectar ELM327"
    echo "8) Salir"
    echo "========================================"
    read -p "Opcion: " op

    case $op in
        1)
            status
            pause
            ;;
        2)
            kill_processes
            pause
            ;;
        3)
            release_rfcomm
            pause
            ;;
        4)
            restart_bluetooth
            pause
            ;;
        5)
            reload_module
            pause
            ;;
        6)
            full_cleanup
            pause
            ;;
        7)
            connect_device
            ;;
        8)
            echo "Saliendo..."
            exit 0
            ;;
        *)
            echo "Opcion invalida"
            sleep 1
            ;;
    esac
done
