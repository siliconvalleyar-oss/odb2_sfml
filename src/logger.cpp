/**
 * @file logger.cpp
 * @brief Implementación de Logger (CSV) y SessionLogger (log detallado).
 *
 * Logger:
 *   - Archivo: logs/all_YYYY_MM_DD_HHMM.csv
 *   - Columnas: timestamp, key, value
 *   - Uso: registro de datos de sensores en tiempo real.
 *
 * SessionLogger:
 *   - Archivo: logs/all_YYYY_MM_DD_HHMMSS.log
 *   - Columnas (pipe-separated):
 *     timestamp | type | description | command | hex_response | interpreted
 *   - Tipos: START, OPTION, CMD, RESULT, ERROR, INFO, END
 */

#include "logger.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#ifdef Q_OS_WIN
#include <direct.h>
#endif

Logger::Logger()
{
}

Logger::~Logger()
{
    close();
}

bool Logger::ensureLogsDir()
{
    struct stat st;
    if (stat("logs", &st) != 0) {
        // Crear directorio logs/ (compatible Windows/Linux)
#ifdef Q_OS_WIN
        if (_mkdir("logs") != 0) {
#else
        if (mkdir("logs", 0755) != 0) {
#endif
            std::cerr << "[monitor] No se pudo crear directorio logs/" << std::endl;
            return false;
        }
        std::cout << "[monitor] Directorio logs/ creado" << std::endl;
    }
    return true;
}

bool Logger::open()
{
    if (!ensureLogsDir()) {
        // Fallback: guardar en directorio actual
        filename = "all_debug.log";
        file = std::make_unique<std::ofstream>(filename);
        if (!file->is_open()) return false;
        *file << "timestamp,key,value\n";
        std::cout << "[monitor] " << filename << " (fallback)" << std::endl;
        return true;
    }

    time_t now = time(nullptr);
    tm* t = localtime(&now);

    std::stringstream ss;

    ss << "logs/all_"
       << (t->tm_year + 1900)
       << "_"
       << std::setw(2) << std::setfill('0') << (t->tm_mon + 1)
       << "_"
       << std::setw(2) << std::setfill('0') << t->tm_mday
       << "_"
       << std::setw(2) << std::setfill('0') << t->tm_hour
       << std::setw(2) << std::setfill('0') << t->tm_min
       << std::setw(2) << std::setfill('0') << t->tm_sec;

    filename = ss.str() + ".csv";

    file = std::make_unique<std::ofstream>(filename);

    if (!file->is_open()) {
        std::cerr << "[monitor] No se pudo crear " << filename << std::endl;
        return false;
    }

    *file << "timestamp,key,value\n";
    std::cout << "[monitor] " << filename << std::endl;

    return true;
}

void Logger::log(const std::string& key, const std::string& value)
{
    if (!file || !file->is_open()) return;
    time_t now = time(nullptr);
    *file << now << "," << key << "," << value << std::endl;
    file->flush();
}

void Logger::log(const std::string& key, int value)
{
    log(key, std::to_string(value));
}

void Logger::log(const std::string& key, float value)
{
    log(key, std::to_string(value));
}

void Logger::log(const std::string& key, double value)
{
    log(key, std::to_string(value));
}

void Logger::close()
{
    if (file && file->is_open())
    {
        file->flush();
        file->close();
    }
}

// ============================================================================
// SessionLogger — Implementación
// ============================================================================

SessionLogger::SessionLogger() {}

SessionLogger::~SessionLogger()
{
    close();
}

std::string SessionLogger::timestamp()
{
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", t);
    return std::string(buf);
}

bool SessionLogger::open()
{
    if (!Logger::ensureLogsDir()) {
        filename = "session_debug.log";
        file = std::make_unique<std::ofstream>(filename);
        if (!file->is_open()) return false;
        *file << "timestamp|type|description|command|hex_response|interpreted\n";
        return true;
    }

    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y_%m_%d_%H%M%S", t);
    filename = std::string("logs/all_") + ts + ".log";

    file = std::make_unique<std::ofstream>(filename);
    if (!file->is_open()) {
        std::cerr << "[monitor] No se pudo crear session log: " << filename << std::endl;
        return false;
    }

    *file << "timestamp|type|description|command|hex_response|interpreted\n";
    *file << timestamp() << "|START|Sesión iniciada - ELM327 OBD-II||||\n";
    file->flush();

    std::cout << "[monitor] " << filename << std::endl;
    return true;
}

void SessionLogger::logOption(int option, const std::string& description)
{
    if (!file || !file->is_open()) return;
    *file << timestamp() << "|OPTION|" << description << "|" << option << "|||\n";
    file->flush();
}

void SessionLogger::logCommand(const std::string& cmd, const std::string& hexResp)
{
    if (!file || !file->is_open()) return;
    *file << timestamp() << "|CMD|Comando enviado|" << cmd << "|" << hexResp << "|\n";
    file->flush();
}

void SessionLogger::logResult(const std::string& key, const std::string& value)
{
    if (!file || !file->is_open()) return;
    *file << timestamp() << "|RESULT|" << key << "|||" << value << "\n";
    file->flush();
}

void SessionLogger::logError(const std::string& msg, const std::string& detail)
{
    if (!file || !file->is_open()) return;
    *file << timestamp() << "|ERROR|" << msg << "||" << detail << "|\n";
    file->flush();
}

void SessionLogger::logInfo(const std::string& msg)
{
    if (!file || !file->is_open()) return;
    *file << timestamp() << "|INFO|" << msg << "|||\n";
    file->flush();
}

void SessionLogger::close()
{
    if (file && file->is_open()) {
        *file << timestamp() << "|END|Sesión finalizada||||\n";
        file->flush();
        file->close();
    }
}
