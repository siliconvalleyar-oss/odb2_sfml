/**
 * @file logger.hpp
 * @brief Loggers para la aplicación de diagnóstico OBD-II.
 *
 * Proporciona dos clases de logging:
 * - Logger: Registro CSV de pares clave-valor con timestamp Unix.
 * - SessionLogger: Registro detallado de sesión con opciones, comandos,
 *   respuestas hexadecimales y valores interpretados.
 */

#pragma once

#include <fstream>
#include <memory>
#include <string>

/**
 * @brief Clase para registrar datos en un archivo CSV.
 * 
 * Permite abrir un archivo de log con nombre basado en la fecha/hora,
 * y escribir pares clave-valor junto con una marca de tiempo Unix.
 * Los datos se guardan en formato CSV con columnas: timestamp, key, value.
 */
class Logger
{
private:
    std::unique_ptr<std::ofstream> file; ///< Manejador del archivo de salida.
    std::string filename;                ///< Nombre del archivo de log generado.

public:
    /**
     * @brief Constructor. No abre el archivo automáticamente; debe llamarse a open().
     */
    Logger();

    /**
     * @brief Destructor. Cierra el archivo si estaba abierto.
     */
    ~Logger();

    /**
     * @brief Abre un nuevo archivo de log con nombre auto-generado.
     * 
     * El nombre sigue el patrón: all_YYYY_MM_DD_HHMM.csv
     * La primera línea del archivo contiene los encabezados: timestamp,key,value
     * 
     * @return true si el archivo se abrió correctamente, false en caso contrario.
     */
    bool open();

    /**
     * @brief Asegura que el directorio logs/ exista, lo crea si es necesario.
     * @return true si el directorio existe o se creó correctamente.
     */
    static bool ensureLogsDir();

    /**
     * @brief Registra un par clave-valor como cadena de texto.
     * 
     * @param key   Identificador del dato (ej. "RPM", "Speed").
     * @param value Valor en formato string.
     */
    void log(const std::string& key, const std::string& value);

    /**
     * @brief Registra un par clave-valor como entero.
     * 
     * @param key   Identificador del dato.
     * @param value Valor entero.
     */
    void log(const std::string& key, int value);

    /**
     * @brief Registra un par clave-valor como flotante.
     * 
     * @param key   Identificador del dato.
     * @param value Valor flotante.
     */
    void log(const std::string& key, float value);

    /**
     * @brief Registra un par clave-valor como double.
     * 
     * @param key   Identificador del dato.
     * @param value Valor double.
     */
    void log(const std::string& key, double value);

    /**
     * @brief Cierra el archivo de log si está abierto.
     *
     * También vacía el buffer y libera los recursos.
     */
    void close();
};

// ============================================================================
// SessionLogger — Logger detallado de sesión
// ============================================================================

/**
 * @brief Logger de sesión: registra opciones, comandos, respuestas hex y valores interpretados.
 *
 * Formato del archivo (pipe-separated):
 *   YYYYMMDD_HHMMSS | TIPO | DESCRIPCIÓN | COMANDO | HEX_RESPONSE | VALOR_INTERPRETADO
 *
 * Tipos de registro:
 *   START  - Inicio de sesión
 *   OPTION - Opción del menú ejecutada
 *   CMD    - Comando OBD enviado + respuesta hexadecimal
 *   RESULT - Resultado interpretado
 *   ERROR  - Error de comunicación
 *   INFO   - Mensaje informativo
 *   END    - Fin de sesión
 *
 * Ubicación: logs/all_YYYY_MM_DD_HHMMSS.log
 */
class SessionLogger
{
private:
    std::unique_ptr<std::ofstream> file;
    std::string filename;
    
    std::string timestamp();

public:
    SessionLogger();
    ~SessionLogger();

    /**
     * @brief Abre archivo de sesión en logs/all_YYYY_MM_DD_HHMMSS.log
     */
    bool open();

    /**
     * @brief Registrar opción seleccionada en el menú
     */
    void logOption(int option, const std::string& description);

    /**
     * @brief Registrar comando OBD enviado y su respuesta hexadecimal cruda
     * @param cmd       El comando PID enviado (ej: "010C")
     * @param hexResp   Respuesta hexadecimal sin procesar
     */
    void logCommand(const std::string& cmd, const std::string& hexResp);

    /**
     * @brief Registrar resultado interpretado de una lectura
     * @param key   Nombre del valor (ej: "RPM", "STFT_B1")
     * @param value Valor interpretado en texto (ej: "750 RPM", "-0.8%")
     */
    void logResult(const std::string& key, const std::string& value);

    /**
     * @brief Registrar mensaje de error
     */
    void logError(const std::string& msg, const std::string& detail);

    /**
     * @brief Registrar mensaje informativo arbitrario
     */
    void logInfo(const std::string& msg);

    /**
     * @brief Cierra el archivo
     */
    void close();

    bool isOpen() const { return file && file->is_open(); }
};
