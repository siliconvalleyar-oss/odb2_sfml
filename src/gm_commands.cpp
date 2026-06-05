/**
 * @file gm_commands.cpp
 * @brief Implementación de comandos específicos para Chevrolet/GM.
 *
 * Combina PIDs OBD-II estándar (Modo 01) con PIDs modo 22 (UDS)
 * para acceder a parámetros del motor. Cada método implementa
 * fallback automático: intenta primero el PID estándar y luego
 * el equivalente en modo 22.
 *
 * Modo 22 usa cabezales CAN 7E0/7E8 y parsea códigos de error
 * NRC (Negative Response Code) según ISO 14229-1.
 */

#include "gm_commands.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

#ifndef Q_OS_WIN
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#else
#include <windows.h>
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

GMCommands::GMCommands(ELM327* elm327) : elm(elm327) {
}

GMCommands::~GMCommands() {
    // Nota: 7E0/7E8 son headers estándar OBD-II, no requieren restauración
}

bool GMCommands::setupGMHeader() {
    // Configurar cabezales CAN para comunicación con ECU motor
    // AT SH 7E0 = enviar como ID 0x7E0 (solicitud ECU)
    // AT CRA 7E8 = recibir respuesta de ID 0x7E8 (respuesta ECU)
    // Estos son los valores estándar OBD-II y no necesitan save/restore
    elm->send("AT SH 7E0", 50);
    elm->send("AT CRA 7E8", 50);
#ifndef Q_OS_WIN
    usleep(50000);
#else
    Sleep(50);
#endif
    return true;
}

void GMCommands::showError(const std::string& cmd, const std::string& response) {
    std::cerr << "[ERROR] Comando " << cmd << " falló: " << response << std::endl;
}

// ============================================================================
// Modo 22 — Envío con cabezal CAN 7E0/7E8 y parseo de respuesta
// ============================================================================

std::string GMCommands::sendMode22WithHeader(const std::string& pidHex) {
    if (!elm->isConnected()) return "";
    
    setupGMHeader();
    
    std::string cmd = "22 " + pidHex;
    std::cout << "[TX 22] " << cmd << std::endl;
    
    // Enviar el comando y leer respuesta
    std::string full = cmd + "\r";
#ifdef Q_OS_WIN
    DWORD written;
    WriteFile(elm->getHandle(), full.c_str(), (DWORD)full.size(), &written, NULL);

    char buffer[1024];
    std::string response;

    COMMTIMEOUTS oldCt, newCt;
    GetCommTimeouts(elm->getHandle(), &oldCt);
    newCt = oldCt;
    newCt.ReadIntervalTimeout = 50;
    newCt.ReadTotalTimeoutMultiplier = 10;
    newCt.ReadTotalTimeoutConstant = 600;
    SetCommTimeouts(elm->getHandle(), &newCt);

    while (true) {
        DWORD read;
        if (!ReadFile(elm->getHandle(), buffer, sizeof(buffer) - 1, &read, NULL)) break;
        if (read == 0) break;
        buffer[read] = '\0';
        response += buffer;
        if (response.find(">") != std::string::npos) break;
    }
    SetCommTimeouts(elm->getHandle(), &oldCt);
#else
    ::write(elm->getSock(), full.c_str(), full.size());
    
    char buffer[1024];
    std::string response;
    fd_set fds;
    struct timeval tv;
    
    FD_ZERO(&fds);
    FD_SET(elm->getSock(), &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 600000;
    
    while (select(elm->getSock() + 1, &fds, NULL, NULL, &tv) > 0) {
        int n = ::recv(elm->getSock(), buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        response += buffer;
        if (response.find(">") != std::string::npos) break;
        FD_ZERO(&fds);
        FD_SET(elm->getSock(), &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 300000;
    }
#endif
    
    // Limpiar respuesta
    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    
    std::string logResp = response.substr(0, 80);
    std::cout << "[RX 22] " << logResp << std::endl;
    
    // Verificar error UDS (Negative Response Code)
    // Buscar 7F + servicio 22 (modo 22). Fallback a 7F genérico.
    // sendMode22WithHeader() solo envía servicio 22, por eso se busca 7F22 primero.
    size_t pos7F = response.find("7F22");  // servicio 22 específico
    if (pos7F == std::string::npos) {
        pos7F = response.find("7F");       // fallback: cualquier 7F
    }
    
    if (pos7F != std::string::npos && pos7F + 6 <= response.length()) {
        std::string nrc = response.substr(pos7F + 4, 2);
        
        // Mensajes descriptivos para cada NRC UDS (ISO 14229-1)
        std::string errorMsg;
        if (nrc == "10")      errorMsg = "Rechazo general (GeneralReject)";
        else if (nrc == "11") errorMsg = "Servicio no soportado por esta ECU (ServiceNotSupported)";
        else if (nrc == "12") errorMsg = "Subfunción no soportada (SubFunctionNotSupported)";
        else if (nrc == "13") errorMsg = "Tamaño de mensaje incorrecto (IncorrectMessageLength)";
        else if (nrc == "14") errorMsg = "Respuesta demasiado larga (ResponseTooLong)";
        else if (nrc == "21") errorMsg = "Secuencia de bus incorrecta (BusyRepeat)";
        else if (nrc == "22") errorMsg = "Condiciones no correctas - ¿Motor apagado? (ConditionsNotCorrect)";
        else if (nrc == "24") errorMsg = "Solicitud fuera de secuencia (RequestSequenceError)";
        else if (nrc == "25") errorMsg = "Sin respuesta del subnodo (NoResponseFromSubnet)";
        else if (nrc == "26") errorMsg = "Fallo de integridad / Checksum (FailurePreventsExecution)";
        else if (nrc == "31") errorMsg = "PID/Request fuera de rango (RequestOutOfRange)";
        else if (nrc == "33") errorMsg = "Acceso denegado - Seguridad activa (SecurityAccessDenied)";
        else if (nrc == "35") errorMsg = "Clave inválida (InvalidKey)";
        else if (nrc == "36") errorMsg = "Intentos de acceso excedidos (ExceededNumberOfAttempts)";
        else if (nrc == "37") errorMsg = "Tiempo de espera no transcurrido (RequiredTimeDelayNotExpired)";
        else if (nrc == "70") errorMsg = "Fallo de ejecución (UploadDownloadNotAccepted)";
        else if (nrc == "71") errorMsg = "Transferencia suspendida (TransferDataSuspended)";
        else if (nrc == "72") errorMsg = "Firma/programación inválida (GeneralProgrammingFailure)";
        else if (nrc == "78") errorMsg = "Respuesta pendiente / Solicitud recibida (RequestCorrectlyReceived)";
        else if (!nrc.empty())
                              errorMsg = "Código NRC desconocido: 0x" + nrc;
        else
                              errorMsg = "Error UDS: " + response.substr(0, 20);
        
        std::cout << "   ⚠️  " << errorMsg << " (PID " << pidHex << ")" << std::endl;
        return "";  // ← importante: retornar vacío para compatibilidad con callers
    }
    
    // Extraer datos de respuesta
    size_t pos = response.find("62" + pidHex);
    if (pos != std::string::npos) {
        return response.substr(pos + 2 + pidHex.length());
    }
    
    // Si no encontramos el prefijo exacto, buscar "62" genérico
    pos = response.find("62");
    if (pos != std::string::npos) {
        return response.substr(pos + 2);
    }
    
    return response;
}

// ================= MODO 22 (público, sin cabezal) =================
std::string GMCommands::sendMode22(const std::string& pidHex) {
    return sendMode22WithHeader(pidHex);
}

// ============================================================================
// Decodificadores de respuestas modo 22
// ============================================================================

std::string GMCommands::decodeKilometers(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        uint32_t km = 0;
        for (size_t i = 0; i < hexResponse.length() && i < 8; i += 2) {
            if (i + 2 <= hexResponse.length()) {
                km = (km << 8) | std::stoi(hexResponse.substr(i, 2), nullptr, 16);
            }
        }
        std::stringstream ss;
        if (km > 1000000) {
            ss << (km / 1000.0) << " km (aprox)";
        } else {
            ss << km << " km";
        }
        return ss.str();
    } catch (const std::exception& e) {
        return "Error decodificando";
    }
}

std::string GMCommands::decodeCatalystTemp(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        int raw = std::stoi(hexResponse.substr(0, 4), nullptr, 16);
        double temp = raw / 10.0 - 40.0;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << temp << " °C";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

std::string GMCommands::decodeFuelPressure(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        int pressure = std::stoi(hexResponse.substr(0, 4), nullptr, 16);
        std::stringstream ss;
        ss << pressure << " kPa";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

std::string GMCommands::decodeEngineTorque(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        int torque = std::stoi(hexResponse.substr(0, 4), nullptr, 16);
        std::stringstream ss;
        ss << torque << " Nm";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

std::string GMCommands::decodeECUVoltage(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        int voltage = std::stoi(hexResponse.substr(0, 4), nullptr, 16);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << (voltage / 100.0) << " V";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

std::string GMCommands::decodeTransmissionTemp(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        int a = std::stoi(hexResponse.substr(0, 2), nullptr, 16);
        int b = std::stoi(hexResponse.substr(2, 2), nullptr, 16);
        double temp = ((a * 256.0) + b) / 10.0 - 40.0;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << temp << " °C";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

std::string GMCommands::decodeOilPressure(const std::string& hexResponse) {
    if (hexResponse.length() < 4) return "Datos insuficientes";
    try {
        int a = std::stoi(hexResponse.substr(0, 2), nullptr, 16);
        int b = std::stoi(hexResponse.substr(2, 2), nullptr, 16);
        double pressure = a;
        if (a == 0 && b > 0) pressure = (a * 256.0 + b);
        double psi = pressure * 0.145038;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << pressure << " kPa (" << psi << " psi)";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

std::string GMCommands::decodeKnockRetard(const std::string& hexResponse) {
    if (hexResponse.length() < 2) return "Datos insuficientes";
    try {
        int a = std::stoi(hexResponse.substr(0, 2), nullptr, 16);
        double retard = a * 0.35;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << retard << "°";
        return ss.str();
    } catch (...) { return "Error decodificando"; }
}

// ============================================================================
// Métodos principales — con fallback automático
// ============================================================================

// ================= Kilometraje =================
std::string GMCommands::getKilometers() {
    std::cout << "► Leyendo odómetro..." << std::endl;
    
    // Método 1: 01A6 (OBD estándar, si soportado)
    int km = elm->getOdometer();
    if (km > 0) {
        std::stringstream ss;
        ss << km << " km";
        return ss.str();
    }
    
    // Método 2: 22 F190 (modo 22 GM)
    std::cout << "   Intentando modo 22 F190..." << std::endl;
    std::string resp = sendMode22WithHeader("F190");
    if (!resp.empty()) {
        return decodeKilometers(resp) + " (modo 22)";
    }
    
    return "❌ No disponible - Los modelos GM con CAN no exponen odómetro vía OBD-II genérico";
}

// ================= Temperatura catalizador =================
std::string GMCommands::getCatalystTemp() {
    std::cout << "► Leyendo temperatura catalizador..." << std::endl;
    
    // Método 1: 013C (OBD estándar)
    int temp = elm->getCatalystTempB1S1();
    if (temp >= 0) {
        std::stringstream ss;
        ss << temp << " °C";
        return ss.str();
    }
    
    // Método 2: 22 F40C (modo 22 GM)
    std::cout << "   Intentando modo 22 F40C..." << std::endl;
    std::string resp = sendMode22WithHeader("F40C");
    if (!resp.empty()) {
        return decodeCatalystTemp(resp) + " (modo 22)";
    }
    
    return "❌ No disponible";
}

// ================= Presión combustible =================
std::string GMCommands::getFuelPressure() {
    std::cout << "► Leyendo presión combustible..." << std::endl;
    
    // Método 1: 010A (OBD estándar)
    double pressure = elm->getFuelPressure();
    if (pressure >= 0) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << pressure << " kPa";
        return ss.str();
    }
    
    // Método 2: 22 F423 (modo 22 GM)
    std::cout << "   Intentando modo 22 F423..." << std::endl;
    std::string resp = sendMode22WithHeader("F423");
    if (!resp.empty()) {
        return decodeFuelPressure(resp) + " (modo 22)";
    }
    
    return "❌ No disponible";
}

// ================= Torque motor =================
std::string GMCommands::getEngineTorque() {
    std::cout << "► Leyendo torque motor..." << std::endl;
    
    // Método 1: 0163 (OBD estándar)
    double torque = elm->getEngineTorqueStandard();
    if (torque >= 0) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << torque << " Nm";
        return ss.str();
    }
    
    // Método 2: 22 F40D (modo 22 GM)
    std::cout << "   Intentando modo 22 F40D..." << std::endl;
    std::string resp = sendMode22WithHeader("F40D");
    if (!resp.empty()) {
        return decodeEngineTorque(resp) + " (modo 22)";
    }
    
    return "❌ No disponible - Torque no soportado vía OBD estándar en este vehículo";
}

// ================= Voltaje ECU =================
std::string GMCommands::getECUVoltage() {
    std::cout << "► Leyendo voltaje ECU..." << std::endl;
    
    // Método 1: 0142 (OBD estándar)
    double voltage = elm->getControlModuleVoltage();
    if (voltage >= 0) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << voltage << " V";
        return ss.str();
    }
    
    // Método 2: 22 F40A (modo 22 GM)
    std::cout << "   Intentando modo 22 F40A..." << std::endl;
    std::string resp = sendMode22WithHeader("F40A");
    if (!resp.empty()) {
        return decodeECUVoltage(resp) + " (modo 22)";
    }
    
    return "❌ No disponible - Voltaje módulo control no reportado";
}

// ================= Historial GM =================
std::string GMCommands::getGMHistory() {
    std::cout << "► Leyendo códigos pendientes (modo 07)..." << std::endl;
    
    // Método 1: Modo 07 (códigos pendientes OBD estándar)
    auto pending = elm->getPendingDTCs();
    if (!pending.empty() && !(pending.size() == 1 && pending[0].code == "NONE")) {
        std::string result = "Códigos pendientes encontrados:\n";
        for (const auto& dtc : pending) {
            if (dtc.code != "NONE") {
                result += "🔧 " + dtc.description + "\n";
            }
        }
        return result;
    }
    
    // Método 2: 19 02 (modo 19 subfunción 02 - GM)
    std::cout << "   Intentando modo 19 02..." << std::endl;
    setupGMHeader();
    std::string resp = elm->send("19 02", 500);
    if (resp.find("59") != std::string::npos && resp.find("7F") == std::string::npos) {
        return "Historial GM: " + resp.substr(0, 80);
    }
    
    return "No hay códigos pendientes almacenados";
}

// ================= Borrar historial =================
std::string GMCommands::clearGMHistory() {
    std::cout << "► Borrando códigos de error (modo 04)..." << std::endl;
    
    // Método 1: Modo 04 estándar
    if (elm->clearDTCs()) {
        return "✅ Historial borrado correctamente";
    }
    
    // Método 2: 14 FF FF (modo 14 GM específico)
    std::cout << "   Intentando modo 14 FF FF..." << std::endl;
    setupGMHeader();
    std::string resp2 = elm->send("14 FF FF", 500);
    if (resp2.find("54") != std::string::npos || resp2.find("OK") != std::string::npos) {
        return "✅ Historial GM borrado correctamente (modo 14)";
    }
    
    return "❌ Error al borrar historial";
}

// ================= Reset adaptativos =================
void GMCommands::resetAdaptations() {
    std::cout << "\n========================================\n";
    std::cout << "   RESET ADAPTATIVOS CHEVROLET/GM\n";
    std::cout << "========================================\n\n";
    
    std::cout << "⚠️  ADVERTENCIA: Modo 22 (F10E/F10D) NO es soportado\n";
    std::cout << "   por la mayoría de ECUs con CAN moderno.\n";
    std::cout << "   Los adaptativos se resetean:\n";
    std::cout << "   1. Desconectando batería 10-15 minutos\n";
    std::cout << "   2. Con escáner Tech2/GM MDI\n";
    std::cout << "   3. Con ciertos comandos con ignition ON\n\n";
    
    std::cout << "--- Intentando métodos de reset ---\n\n";
    
    // Método 1: F10E
    std::cout << "📌 Método 1: 22 F10E (Reset adaptativos)\n";
    std::string r1 = sendMode22WithHeader("F10E");
    if (!r1.empty()) {
        std::cout << "   ✅ Respuesta: " << r1 << std::endl;
    } else {
        std::cout << "   ❌ No soportado\n";
    }
    
    std::cout << "\n📌 Método 2: 22 F10D (Reset adaptativos alt.)\n";
    std::string r2 = sendMode22WithHeader("F10D");
    if (!r2.empty()) {
        std::cout << "   ✅ Respuesta: " << r2 << std::endl;
    } else {
        std::cout << "   ❌ No soportado\n";
    }
    
    std::cout << "\n📌 Método 3: 22 F11C (Reset Fuel Trims)\n";
    std::string r3 = sendMode22WithHeader("F11C");
    if (!r3.empty()) {
        std::cout << "   ✅ Respuesta: " << r3 << std::endl;
    } else {
        std::cout << "   ❌ No soportado\n";
    }
    
    // Verificación post-reset
    std::cout << "\n📌 Verificación Fuel Trims actuales:\n";
    auto ft = elm->getAllFuelTrims();
    if (ft.available) {
        auto printTrim = [](const std::string& label, double val) {
            std::cout << "   " << label << ": ";
            if (val < -50.0)
                std::cout << "⚠️  Error de lectura\n";
            else
                std::cout << std::fixed << std::setprecision(1) << val << "%\n";
        };
        printTrim("STFT B1", ft.shortTermBank1);
        printTrim("STFT B2", ft.shortTermBank2);
        printTrim("LTFT B1", ft.longTermBank1);
        printTrim("LTFT B2", ft.longTermBank2);
    } else {
        std::cout << "   No disponibles\n";
    }
    
    std::cout << "\n========================================\n";
    std::cout << "   RESET COMPLETADO\n";
    std::cout << "========================================\n";
}

// ================= Temperatura transmisión (22 1940 — ✓ Funciona) =================
std::string GMCommands::getTransmissionTemp() {
    std::cout << "► Leyendo temperatura transmisión (22 1940)...\n";
    std::string resp = sendMode22WithHeader("1940");
    if (resp.empty() || resp.find("7F") != std::string::npos) {
        return "❌ No disponible en este vehículo";
    }
    return decodeTransmissionTemp(resp);
}

// ================= Temperatura aceite =================
std::string GMCommands::getOilTempDisplay() {
    std::cout << "► Leyendo temperatura aceite (015C)...\n";
    double oilTemp = elm->getOilTemp();
    if (oilTemp >= 0) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(0) << oilTemp << " °C";
        return ss.str();
    }
    
    // Fallback: 22 1470 (Presión aceite en modo 22)
    std::cout << "   Intentando modo 22 1470 (presión aceite)...\n";
    std::string resp = sendMode22WithHeader("1470");
    if (!resp.empty()) {
        return decodeOilPressure(resp) + " (modo 22)";
    }
    
    return "❌ No disponible";
}

// ================= Knock retard =================
std::string GMCommands::getKnockRetardDisplay() {
    std::cout << "► Leyendo avance encendido / knock (010E)...\n";
    double timing = elm->getTimingAdvance();
    if (timing < -50) {
        // Fallback: 22 11A2
        std::cout << "   Intentando modo 22 11A2...\n";
        std::string resp = sendMode22WithHeader("11A2");
        if (!resp.empty()) {
            return "Knock retard: " + decodeKnockRetard(resp);
        }
        return "❌ No disponible";
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << timing << "° BTDC";
    
    if (timing < 0)      ss << " (avance negativo - posible knock)";
    else if (timing < 5) ss << " (bajo - motor bajo carga)";
    else if (timing < 15) ss << " (ralentí normal)";
    else if (timing < 30) ss << " (crucero normal)";
    else                  ss << " (alto - buena eficiencia)";
    
    double stft = elm->getShortTermTrimBank1();
    if (stft >= -50 && stft <= 50) {
        ss << " | STFT: " << std::fixed << std::setprecision(1) << stft << "%";
    }
    
    return ss.str();
}

// ============================================================================
// Escaneo de PIDs
// ============================================================================

void GMCommands::scanPIDs() {
    std::cout << "\n=== ESCANEO DE PIDs OBD-II (MODO 01) ===\n";
    std::cout << "Verificando rangos de PIDs soportados...\n\n";
    
    struct PIDRange {
        std::string cmd;
        std::string label;
        int base;
    };
    
    PIDRange ranges[] = {
        {"0100", "PIDs 01-20", 1},
        {"0120", "PIDs 21-40", 33},
        {"0140", "PIDs 41-60", 65},
        {"0160", "PIDs 61-80", 97},
        {"0180", "PIDs 81-100", 129},
        {"01A0", "PIDs 101-120", 161},
        {"01C0", "PIDs 121-140", 193}
    };
    
    for (const auto& range : ranges) {
        std::string r = elm->send(range.cmd, 500);
        auto b = elm->splitResponse(r);
        
        if (b.size() >= 6 && b[0] == "41") {
            uint32_t mask = 0;
            for (int i = 0; i < 4; i++) {
                mask = (mask << 8) | std::stoi(b[i+2], nullptr, 16);
            }
            std::cout << range.label << ": ";
            int count = 0;
            for (int pid = 1; pid <= 32; pid++) {
                if (mask & (1 << (32 - pid))) {
                    std::cout << std::hex << std::uppercase << (range.base + pid - 1) << " ";
                    count++;
                }
            }
            std::cout << std::dec << "(" << count << ")\n";
        } else {
            std::cout << range.label << ": No disponible\n";
        }
    }
    
    std::cout << "\n=== ESCANEO COMPLETADO ===\n";
}

void GMCommands::scanGMPIDs() {
    std::cout << "\n=== ESCANEO DE PIDs GM (MODO 22) ===\n";
    std::cout << "Verificando PIDs modo 22 comunes...\n\n";
    
    const std::pair<std::string, std::string> knownPIDs[] = {
        {"1940", "Temp. transmisión"},
        {"F190", "Kilometraje ECU"},
        {"F40C", "Temp. catalizador"},
        {"F40A", "Voltaje ECU"},
        {"F423", "Presión combustible"},
        {"F40D", "Torque motor"},
        {"F10E", "Reset adaptativos"},
        {"F10D", "Reset adaptativos 2"},
        {"1470", "Presión aceite"},
        {"11A2", "Knock retard"},
        {"F117", "Reset fuel trim"},
        {"F11C", "Reset fuel trim 2"}
    };
    
    int found = 0;
    for (const auto& pid : knownPIDs) {
        std::cout << "  22 " << pid.first << " (" << pid.second << ")... ";
        std::cout.flush();
        std::string resp = sendMode22WithHeader(pid.first);
        if (!resp.empty()) {
            found++;
            std::cout << "✅ " << resp.substr(0, 40) << std::endl;
        } else {
            std::cout << "❌" << std::endl;
        }
#ifndef Q_OS_WIN
        usleep(50000);
#else
        Sleep(50);
#endif
    }
    
    std::cout << "\n=== COMPLETADO: " << found << " de " << (sizeof(knownPIDs)/sizeof(knownPIDs[0])) << " PIDs modo 22 funcionan ===\n";
}

// ============================================================================
// Comando personalizado
// ============================================================================

std::string GMCommands::sendCustomCommand(const std::string& cmd) {
    std::cout << "[TX] " << cmd << std::endl;
    std::string resp = elm->send(cmd, 500);
    if (!resp.empty()) {
        auto bytes = elm->splitResponse(resp);
        std::cout << "[RX] Bytes: ";
        for (const auto& b : bytes) {
            std::cout << b << " ";
        }
        std::cout << std::endl;
    }
    return resp;
}
