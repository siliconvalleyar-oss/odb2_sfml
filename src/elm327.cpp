/**
 * @file elm327.cpp
 * @brief Implementación de la clase ELM327.
 *
 * Gestiona la conexión Bluetooth RFCOMM con el adaptador ELM327,
 * el envío/recepción de comandos OBD-II y AT, y el parseo de
 * respuestas hexadecimales.
 *
 * @note Los métodos getter devuelven -1 o "No disponible" ante
 *       errores de comunicación o timeout.
 */

#include "elm327.hpp"

#include <fstream>
#include <functional>
#include <ctime>
#include "logger.hpp"

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>

#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

ELM327::ELM327(const std::string& macAddr, int ch)
    : sock(-1), mac(macAddr), channel(ch), m_stoppedRecovery(false),
      m_stoppedPenaltyMs(0), m_consecutiveSuccess(0), m_stoppedCount(0),
      m_sessionLog(nullptr) {}

ELM327::~ELM327()
{
    disconnect();
}

//bool ELM327::isConnected() {
//    return sock >= 0;
//}

bool ELM327::connectBT()
{
    struct sockaddr_rc addr{};

    sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sock < 0)
    {
        perror("[ERROR] socket");
        return false;
    }

    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t)channel;
    str2ba(mac.c_str(), &addr.rc_bdaddr);

    std::cout << "[INFO] Conectando a " << mac << "...\n";

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("[ERROR] connect");
        return false;
    }

    std::cout << "[OK] Conectado!\n";

    // Inicializar ELM327
    send("ATZ", 1000);
    send("ATE0"); // echo off
    send("ATL0"); // linefeed off
    send("ATS0"); // spaces off
    send("ATSP0"); // protocolo automático
    
    // Configurar para respuestas más rápidas
    send("ATAT1"); // adaptor timeout 1
    send("ATST20"); // timeout 200ms

    return true;
}

void ELM327::disconnect()
{
    if (sock >= 0)
    {
        std::cout << "[INFO] Cerrando conexión\n";
        close(sock);
        sock = -1;
    }
}

std::string ELM327::readRaw()
{
    if (!isConnected()) return "";
    
    char buffer[4096];
    int n = read(sock, buffer, sizeof(buffer)-1);

    if (n > 0)
    {
        buffer[n] = 0;
        std::string result(buffer);
        
        // Limpiar caracteres no deseados
        result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
        
        return result;
    }

    return "";
}

// sendRaw - Envío con select() timeout (usado por modo 22)
// Incluye detección de STOPPED y penalidad adaptativa (como send())
std::string ELM327::sendRaw(const std::string& cmd, int timeoutMs) {
    if (!isConnected()) return "";
    
    std::string full = cmd + "\r";
    std::cout << "[TX RAW] " << cmd << std::endl;
    
    ::write(sock, full.c_str(), full.size());
    
    int effectiveTimeout = timeoutMs + m_stoppedPenaltyMs;  // + penalidad adaptativa
    char buffer[1024];
    std::string response;
    fd_set fds;
    struct timeval tv;
    
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    tv.tv_sec = effectiveTimeout / 1000;
    tv.tv_usec = (effectiveTimeout % 1000) * 1000;
    
    while (select(sock + 1, &fds, NULL, NULL, &tv) > 0) {
        int n = ::recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        response += buffer;
        if (response.find(">") != std::string::npos) break;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 300000;
    }
    
    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    
    if (!response.empty()) {
        std::cout << "[RX RAW] " << response.substr(0, 120) << std::endl;
    }
    
    // Detectar STOPPED y recuperar (con penalidad adaptativa)
    if (!m_stoppedRecovery && response.find("STOPPED") != std::string::npos) {
        std::cout << "[WARN] sendRaw detectó STOPPED! Recuperando...\n";
        m_stoppedRecovery = true;
        m_stoppedPenaltyMs = std::min(m_stoppedPenaltyMs + 100, 1000);
        m_consecutiveSuccess = 0;
        m_stoppedCount++;
        std::cout << "[ADAPT] Penalidad +" << m_stoppedPenaltyMs << "ms (total: " << m_stoppedCount << ")\n";
        recoverFromStopped();
        m_stoppedRecovery = false;
        std::cout << "[RETRY] Reintentando " << cmd << "...\n";
        return sendRaw(cmd, timeoutMs);
    }
    
    // Decay de penalidad (solo si no estamos en recovery)
    if (!m_stoppedRecovery && m_stoppedPenaltyMs > 0) {
        m_consecutiveSuccess++;
        if (m_consecutiveSuccess >= 10) {
            m_stoppedPenaltyMs = std::max(0, m_stoppedPenaltyMs - 25);
            m_consecutiveSuccess = 0;
            if (m_stoppedPenaltyMs > 0) {
                std::cout << "[ADAPT] Penalidad reducida a +" << m_stoppedPenaltyMs << "ms\n";
            } else {
                std::cout << "[ADAPT] Penalidad restablecida a 0ms\n";
            }
        }
    }
    
    return response;
}

// ================= UTILIDADES =================

std::string ELM327::send(const std::string& cmd, int delayMs)
{
    if (!isConnected()) return "";
    
    std::string full = cmd + "\r";

    std::cout << "[TX] " << cmd << std::endl;

    ::write(sock, full.c_str(), full.size());
    
    // Usar select() para timeout en lugar de solo usleep
    // Esto evita quedarse colgado si el ELM327 no responde
    char buffer[4096];
    std::string response;
    fd_set fds;
    struct timeval tv;
    int ret;
    
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    int timeoutMs = (delayMs > 0) ? delayMs : 200;
    int effectiveTimeout = timeoutMs + m_stoppedPenaltyMs;  // + penalidad adaptativa
    tv.tv_sec = effectiveTimeout / 1000;
    tv.tv_usec = (effectiveTimeout % 1000) * 1000;
    
    // Mostrar penalidad en el log si está activa
    if (m_stoppedPenaltyMs > 0 && cmd.substr(0, 2) != "AT") {
        std::cout << "[ADAPT] +" << m_stoppedPenaltyMs << "ms (" << m_stoppedCount << " STOPPED)\n";
    }
    
    while (true) {
        ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) break; // timeout o error
        
        int n = ::recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        response += buffer;
        
        // Si recibimos el prompt '>', la respuesta está completa
        if (response.find('>') != std::string::npos) break;
        
        // Resetear timeout para leer más datos
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 300000; // 300ms adicionales
    }
    
    // Limpiar caracteres de control para el log
    std::string logResp = response;
    logResp.erase(std::remove(logResp.begin(), logResp.end(), '\r'), logResp.end());
    logResp.erase(std::remove(logResp.begin(), logResp.end(), '\n'), logResp.end());
    if (!logResp.empty()) {
        std::cout << "[RX] " << logResp.substr(0, 100) << std::endl;
    }
    
    // Session log: comando + respuesta hex
    if (m_sessionLog && !cmd.empty()) {
        // Extraer solo hex de la respuesta (quitar prompt > y basura)
        std::string hexForLog = logResp;
        size_t p = hexForLog.find('>');
        if (p != std::string::npos) hexForLog = hexForLog.substr(0, p);
        // No loguear AT commands internos ni comandos vacíos (demasiado ruido)
        if (cmd.substr(0, 2) != "AT" && cmd != "\r") {
            m_sessionLog->logCommand(cmd, hexForLog);
        }
    }
    
    // Detectar estado STOPPED y recuperar automáticamente
    if (!m_stoppedRecovery && logResp.find("STOPPED") != std::string::npos) {
        std::cout << "[WARN] ELM327 en estado STOPPED! Recuperando...\n";
        m_stoppedRecovery = true;
        
        // --- Lógica adaptativa ---
        // Aumentar penalidad en 100ms por cada STOPPED (máx 1000ms extra)
        m_stoppedPenaltyMs = std::min(m_stoppedPenaltyMs + 100, 1000);
        m_consecutiveSuccess = 0;
        m_stoppedCount++;
        std::cout << "[ADAPT] Penalidad +" << m_stoppedPenaltyMs << "ms (total STOPPED: " << m_stoppedCount << ")\n";
        
        recoverFromStopped();
        m_stoppedRecovery = false;
        // Reintentar el comando original
        std::cout << "[RETRY] Reintentando " << cmd << " con penalidad " << m_stoppedPenaltyMs << "ms...\n";
        return send(cmd, delayMs);
    }
    
    // Reducción gradual de penalidad tras comandos exitosos
    // No contar comandos AT durante recovery (falsearía la cuenta)
    if (!m_stoppedRecovery && m_stoppedPenaltyMs > 0) {
        m_consecutiveSuccess++;
        if (m_consecutiveSuccess >= 10) {
            m_stoppedPenaltyMs = std::max(0, m_stoppedPenaltyMs - 25);
            m_consecutiveSuccess = 0;
            if (m_stoppedPenaltyMs > 0) {
                std::cout << "[ADAPT] Penalidad reducida a +" << m_stoppedPenaltyMs << "ms\n";
            } else {
                std::cout << "[ADAPT] Penalidad restablecida a 0ms\n";
            }
        }
    }
    
    return response;
}

std::vector<std::string> ELM327::splitResponse(const std::string& response) {
    std::vector<std::string> results;
    
    // Limpiar caracteres de control
    std::string clean = response;
    clean.erase(std::remove(clean.begin(), clean.end(), '\r'), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), '\n'), clean.end());
    
    // Eliminar todo después de '>' (incluyendo el prompt)
    size_t promptPos = clean.find('>');
    if (promptPos != std::string::npos) {
        clean = clean.substr(0, promptPos);
    }
    
    // Convertir a ASCII uppercase para consistencia
    std::transform(clean.begin(), clean.end(), clean.begin(), ::toupper);
    
    // --- Buscar inicio de datos OBD usando find() en cualquier posición ---
    // Esto funciona tanto con headers ON (ATH1) como OFF (ATH0)
    size_t startPos = std::string::npos;
    
    // Buscar headers de respuesta OBD conocidos con find() en cualquier posición
    // find() encuentra el header aunque esté desplazado por CAN headers (ATH1)
    const char* prefixes[] = {"41", "43", "44", "47", "49", "4A", "62", "7F", "61"};
    for (const char* prefix : prefixes) {
        size_t pos = clean.find(prefix);
        if (pos != std::string::npos) {
            // NO alinear a par: cuando headers CAN están activos (ATH1),
            // el header OBD puede estar en posición impar debido al CAN ID
            // de 3 nibbles (ej: 7E8). Alinear backward incluiría el byte
            // de longitud, corrompiendo el parseo.
            startPos = pos;
            break;
        }
    }
    
    // Si no se encontró header OBD, buscar "OK", "NODATA" o "SEARCHING"
    if (startPos == std::string::npos) {
        size_t okPos = clean.find("OK");
        if (okPos != std::string::npos && (okPos == 0 || !isxdigit(clean[okPos-1]))) {
            startPos = okPos;
        }
    }
    if (startPos == std::string::npos) {
        size_t ndPos = clean.find("NODATA");
        if (ndPos != std::string::npos) startPos = ndPos;
    }
    if (startPos == std::string::npos) {
        size_t sePos = clean.find("SEARCHING");
        if (sePos != std::string::npos) startPos = sePos;
    }
    
    // Fallback: empezar desde el primer hex digit (sin alinear, por si hay CAN headers)
    if (startPos == std::string::npos) {
        startPos = clean.find_first_of("0123456789ABCDEF");
        if (startPos == std::string::npos) return results;
    }
    
    std::string data = clean.substr(startPos);
    
    // Si encontramos "OK" o "NODATA", devolver eso directamente
    if (data.substr(0, 2) == "OK" || data.substr(0, 6) == "NODATA") {
        results.push_back(data);
        return results;
    }
    
    // Si es SEARCHING, extraer datos después
    if (data.substr(0, 9) == "SEARCHING") {
        data = data.substr(9);
    }
    
    // Dividir en bytes de 2 caracteres hex
    for (size_t i = 0; i + 1 < data.length(); i += 2) {
        if (i + 2 > data.length()) break;
        std::string byte = data.substr(i, 2);
        if (isxdigit(byte[0]) && isxdigit(byte[1])) {
            results.push_back(byte);
        } else {
            break; // dejar de procesar si encontramos no-hex
        }
    }
    
    return results;
}

// ============================================================================
// Implementación de parámetros del motor — PIDs OBD-II Modo 01
//
// Cada método envía el PID correspondiente, parsea la respuesta hex
// usando splitResponse(), y aplica la fórmula SAE J1979 para convertir
// los bytes a valores físicos.
// ============================================================================

int ELM327::getRPM()
{
    std::string r = send("010C");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 4) {  // RPM usa 2 bytes
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256 + b) / 4;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getSpeed()
{
    std::string r = send("010D");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            return std::stoi(bytes[2], nullptr, 16);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getCoolantTemp()
{
    std::string r = send("0105");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getTemp()
{
    return getCoolantTemp();
}

int ELM327::getEngineLoad()
{
    std::string r = send("0104");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int load = std::stoi(bytes[2], nullptr, 16);
            return (load * 100) / 255;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getThrottlePosition()
{
    std::string r = send("0111");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int pos = std::stoi(bytes[2], nullptr, 16);
            return (pos * 100.0) / 255.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getIntakePressure()
{
    std::string r = send("010B");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            return std::stoi(bytes[2], nullptr, 16);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getIntakeTemp()
{
    std::string r = send("010F");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getTimingAdvance()
{
    std::string r = send("010E");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int advance = std::stoi(bytes[2], nullptr, 16);
            return advance / 2.0 - 64;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getFuelPressure()
{
    std::string r = send("010A");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int pressure = std::stoi(bytes[2], nullptr, 16);
            return pressure * 3;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getMAF()
{
    std::string r = send("0110");
    auto bytes = splitResponse(r);
    
    // MAF usa 2 bytes de datos: (A*256+B)/100 g/s
    // Después de splitResponse: bytes[0]="41" (modo), bytes[1]="10" (PID), bytes[2]=A, bytes[3]=B
    if (bytes.size() >= 4) {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256 + b) / 100.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getFuelLevel()
{
    std::string r = send("012F");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int level = std::stoi(bytes[2], nullptr, 16);
            return (level * 100.0) / 255.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getAmbientTemp()
{
    std::string r = send("0146");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getOilTemp()
{
    std::string r = send("015C");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getCommandedEGR()
{
    std::string r = send("012C");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int egr = std::stoi(bytes[2], nullptr, 16);
            return (egr * 100.0) / 255.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getEGRError()
{
    std::string r = send("012D");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int error = std::stoi(bytes[2], nullptr, 16);
            return (error * 100.0) / 128.0 - 100;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getEVAPPressure()
{
    std::string r = send("0132");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 4) {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256 + b) / 4.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getBarometricPressure()
{
    std::string r = send("0133");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            return std::stoi(bytes[2], nullptr, 16);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

// ============================================================================
// Sensores de oxígeno — PIDs 0114, 0115, 0116, 0117
//
// Obtiene voltaje y fuel trim de corto plazo para todos los sensores
// O2 disponibles. Bank 1/2, sensors 1-8.
// ============================================================================

std::vector<OxygenSensor> ELM327::getOxygenSensors()
{
    std::vector<OxygenSensor> sensors;
    
    // PID 0114 - Sensores de oxígeno (Banco 1, Sensores 1-4)
    std::string r1 = send("0114", 400);  // delay 400ms para evitar STOPPED
    auto bytes1 = splitResponse(r1);
    
    // Respuesta esperada: 41 14 [voltaje1] [trim1] [voltaje2] [trim2] [voltaje3] [trim3] [voltaje4] [trim4]
    if (bytes1.size() >= 4 && bytes1[0] == "41" && bytes1[1] == "14") {
        for (size_t i = 2; i + 1 < bytes1.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes1[i], nullptr, 16);
                int trimByte = std::stoi(bytes1[i+1], nullptr, 16);
                
                // Si ambos son 0xFF o 0x00, no hay sensor
                if ((voltageByte == 0xFF && trimByte == 0xFF) || 
                    (voltageByte == 0x00 && trimByte == 0x00)) {
                    continue;
                }
                
                OxygenSensor sensor;
                sensor.bank = 1;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 1);
                
                // El voltaje para PID 0114 está en 0-1.275V (0 = 0V, 255 = 1.275V)
                sensor.voltage = voltageByte / 200.0;
                
                // Short term trim: 0-100% (0 = -100%, 128 = 0%, 255 = +99.2%)
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }
    
    // PID 0115 - Sensores de oxígeno (Banco 2, Sensores 1-4)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string r2 = send("0115", 400);
    auto bytes2 = splitResponse(r2);
    
    if (bytes2.size() >= 4 && bytes2[0] == "41" && bytes2[1] == "15") {
        for (size_t i = 2; i + 1 < bytes2.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes2[i], nullptr, 16);
                int trimByte = std::stoi(bytes2[i+1], nullptr, 16);
                
                if ((voltageByte == 0xFF && trimByte == 0xFF) || 
                    (voltageByte == 0x00 && trimByte == 0x00)) {
                    continue;
                }
                
                OxygenSensor sensor;
                sensor.bank = 2;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 1);
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }
    
    // PID 0116 - Sensores de oxígeno (Banco 1, Sensores 5-8) - Para vehículos con más de 4 sensores
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string r3 = send("0116", 400);
    auto bytes3 = splitResponse(r3);
    
    if (bytes3.size() >= 4 && bytes3[0] == "41" && bytes3[1] == "16") {
        for (size_t i = 2; i + 1 < bytes3.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes3[i], nullptr, 16);
                int trimByte = std::stoi(bytes3[i+1], nullptr, 16);
                
                if ((voltageByte == 0xFF && trimByte == 0xFF) || 
                    (voltageByte == 0x00 && trimByte == 0x00)) {
                    continue;
                }
                
                OxygenSensor sensor;
                sensor.bank = 1;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 5); // Sensores 5-8
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }
    
    // PID 0117 - Sensores de oxígeno (Banco 2, Sensores 5-8) - Para vehículos con más de 4 sensores
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string r4 = send("0117", 400);
    auto bytes4 = splitResponse(r4);
    
    if (bytes4.size() >= 4 && bytes4[0] == "41" && bytes4[1] == "17") {
        for (size_t i = 2; i + 1 < bytes4.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes4[i], nullptr, 16);
                int trimByte = std::stoi(bytes4[i+1], nullptr, 16);
                
                if ((voltageByte == 0xFF && trimByte == 0xFF) || 
                    (voltageByte == 0x00 && trimByte == 0x00)) {
                    continue;
                }
                
                OxygenSensor sensor;
                sensor.bank = 2;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 5); // Sensores 5-8
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }
    
    // Si no se encontraron sensores, mostrar mensaje
    if (sensors.empty()) {
        std::cout << "[INFO] No se detectaron sensores de oxígeno" << std::endl;
    }
    
    return sensors;
}



// ============================================================================
// DTCs — Códigos de error (Modo 03)
// ============================================================================

std::string ELM327::decodeDTCCode(const std::string& code) {
    if (code.length() < 4) return "Código inválido";
    
    static const std::map<std::string, std::string> dtcTypes = {
        {"P0", "Powertrain - Genérico"},
        {"P1", "Powertrain - Fabricante"},
        {"P2", "Powertrain - Genérico (P2xxx)"},
        {"P3", "Powertrain - Genérico (P3xxx)"},
        {"C0", "Chasis - Genérico"},
        {"C1", "Chasis - Fabricante"},
        {"C2", "Chasis - Genérico (C2xxx)"},
        {"C3", "Chasis - Genérico (C3xxx)"},
        {"B0", "Carrocería - Genérico"},
        {"B1", "Carrocería - Fabricante"},
        {"B2", "Carrocería - Genérico (B2xxx)"},
        {"B3", "Carrocería - Genérico (B3xxx)"},
        {"U0", "Red - Genérico"},
        {"U1", "Red - Fabricante"},
        {"U2", "Red - Genérico (U2xxx)"},
        {"U3", "Red - Genérico (U3xxx)"}
    };
    
    std::string prefix = code.substr(0, 2);
    auto it = dtcTypes.find(prefix);
    if (it != dtcTypes.end()) {
        return code + " - " + it->second;
    }
    
    return code;
}

std::vector<DTCCode> ELM327::getDTCs()
{
    std::vector<DTCCode> dtcs;
    
    // Modo 03 - Obtener DTCs almacenados
    std::string r = send("03", 500);
    
    if (r.find("NO DATA") != std::string::npos) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos de error almacenados";
        dtcs.push_back(noDTC);
        return dtcs;
    }
    
    auto bytes = splitResponse(r);
    
    // Con el nuevo splitResponse, bytes[0] debería ser "43" directamente
    if (bytes.size() >= 2 && bytes[0] == "43") {
        int numCodes = 0;
        try {
            numCodes = std::stoi(bytes[1], nullptr, 16);
        } catch (...) {}
        
        int expectedBytes = 2 + (numCodes * 2);
        if (expectedBytes > (int)bytes.size()) expectedBytes = bytes.size();
        
        for (size_t i = 2; i + 1 < (size_t)expectedBytes; i += 2) {
            std::string code = bytes[i] + bytes[i + 1];
            if (code != "0000" && code != "FFFF") {
                DTCCode dtc;
                dtc.code = code;
                dtc.description = decodeDTCCode(code);
                dtcs.push_back(dtc);
            }
        }
    }
    
    if (dtcs.empty()) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos de error almacenados";
        dtcs.push_back(noDTC);
    }
    
    return dtcs;
}

bool ELM327::clearDTCs()
{
    std::cout << "[INFO] Preparando ELM327 para limpiar DTCs..." << std::endl;

    // Para modo CAN, NO usar ATH1 (headers ON) antes de 04.
    // ATH1 + modo 04 causa respuesta 7F porque el vehículo recibe
    // el comando con header incorrecto.
    send("ATZ", 2000);    // Reset (vuelve a estado conocido) - más tiempo para estabilizar
    send("ATE0", 300);    // Echo OFF
    send("ATL0", 200);    // Linefeeds OFF
    send("ATS0", 200);    // Spaces OFF
    send("ATH0", 200);    // Headers OFF (importante para modo 04 en CAN!)
    send("ATSP0", 500);   // Auto protocolo
    send("ATAT1", 100);   // Adaptor timeout mínimo
    send("ATST10", 100);  // Timeout búsqueda 100ms
    send("ATD", 300);     // Limpiar buffer

    std::cout << "[INFO] Limpiando códigos de error..." << std::endl;

    for (int i = 0; i < 3; i++) {
        std::string r = send("04", 1500);
        std::cout << "[DEBUG] Respuesta: " << r.substr(0, 60) << std::endl;

        // Respuesta válida: 44 = positivo modo 04
        if (r.find("44") != std::string::npos) {
            std::cout << "[OK] Códigos de error borrados correctamente" << std::endl;
            // Restaurar configuración normal: headers OFF y echo OFF
            ensureNormalConfig();
            return true;
        }

        if (r.find("OK") != std::string::npos) {
            std::cout << "[OK] Comando aceptado" << std::endl;
            ensureNormalConfig();
            return true;
        }

        // Si es BUS INIT ERROR o NO DATA, reintentar
        if (r.find("BUS INIT") != std::string::npos || r.find("ERROR") != std::string::npos) {
            std::cout << "[WARN] Error de bus, reintentando..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "[ERROR] No se pudieron borrar los códigos" << std::endl;
    ensureNormalConfig();
    return false;
}
 
// ============================================================================
// Diagnóstico — Protocolo, VIN, MIL, Info del vehículo
// ============================================================================

std::string ELM327::getProtocol()
{
    std::string r = send("ATDP", 100);
    r.erase(std::remove(r.begin(), r.end(), '\r'), r.end());
    r.erase(std::remove(r.begin(), r.end(), '\n'), r.end());
    return r;
}



std::string ELM327::getVehicleInfo()
{
    std::string info = "=== INFORMACIÓN DEL VEHÍCULO ===\n";
    
    // Protocolo
    info += "Protocolo: " + getProtocol() + "\n";
    
    // VIN si está disponible - con manejo de excepciones
    try {
        std::string vin = getVIN();
        if (!vin.empty() && vin != "No disponible" && vin != "ERROR") {
            info += "VIN: " + vin + "\n";
        } else {
            info += "VIN: No disponible\n";
        }
    } catch (const std::exception& e) {
        info += "VIN: Error al leer (" + std::string(e.what()) + ")\n";
    }
    
    // ECU Name
    try {
        std::string ecu = getECUName();
        if (!ecu.empty() && ecu != "No disponible") {
            info += "ECU: " + ecu + "\n";
        }
    } catch (...) {}
    
    // Calibration ID (CVN)
    try {
        std::string calID = getCalibrationID();
        if (!calID.empty() && calID != "No disponible") {
            info += "CVN: " + calID + "\n";
        }
    } catch (...) {}
    
    // Calibration Date
    try {
        std::string calDate = getCalibrationDate();
        if (!calDate.empty() && calDate != "No disponible") {
            info += "Fecha Calibracion: " + calDate + "\n";
        }
    } catch (...) {}
    
    // MIL status
    try {
        if (checkMIL()) {
            info += "⚠️  MIL activa - Revisar códigos de error\n";
        } else {
            info += "✓ MIL inactiva\n";
        }
    } catch (const std::exception& e) {
        info += "MIL: Error al consultar\n";
    }
    
    return info;
}

// ============================================================================
// Modo 02 — Freeze Frame (Datos de congelación)
//
// Captura una "foto" de los parámetros del motor en el momento
// en que se registró un DTC. Útil para diagnóstico de fallas
// intermitentes.
// ============================================================================

std::string ELM327::getFreezeFrame() {
    std::string result;
    
    // Modo 02 - Solicitar freeze frame
    std::string r = send("02", 500);
    
    if (r.find("NO DATA") != std::string::npos || r.empty()) {
        return "No hay datos de congelación almacenados";
    }
    
    auto bytes = splitResponse(r);
    
    // Formato: 42 PID [datos...]
    // bytes[0] = "42" (modo 2 respuesta), bytes[1] = PID
    if (bytes.size() >= 2 && bytes[0] == "42") {
        std::string pid = "0x" + bytes[1];
        result = "Freeze Frame - PID " + pid + "\n";
        
        // Mostrar datos crudos
        result += "Datos: ";
        for (size_t i = 2; i < bytes.size(); i++) {
            result += bytes[i] + " ";
        }
        
        // Intentar decodificar PIDs conocidos
        if (bytes.size() >= 4) {
            try {
                if (bytes[1] == "05") { // Coolant temp
                    int temp = std::stoi(bytes[2], nullptr, 16) - 40;
                    result += "\nTemp. refrigerante: " + std::to_string(temp) + "°C";
                } else if (bytes[1] == "0C") { // RPM
                    int a = std::stoi(bytes[2], nullptr, 16);
                    int b = std::stoi(bytes[3], nullptr, 16);
                    result += "\nRPM: " + std::to_string((a * 256 + b) / 4);
                } else if (bytes[1] == "0D") { // Speed
                    result += "\nVelocidad: " + std::to_string(std::stoi(bytes[2], nullptr, 16)) + " km/h";
                } else if (bytes[1] == "04") { // Engine load
                    int load = (std::stoi(bytes[2], nullptr, 16) * 100) / 255;
                    result += "\nCarga motor: " + std::to_string(load) + "%";
                } else if (bytes[1] == "0F") { // Intake temp
                    int temp = std::stoi(bytes[2], nullptr, 16) - 40;
                    result += "\nTemp. admisión: " + std::to_string(temp) + "°C";
                }
            } catch (...) {}
        }
    } else {
        result = "Respuesta inesperada (" + r.substr(0, 40) + ")";
    }
    
    return result;
}

// ============================================================================
// Modo 06 — Monitoreo interno OBD
//
// Proporciona resultados de monitores de diagnóstico internos:
// catalizador, sensor O2, EGR, EVAP, VVT, etc.
// Cada monitor tiene un TID (Test ID) y MIDs (Monitor IDs).
// ============================================================================

std::string ELM327::getMonitorTests() {
    std::string result;
    
    // Modo 06 - Solicitar resultados de monitoreo
    std::string r = send("06", 800);
    
    if (r.find("NO DATA") != std::string::npos || r.empty()) {
        return "Monitores OBD no disponibles o no soportados";
    }
    
    auto bytes = splitResponse(r);
    
    // Formato: 46 TID [MID] [datos...]
    if (bytes.size() >= 2 && bytes[0] == "46") {
        result = "=== MONITORES OBD (MODO 06) ===\n";
        result += "Test ID: " + bytes[1] + "\n";
        
        // Decodificar TIDs conocidos
        std::string tidName;
        if (bytes[1] == "00") tidName = "Catalizador B1";
        else if (bytes[1] == "01") tidName = "Catalizador B2";
        else if (bytes[1] == "02") tidName = "Sensor O2 B1";
        else if (bytes[1] == "03") tidName = "Sensor O2 B2";
        else if (bytes[1] == "20") tidName = "Monitor EGR";
        else if (bytes[1] == "21") tidName = "Monitor VVT";
        else if (bytes[1] == "31") tidName = "Componente EVAP";
        else tidName = "TID 0x" + bytes[1];
        result += "Test: " + tidName + "\n";
        
        // Mostrar datos crudos
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            try {
                int val = std::stoi(bytes[i+1], nullptr, 16);
                result += "  MID " + bytes[i] + ": " + std::to_string(val);
                // Intentar interpretar como porcentaje si <= 255
                if (bytes.size() > i + 2) {
                    int max = 255;
                    result += " (" + std::to_string(val * 100 / max) + "%)";
                }
                result += "\n";
            } catch (...) {
                result += "  MID " + bytes[i] + ": " + bytes[i+1] + "\n";
            }
        }
    } else {
        result = "Modo 06 no soportado por esta ECU (respuesta: " + r.substr(0, 30) + ")";
    }
    
    return result;
}

std::string ELM327::getTestResults() {
    return getMonitorTests(); // alias
}

// ============================================================================
// Modo 08 — Prueba de sensor O2
//
// Solicita una prueba de funcionamiento del sensor de oxígeno.
// El PID de prueba se calcula según banco y sensor.
// ============================================================================

bool ELM327::requestO2Test(int bank, int sensor) {
    std::cout << "[INFO] Solicitando prueba de sensor O2 B" << bank << "S" << sensor << "...\n";
    
    // Modo 08 - PID de prueba según banco/sensor
    // Formato: 08 [PID de prueba]
    // PID = 0x01 para O2 Heater Monitor, 0x02 para O2 Sensor
    // El sensor se indica en la posición del byte
    int sensorPos = (bank - 1) * 4 + (sensor - 1);
    std::stringstream ss;
    ss << "08 " << std::hex << std::setw(2) << std::setfill('0') << (0x01 + sensorPos);
    
    std::string resp = send(ss.str(), 1000);
    
    if (resp.find("48") != std::string::npos) {
        std::cout << "[OK] Prueba de sensor completada\n";
        return true;
    }
    
    std::cout << "[WARN] Prueba no disponible o no soportada\n";
    return false;
}

// ============================================================================
// Modo 09 — Información de la ECU (extendida)
//
// Obtiene datos de identificación del vehículo: nombre de ECU,
// ID de calibración (CVN), fecha de calibración, y seguimiento
// de rendimiento.
// ============================================================================

/**
 * @brief Extrae cadena ASCII de una respuesta Modo 09.
 * @param response Respuesta completa del ELM327.
 * @param header Header esperado (ej: "490A", "4904").
 * @return Cadena ASCII extraída, o "No disponible".
 */
static std::string extractMode09String(const std::string& response, const std::string& header) {
    size_t pos = response.find(header);
    if (pos == std::string::npos) return "No disponible";
    std::string data = response.substr(pos + 4);
    data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
    data.erase(std::remove(data.begin(), data.end(), '\r'), data.end());
    data.erase(std::remove(data.begin(), data.end(), '\n'), data.end());
    std::string result;
    bool startData = false;
    for (size_t i = 0; i + 1 < data.length(); i += 2) {
        std::string byteStr = data.substr(i, 2);
        if (!startData && byteStr == "01") { startData = true; continue; }
        if (startData) {
            try {
                int val = std::stoi(byteStr, nullptr, 16);
                if (val >= 32 && val <= 126) result += static_cast<char>(val);
                else break;
            } catch (...) { break; }
        }
    }
    if (!result.empty()) return result;
    return "No disponible";
}

std::string ELM327::getECUName() {
    return extractMode09String(send("090A", 500), "490A");
}

std::string ELM327::getCalibrationID() {
    return extractMode09String(send("0904", 500), "4904");
}

std::string ELM327::getCalibrationDate() {
    std::string resp = send("0906", 500);
    std::string date = extractMode09String(resp, "4906");
    // Si no es ASCII legible, mostrar crudo
    if (date == "No disponible") {
        size_t pos = resp.find("4906");
        if (pos != std::string::npos) {
            std::string data = resp.substr(pos + 4, 16);
            data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
            if (data.length() >= 8) {
                try {
                    int year = std::stoi(data.substr(0, 2), nullptr, 16) + 2000;
                    int month = std::stoi(data.substr(2, 2), nullptr, 16);
                    int day = std::stoi(data.substr(4, 2), nullptr, 16);
                    return std::to_string(year) + "-" +
                           (month < 10 ? "0" : "") + std::to_string(month) + "-" +
                           (day < 10 ? "0" : "") + std::to_string(day);
                } catch (...) {}
            }
        }
    }
    return date;
}

std::string ELM327::getPerformanceTracking() {
    std::string resp = send("0908", 500);
    std::string result = extractMode09String(resp, "4908");
    if (result == "No disponible") {
        // Mostrar datos crudos si no hay string ASCII
        size_t pos = resp.find("4908");
        if (pos != std::string::npos) {
            std::string data = resp.substr(pos + 4, 20);
            data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
            if (!data.empty()) result = "Datos: " + data;
        }
    }
    return result;
}

// ============================================================================
// Auto-Scan — Escaneo completo de módulos CAN
//
// Escanea hasta 9 módulos (ECU, TCM, ABS, Airbag, BCM, etc.)
// Verifica respuesta y lee DTCs de cada uno.
// ============================================================================

std::vector<ELM327::ModuleScanResult> ELM327::autoScan() {
    std::vector<ModuleScanResult> results;
    
    // IDs de módulos comunes
    const int moduleIds[] = {0x7E0, 0x7E1, 0x7E2, 0x7E3, 0x7E4, 0x7E5, 0x7E6, 0x7E7, 0x7C0};
    const char* moduleNames[] = {"ECU Motor", "TCM", "ABS", "Suspensión", "Airbag", "Cluster", "BCM", "TCM2", "Infotainment"};
    const int numModules = 9;
    
    ensureNormalConfig();
    
    for (int i = 0; i < numModules; i++) {
        int id = moduleIds[i];
        std::stringstream ss;
        ss << std::hex << id;
        std::string idStr = ss.str();
        
        ModuleScanResult mod;
        mod.id = id;
        mod.name = moduleNames[i];
        mod.responds = false;
        
        std::cout << "  Escaneando " << mod.name << " (0x" << idStr << ")... " << std::flush;
        
        // Configurar header para este módulo
        send("AT SH " + idStr, 30);
        send("AT CRA " + std::to_string(id + 8), 30);
        usleep(80000);
        
        // Verificar si responde con PID 0100
        std::string r = send("0100", 200);
        
        if (r.find("41") != std::string::npos) {
            mod.responds = true;
            std::cout << "✅ RESPONDE\n";
            
            // Leer DTCs del módulo
            std::string dtcResp = send("03", 500);
            auto dtcBytes = splitResponse(dtcResp);
            if (dtcBytes.size() >= 2 && dtcBytes[0] == "43") {
                int numCodes = std::stoi(dtcBytes[1], nullptr, 16);
                for (size_t j = 2; j + 1 < dtcBytes.size() && j < (size_t)(2 + numCodes * 2); j += 2) {
                    std::string code = dtcBytes[j] + dtcBytes[j + 1];
                    if (code != "0000") {
                        DTCCode dtc;
                        dtc.code = code;
                        dtc.description = decodeDTCCode(code);
                        mod.dtcs.push_back(dtc);
                    }
                }
            }
        } else {
            std::cout << "-\n";
        }
        
        results.push_back(mod);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Restaurar header ECU motor
    send("AT SH", 30);
    send("AT CRA", 30);
    ensureNormalConfig();
    
    return results;
}

 
 
std::string ELM327::getVIN()
{
    // Primero, obtener la respuesta cruda sin procesar
    std::string fullCmd = "0902\r";
    write(sock, fullCmd.c_str(), fullCmd.size());
    usleep(500000);
    
    // Leer respuesta cruda
    char buffer[1024];
    int n = read(sock, buffer, sizeof(buffer)-1);
    if (n <= 0) return "No disponible";
    
    buffer[n] = '\0';
    std::string response(buffer);
    
    // Limpiar solo caracteres de control básicos
    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    
    std::cout << "[DEBUG VIN RAW] " << response << std::endl;
    
    // Buscar el patrón de respuesta correcta
    // Formato: 49 02 01 [bytes ASCII del VIN]
    std::string vin;
    size_t pos = response.find("4902");
    
    if (pos != std::string::npos) {
        // Extraer la parte después de 49 02
        std::string data = response.substr(pos + 4);
        
        // Eliminar espacios para procesar
        data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
        
        // Procesar bytes en pares
        for (size_t i = 0; i < data.length(); i += 2) {
            if (i + 2 <= data.length()) {
                std::string byteStr = data.substr(i, 2);
                // Ignorar el primer byte (que podría ser el contador)
                if (i >= 2) {  // Saltar el byte 0x01 que indica inicio de datos
                    try {
                        int val = std::stoi(byteStr, nullptr, 16);
                        if (val >= 32 && val <= 126) {
                            vin += static_cast<char>(val);
                        }
                    } catch (...) {
                        // Si no se puede convertir, terminar
                        break;
                    }
                }
            }
        }
    }
    
    // Si no se encontró VIN con el método anterior, intentar método alternativo
    if (vin.empty()) {
        // Buscar secuencia de bytes que sean ASCII válidos
        std::string cleanResp;
        for (char c : response) {
            if (isxdigit(c) || c == ' ') {
                cleanResp += c;
            }
        }
        
        std::stringstream ss(cleanResp);
        std::string byte;
        std::vector<std::string> bytes;
        
        while (ss >> byte) {
            if (byte.length() == 2) {
                bytes.push_back(byte);
            }
        }
        
        // Buscar el patrón 49 02 y luego extraer ASCII
        for (size_t i = 0; i < bytes.size() - 1; i++) {
            if (bytes[i] == "49" && bytes[i+1] == "02") {
                for (size_t j = i + 2; j < bytes.size(); j++) {
                    try {
                        int val = std::stoi(bytes[j], nullptr, 16);
                        if (val >= 32 && val <= 126) {
                            vin += static_cast<char>(val);
                        } else if (val == 0 || val == 0xFF) {
                            break;
                        }
                    } catch (...) {
                        break;
                    }
                }
                break;
            }
        }
    }
    
    // Limpiar caracteres no deseados
    if (!vin.empty()) {
        // Eliminar cualquier carácter no imprimible
        vin.erase(std::remove_if(vin.begin(), vin.end(), 
            [](char c) { return !isprint(c); }), vin.end());
        return vin;
    }
    
    return "No disponible";
}
 

bool ELM327::checkMIL()
{
    // PID 0x01 - Estado de diagnóstico
    std::string r = send("0101");
    auto bytes = splitResponse(r);
    
    if (bytes.size() >= 3) {
        try {
            int status = std::stoi(bytes[2], nullptr, 16);
            return (status & 0x80) != 0; // Bit 7 = MIL
        } catch (...) {
            return false;
        }
    }
    
    return false;
}

bool ELM327::setProtocol(int protocol)
{
    std::string cmd = "ATSP" + std::to_string(protocol);
    std::string r = send(cmd, 500);
    return r.find("OK") != std::string::npos;
}

bool ELM327::resetELM()
{
    std::string r = send("ATZ", 2000);
    return r.find("ELM327") != std::string::npos;
}
 

// ============================================================================
// Comandos GM (Modo 22) — Comunicación con ECU en 7E0/7E8
// ============================================================================

std::string ELM327::sendCommand(const std::string& pidHex)
{
    if (!isConnected()) return "";

    // Configurar cabezal para la ECU del motor (GM)
    // 7E0/7E8 son los estándar OBD-II, no necesitan save/restore
    send("AT SH 7E0");   // Cabezal de solicitud
    send("AT CRA 7E8");  // Cabezal de respuesta
    usleep(50000);       // Pequeña pausa

    // Construir comando modo 22
    std::string cmd = "22 " + pidHex;
    std::cout << "[TX GM] " << cmd << std::endl;

    // Enviar comando
    std::string full = cmd + "\r";
    ::write(sock, full.c_str(), full.size());

    // Leer respuesta con timeout (500ms)
    char buffer[512];
    std::string response;
    fd_set fds;
    struct timeval tv;
    int ret;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    while (true) {
        ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) break;

        int n = ::recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        response += buffer;

        if (response.find(">") != std::string::npos) break;
    }

    // Limpiar respuesta
    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '>'), response.end());

    // Mostrar diagnóstico
    std::cout << "[RX GM] " << response << std::endl;

    // Detectar error
    if (response.find("7F") == 0) {
        std::cerr << "⚠️  Error en comando GM: PID no soportado o comunicación fallida" << std::endl;
        return "";
    }

    return response;
}



// ============================================================================
// Fuel Trims — STFT y LTFT por banco
// ============================================================================

double ELM327::getShortTermTrimBank1() {
    // PID 0114, primer sensor (banco 1, sensor 1)
    std::string r = send("0114");
    auto bytes = splitResponse(r);
    // Respuesta esperada: 41 14 [voltaje1] [trim1] ...
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "14") {
        try {
            int trimByte = std::stoi(bytes[3], nullptr, 16);
            return (trimByte * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

double ELM327::getShortTermTrimBank2() {
    std::string r = send("0115");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "15") {
        try {
            int trimByte = std::stoi(bytes[3], nullptr, 16);
            return (trimByte * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

double ELM327::getLongTermTrimBank1() {
    std::string r = send("0107");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 3) {
        try {
            int trim = std::stoi(bytes[2], nullptr, 16);
            return (trim * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

double ELM327::getLongTermTrimBank2() {
    std::string r = send("0108");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 3) {
        try {
            int trim = std::stoi(bytes[2], nullptr, 16);
            return (trim * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

FuelTrim ELM327::getAllFuelTrims() {
    FuelTrim ft;
    // Consultar cada PID con delay entre ellos para evitar STOPPED
    ft.shortTermBank1 = getShortTermTrimBank1();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ft.shortTermBank2 = getShortTermTrimBank2();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ft.longTermBank1 = getLongTermTrimBank1();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ft.longTermBank2 = getLongTermTrimBank2();
    ft.available = (ft.shortTermBank1 != -999.0 || ft.shortTermBank2 != -999.0 ||
                    ft.longTermBank1 != -999.0 || ft.longTermBank2 != -999.0);
    return ft;
}

OxygenSensor ELM327::getO2Sensor(int bank, int sensor) {
    OxygenSensor empty;
    empty.bank = bank;
    empty.sensor = sensor;
    empty.voltage = -1.0;
    empty.shortTermTrim = -1.0;
    
    std::string pid;
    if (bank == 1) {
        if (sensor <= 4) pid = "0114";
        else if (sensor <= 8) pid = "0116";
        else return empty;
    } else if (bank == 2) {
        if (sensor <= 4) pid = "0115";
        else if (sensor <= 8) pid = "0117";
        else return empty;
    } else return empty;
    
    std::string r = send(pid);
    auto bytes = splitResponse(r);
    // Formato: 41 14 v1 t1 v2 t2 v3 t3 v4 t4
    int index = 2 + (sensor-1)*2;
    if ((int)bytes.size() > index+1 && bytes[0] == "41" && (bytes[1] == pid.substr(2,2) || bytes[1] == pid.substr(2))) {
        try {
            int vByte = std::stoi(bytes[index], nullptr, 16);
            int tByte = std::stoi(bytes[index+1], nullptr, 16);
            empty.voltage = vByte / 200.0;
            empty.shortTermTrim = (tByte * 100.0 / 128.0) - 100;
        } catch (...) {}
    }
    return empty;
}

// ============================================================================
// PIDs OBD-II adicionales — Catalizador, voltaje, torque, odómetro
// ============================================================================

int ELM327::getCatalystTempB1S1() {
    // PID 013C - Catalyst Temperature Bank 1 Sensor 1
    // Fórmula: ((A*256)+B)/10 - 40 = °C
    std::string r = send("013C");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "3C") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (int)(((a * 256.0 + b) / 10.0) - 40.0);
        } catch (...) { return -1; }
    }
    return -1;
}

double ELM327::getControlModuleVoltage() {
    // PID 0142 - Control module voltage
    // Fórmula: (A*256+B)/1000 = V
    std::string r = send("0142");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "42") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256.0 + b) / 1000.0;
        } catch (...) { return -1.0; }
    }
    return -1.0;
}

double ELM327::getEngineTorqueStandard() {
    // PID 0163 - Engine torque reference
    // Fórmula: ((A*256)+B)/2 - 250 = Nm (o A*100/255 para algunos vehículos)
    std::string r = send("0163");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "63") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return ((a * 256.0 + b) / 2.0) - 250.0;
        } catch (...) { return -1.0; }
    }
    // Fallback: usar PID 0161 - Driver's demand torque
    std::string r2 = send("0161");
    auto bytes2 = splitResponse(r2);
    if (bytes2.size() >= 3 && bytes2[0] == "41" && bytes2[1] == "61") {
        try {
            int a = std::stoi(bytes2[2], nullptr, 16);
            return (a * 100.0 / 255.0) * 0.5; // aproximación
        } catch (...) { return -1.0; }
    }
    return -1.0;
}

int ELM327::getOdometer() {
    // PID 01A6 - Odometer (no estándar, algunos vehículos lo soportan)
    // Fórmula: (A*256*256 + B*256 + C) / X según vehículo
    std::string r = send("01A6");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 5 && bytes[0] == "41" && bytes[1] == "A6") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            int c = std::stoi(bytes[4], nullptr, 16);
            return (a * 65536 + b * 256 + c); // km
        } catch (...) { return -1; }
    }
    return -1;
}

std::vector<DTCCode> ELM327::getPendingDTCs() {
    std::vector<DTCCode> dtcs;
    // Modo 07 - Códigos de error pendientes (no confirmados)
    std::string r = send("07", 500);
    
    if (r.find("NO DATA") != std::string::npos) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos pendientes";
        dtcs.push_back(noDTC);
        return dtcs;
    }
    
    auto bytes = splitResponse(r);
    // Con nuevo splitResponse: bytes[0]="47" directamente
    if (bytes.size() >= 2 && bytes[0] == "47") {
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            std::string code = bytes[i] + bytes[i + 1];
            if (code != "0000" && code != "FFFF") {
                DTCCode dtc;
                dtc.code = code;
                dtc.description = decodeDTCCode(code);
                dtcs.push_back(dtc);
            }
        }
    }
    
    if (dtcs.empty()) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos pendientes";
        dtcs.push_back(noDTC);
    }
    return dtcs;
}

std::vector<DTCCode> ELM327::getPermanentDTCs() {
    std::vector<DTCCode> dtcs;
    // Modo 0A - Códigos de error permanentes
    std::string r = send("0A", 500);
    
    if (r.find("NO DATA") != std::string::npos) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos permanentes";
        dtcs.push_back(noDTC);
        return dtcs;
    }
    
    auto bytes = splitResponse(r);
    if (bytes.size() >= 2 && bytes[0] == "4A") {
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            std::string code = bytes[i] + bytes[i + 1];
            if (code != "0000" && code != "FFFF") {
                DTCCode dtc;
                dtc.code = code;
                dtc.description = decodeDTCCode(code);
                dtcs.push_back(dtc);
            }
        }
    }
    
    if (dtcs.empty()) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos permanentes";
        dtcs.push_back(noDTC);
    }
    return dtcs;
}

bool ELM327::clearPendingDTCs() {
    // Modo 04 también limpia DTCs pendientes
    return clearDTCs();
}

ELM327::DashboardData ELM327::getDashboardFast() {
    DashboardData d;
    d.valid = false;
    d.rpm = -1; d.speed = -1; d.coolant = -1; d.load = -1;
    d.throttle = -1; d.maf = -1; d.fuelLevel = -1; d.timing = -1;
    d.intakeTemp = -1; d.intakePressure = -1;
    
    if (!isConnected()) return d;
    
    // Asegurar configuración normal antes de lectura rápida
    ensureNormalConfig();
    
    // Leer cada PID individualmente con send() para evitar STOPPED
    // Esto es más lento (~3s) pero mucho más confiable que el batch
    std::string rawData;
    
    // PIDs a leer: RPM, Speed, Coolant, Load, Throttle, MAP, IntakeTemp, Timing, MAF, FuelLevel
    const char* pids[] = {"010C", "010D", "0105", "0104", "0111", "010B", "010F", "010E", "0110", "012F"};
    const int numPids = 10;
    
    for (int i = 0; i < numPids; i++) {
        std::string resp = send(std::string(pids[i]), 300);
        rawData += resp;
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // pausa entre comandos (evita STOPPED)
    }
    
    // Procesar respuestas acumuladas
    auto bytes = splitResponse(rawData);
    
    for (size_t i = 0; i + 2 < bytes.size(); ) {
        if (bytes[i] == "41") {
            std::string pid = bytes[i+1];
            try {
                if (pid == "0C" && i + 3 < bytes.size()) { // RPM
                    int a = std::stoi(bytes[i+2], nullptr, 16);
                    int b = std::stoi(bytes[i+3], nullptr, 16);
                    d.rpm = (a * 256 + b) / 4;
                    i += 4;
                } else if (pid == "0D") { // Speed
                    d.speed = std::stoi(bytes[i+2], nullptr, 16);
                    i += 3;
                } else if (pid == "05") { // Coolant temp
                    d.coolant = std::stoi(bytes[i+2], nullptr, 16) - 40;
                    i += 3;
                } else if (pid == "04") { // Engine load
                    d.load = (std::stoi(bytes[i+2], nullptr, 16) * 100) / 255;
                    i += 3;
                } else if (pid == "11") { // Throttle
                    d.throttle = (std::stoi(bytes[i+2], nullptr, 16) * 100.0) / 255.0;
                    i += 3;
                } else if (pid == "0B") { // Intake pressure
                    d.intakePressure = std::stoi(bytes[i+2], nullptr, 16);
                    i += 3;
                } else if (pid == "0F") { // Intake temp
                    d.intakeTemp = std::stoi(bytes[i+2], nullptr, 16) - 40;
                    i += 3;
                } else if (pid == "0E") { // Timing advance
                    d.timing = (std::stoi(bytes[i+2], nullptr, 16) / 2.0) - 64;
                    i += 3;
                } else if (pid == "10" && i + 3 < bytes.size()) { // MAF
                    int a = std::stoi(bytes[i+2], nullptr, 16);
                    int b = std::stoi(bytes[i+3], nullptr, 16);
                    d.maf = (a * 256 + b) / 100.0;
                    i += 4;
                } else if (pid == "2F") { // Fuel level
                    d.fuelLevel = (std::stoi(bytes[i+2], nullptr, 16) * 100.0) / 255.0;
                    i += 3;
                } else {
                    i += 3;
                }
            } catch (...) {
                i += 3;
            }
        } else {
            i++;
        }
    }
    
    d.valid = (d.rpm >= 0);
    return d;
}

// ============================================================================
// Recuperación de estado STOPPED y configuración normal
//
// recoverFromStopped() intenta ATD primero, ATZ como fallback,
// limpia buffer, y reconfigura el ELM327.
// ============================================================================

bool ELM327::recoverFromStopped() {
    std::cout << "[RECOVERY] Recuperando ELM327 de estado STOPPED...\n";
    
    // 1. Enviar comando vacío para "despertar"
    const char* wake = "\r";
    ::write(sock, wake, 1);
    usleep(150000);
    
    // 2. ATD (Defaults) usando send() con select() timeout (más robusto que raw write)
    std::string atdResp = send("ATD", 500);
    
    // 3. Si ATD no responde, intentar ATZ como fallback
    //    Si ATZ es necesario, resetear penalidad (dispositivo se reinició)
    if (atdResp.empty() || atdResp.find("OK") == std::string::npos) {
        std::cout << "[RECOVERY] ATD no respondió, intentando ATZ...\n";
        send("ATZ", 2000);
        if (m_stoppedPenaltyMs > 0) {
            m_stoppedPenaltyMs = 0;
            m_consecutiveSuccess = 0;
            std::cout << "[ADAPT] Penalidad restablecida a 0ms (ATZ completado)\n";
        }
    }
    
    // 4. Limpiar buffer completamente con timeout progresivo
    //    Cada iteración resetea tv para evitar acumulación de timeout
    char tmp[256];
    fd_set fds;
    struct timeval tv;
    
    for (int attempt = 0; attempt < 5; attempt++) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  // 200ms por intento
        
        int ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) break; // No hay más datos o timeout
        
        if (::recv(sock, tmp, sizeof(tmp) - 1, 0) <= 0) break;
    }
    
    // 5. Reconfigurar completamente (ATSP0 re-detecta protocolo CAN)
    send("ATE0", 150);   // Echo OFF
    send("ATH0", 150);   // Headers OFF
    send("ATS0", 150);   // Spaces OFF
    send("ATL0", 150);   // Linefeeds OFF
    send("ATSP0", 500);  // Protocolo automático (re-detecta después de STOPPED)
    send("ATAT1", 100);  // Adaptive timing mínimo
    send("ATST20", 100); // Timeout 200ms
    
    std::cout << "[RECOVERY] ELM327 recuperado correctamente\n";
    return true;
}

void ELM327::ensureNormalConfig() {
    send("ATE0", 150);  // Echo OFF
    send("ATH0", 150);  // Headers OFF
    send("ATS0", 150);  // Spaces OFF
    send("ATL0", 150);  // Linefeeds OFF
    send("ATAT1", 100); // Adaptive timing mínimo
    send("ATST20", 100);// Timeout 200ms
}

// ============================================================================
// Logging completo — Todos los sensores con respuesta hexadecimal
// ============================================================================

void ELM327::logAllSensorsRaw(const std::string& filename) {
    Logger::ensureLogsDir();
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[ERROR] No se pudo crear " << filename << std::endl;
        return;
    }
    
    // Cabecera: timestamp, PID, comando, respuesta_hex, valor_interpretado
    file << "timestamp,pid,command,hex_response,interpreted_value\n";
    
    // Lista de PIDs a consultar (nombre, comando, función de interpretación)
    struct SensorInfo {
        std::string name;
        std::string cmd;
        std::function<std::string()> interpreter; // lambda que devuelve string
    };
    
    // Usamos capturas de this para llamar a los métodos
    std::vector<SensorInfo> sensors = {
        {"RPM", "010C", [this]() { return std::to_string(getRPM()); }},
        {"Speed", "010D", [this]() { return std::to_string(getSpeed()); }},
        {"CoolantTemp", "0105", [this]() { return std::to_string(getCoolantTemp()); }},
        {"EngineLoad", "0104", [this]() { return std::to_string(getEngineLoad()); }},
        {"Throttle", "0111", [this]() { return std::to_string(getThrottlePosition()); }},
        {"IntakePressure", "010B", [this]() { return std::to_string(getIntakePressure()); }},
        {"IntakeTemp", "010F", [this]() { return std::to_string(getIntakeTemp()); }},
        {"TimingAdvance", "010E", [this]() { return std::to_string(getTimingAdvance()); }},
        {"MAF", "0110", [this]() { return std::to_string(getMAF()); }},
        {"FuelLevel", "012F", [this]() { return std::to_string(getFuelLevel()); }},
        {"BaroPressure", "0133", [this]() { return std::to_string(getBarometricPressure()); }},
        {"LTFT_Bank1", "0107", [this]() { return std::to_string(getLongTermTrimBank1()); }},
        {"LTFT_Bank2", "0108", [this]() { return std::to_string(getLongTermTrimBank2()); }},
        {"STFT_Bank1_S1", "0114", [this]() { return std::to_string(getShortTermTrimBank1()); }},
        {"STFT_Bank2_S1", "0115", [this]() { return std::to_string(getShortTermTrimBank2()); }},
        {"O2_B1S1_Voltage", "0114", [this]() { return std::to_string(getO2Sensor(1,1).voltage); }},
        {"O2_B2S1_Voltage", "0115", [this]() { return std::to_string(getO2Sensor(2,1).voltage); }}
    };
    
    time_t now = time(nullptr);
    for (auto& s : sensors) {
        // Usar send() en vez de raw write + readRaw para tener detección de STOPPED
        std::string resp = send(s.cmd, 400);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // pausa entre comandos
        
        // Limpiar respuesta para logueo raw (solo hex)
        std::string rawResp = resp;
        rawResp.erase(std::remove(rawResp.begin(), rawResp.end(), '\r'), rawResp.end());
        rawResp.erase(std::remove(rawResp.begin(), rawResp.end(), '\n'), rawResp.end());
        size_t p = rawResp.find('>');
        if (p != std::string::npos) rawResp = rawResp.substr(0, p);
        
        // Valor interpretado (segunda consulta vía getter)
        std::string value = s.interpreter();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // pausa entre PIDs
        
        file << now << ",\"" << s.name << "\",\"" << s.cmd << "\",\""
             << rawResp << "\",\"" << value << "\"\n";
        file.flush();
    }
    
    file.close();
    std::cout << "[OK] Log completo guardado en " << filename << std::endl;
}

// ============================================================================
// Log específico para P0171 (Mezcla pobre)
//
// Monitorea: RPM, Speed, STFT B1+B2, LTFT B1+B2, O2 B1S1+B2S1 voltages.
// Optimizado: PID 0114 da STFT B1 + O2 B1S1 en UNA consulta
//             PID 0115 da STFT B2 + O2 B2S1 en UNA consulta
// Total: 5 consultas por ciclo (en vez de 8)
// ============================================================================

void ELM327::logP0171Diagnostic(const std::string& filename, int durationSec) {
    Logger::ensureLogsDir();
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[ERROR] No se pudo crear " << filename << std::endl;
        return;
    }
    
    // Cabecera
    file << "timestamp,rpm,speed,stft_b1(%),stft_b2(%),ltft_b1(%),ltft_b2(%),o2_b1s1_voltage(V),o2_b2s1_voltage(V)\n";
    
    time_t start = time(nullptr);
    int count = 0;
    
    std::cout << "[INFO] Registrando datos para diagnóstico P0171 durante " << durationSec << " segundos...\n";
    std::cout << "[INFO] Muestreo continuo (~1s por muestra)\n";
    std::cout << "Presione Ctrl+C para detener antes.\n\n";
    
    while (time(nullptr) - start < durationSec) {
        time_t now = time(nullptr);
        
        // --- Consultas individuales con delays para evitar STOPPED ---
        
        // 1. RPM (010C)
        int rpm = getRPM();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 2. Speed (010D)
        int speed = getSpeed();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 3. LTFT B1 (0107)
        double ltft1 = -999.0;
        {
            std::string r = send("0107", 300);
            auto b = splitResponse(r);
            if (b.size() >= 3) {
                ltft1 = (std::stoi(b[2], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 4. LTFT B2 (0108)
        double ltft2 = -999.0;
        {
            std::string r = send("0108", 300);
            auto b = splitResponse(r);
            if (b.size() >= 3) {
                ltft2 = (std::stoi(b[2], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 5. PID 0114 → STFT B1 + O2 B1S1 (UNA consulta)
        double stft1 = -999.0;
        double o2v1 = -1.0;
        {
            std::string r = send("0114", 400);
            auto b = splitResponse(r);
            if (b.size() >= 4 && b[0] == "41" && b[1] == "14") {
                o2v1 = std::stoi(b[2], nullptr, 16) / 200.0;
                stft1 = (std::stoi(b[3], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 6. PID 0115 → STFT B2 + O2 B2S1 (UNA consulta)
        double stft2 = -999.0;
        double o2v2 = -1.0;
        {
            std::string r = send("0115", 400);
            auto b = splitResponse(r);
            if (b.size() >= 4 && b[0] == "41" && b[1] == "15") {
                o2v2 = std::stoi(b[2], nullptr, 16) / 200.0;
                stft2 = (std::stoi(b[3], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        
        // Escribir al archivo
        file << now << ","
             << rpm << ","
             << speed << ","
             << stft1 << ","
             << stft2 << ","
             << ltft1 << ","
             << ltft2 << ","
             << o2v1 << ","
             << o2v2 << "\n";
        file.flush();
        
        count++;
        if (count % 5 == 0) {
            std::cout << "   Registros: " << count << " | Último: RPM=" << rpm
                      << " STFT_B1=" << std::fixed << std::setprecision(1) << stft1 << "%"
                      << " LTFT_B1=" << ltft1 << "%"
                      << "\r" << std::flush;
        }
        
        // Esperar hasta completar ~1s (ya gastamos ~700ms en consultas)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    
    file.close();
    std::cout << "\n[OK] Log P0171 guardado en " << filename << " (" << count << " registros en " << (time(nullptr)-start) << "s)\n";
}