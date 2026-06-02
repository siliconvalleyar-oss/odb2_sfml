/**
 * @file gm_commands.hpp
 * @brief Comandos específicos para vehículos Chevrolet/GM.
 *
 * Proporciona acceso a parámetros del motor usando:
 * - PIDs OBD-II estándar (Modo 01) — funcionan en cualquier vehículo.
 * - PIDs Modo 22 (UDS) — específicos de GM, requieren cabezal CAN 7E0/7E8.
 *
 * Los métodos implementan fallback automático: si el PID OBD estándar
 * no está disponible, se intenta el equivalente en modo 22.
 */

#pragma once
#include "elm327.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstddef>

/**
 * @brief Resultado de un comando GM.
 */
struct GMData {
    std::string command;   ///< Comando enviado (ej: "22 1940")
    std::string response;  ///< Respuesta hexadecimal recibida
    bool success;          ///< true si la respuesta fue exitosa
    std::string errorMsg;  ///< Mensaje de error si success == false
};

// ============================================================================
// GMCommands — Comandos para Chevrolet/GM
// ============================================================================

/**
 * @brief Clase para manejar comandos específicos de vehículos GM.
 *
 * Combina PIDs OBD-II estándar (Modo 01, universales) con PIDs
 * modo 22 (UDS, específicos GM). Cada método intenta primero el
 * PID estándar y cae al modo 22 si no hay respuesta.
 *
 * Los cabezales CAN (AT SH / AT CRA) se configuran automáticamente
 * para comunicación con la ECU del motor en 7E0/7E8.
 *
 * @dot
 * digraph fallback_gm {
 *     rankdir = TB;
 *     node [shape = box, style = "rounded,filled", fontname = "Sans"];
 *
 *     subgraph cluster_method {
 *         label = "getKilometers(), getCatalystTemp(), etc.";
 *         style = dashed;
 *         fillcolor = "#F0F8FF";
 *
 *         obd  [label = "Intentar PID\nOBD-II Estandar\n(01A6 / 013C / etc.)",
 *               fillcolor = "#C6FFC6"];
 *         check1 [label = "¿Respuesta\nvalida?", shape = diamond,
 *                 fillcolor = "#FFFACD"];
 *         result1 [label = "✅ Devolver resultado",
 *                  shape = ellipse, fillcolor = "#90EE90"];
 *
 *         mode22 [label = "Intentar PID\nModo 22 GM\n(F190 / F40C / etc.)",
 *                 fillcolor = "#FFE4B5"];
 *         check2 [label = "¿Respuesta\nvalida?", shape = diamond,
 *                 fillcolor = "#FFFACD"];
 *         result2 [label = "✅ Devolver resultado\n+ '(modo 22)'",
 *                  shape = ellipse, fillcolor = "#90EE90"];
 *         fail   [label = "❌ Mensaje de error\n'No disponible'",
 *                  shape = ellipse, fillcolor = "#FFB5B5"];
 *
 *         obd -> check1;
 *         check1 -> result1 [label = "SI"];
 *         check1 -> mode22 [label = "NO"];
 *         mode22 -> check2;
 *         check2 -> result2 [label = "SI"];
 *         check2 -> fail [label = "NO"];
 *     }
 *
 *     cabezal [label = "setupGMHeader():\nAT SH 7E0\nAT CRA 7E8",
 *              fillcolor = "#E8F0FE"];
 *     nrc [label = "Decodificar NRC:\n7F22 + codigo error\n(ISO 14229-1)",
 *          fillcolor = "#FFDAB9"];
 *
 *     cabezal -> mode22 [style = dotted, arrowhead = none];
 *     mode22 -> nrc [label = "si 7F...", style = dashed];
 * }
 * @enddot
 */
class GMCommands {
public:
    explicit GMCommands(ELM327* elm);
    ~GMCommands();

    // ========== Métodos públicos ==========

    /**
     * @brief Obtiene el kilometraje/odómetro de la ECU.
     * @return String con km (ej: "123456 km") o mensaje de error.
     */
    std::string getKilometers();

    /**
     * @brief Obtiene la temperatura del catalizador.
     * @return String con °C o mensaje de error.
     */
    std::string getCatalystTemp();

    /**
     * @brief Obtiene la presión de combustible del riel.
     * @return String con kPa o mensaje de error.
     */
    std::string getFuelPressure();

    /**
     * @brief Obtiene el torque del motor.
     * @return String con Nm o mensaje de error.
     */
    std::string getEngineTorque();

    /**
     * @brief Obtiene el voltaje de la ECU.
     * @return String con V o mensaje de error.
     */
    std::string getECUVoltage();

    /**
     * @brief Obtiene el historial de códigos pendientes (modo 07 / 19 02).
     * @return String con lista de DTCs pendientes.
     */
    std::string getGMHistory();

    /**
     * @brief Borra el historial de DTCs (modo 04 / 14 FF FF).
     * @return String indicando éxito o error.
     */
    std::string clearGMHistory();

    /**
     * @brief Intenta resetear los adaptativos de la ECU vía modo 22.
     *        Métodos: F10E, F10D, F11C.
     */
    void resetAdaptations();

    /**
     * @brief Obtiene temperatura de transmisión (22 1940).
     * @return String con °C o mensaje de error.
     */
    std::string getTransmissionTemp();

    /**
     * @brief Obtiene temperatura de aceite (015C / 22 1470).
     * @return String con °C o presión en kPa/psi.
     */
    std::string getOilTempDisplay();

    /**
     * @brief Obtiene avance de encendido y detección de knock.
     * @return String con °BTDC, STFT, y análisis de rango.
     */
    std::string getKnockRetardDisplay();

    /**
     * @brief Escanea rangos de PIDs OBD-II estándar (Modo 01).
     *        Consulta PIDs 0100-01C0 para detectar soporte.
     */
    void scanPIDs();

    /**
     * @brief Escanea PIDs modo 22 conocidos de GM.
     *        Prueba 12 PIDs comunes (1940, F190, F40C, etc.).
     */
    void scanGMPIDs();

    /**
     * @brief Envía un comando OBD personalizado y muestra la respuesta.
     * @param cmd Comando completo (ej: "010C", "22 1940").
     * @return Respuesta cruda del ELM327.
     */
    std::string sendCustomCommand(const std::string& cmd);

    /**
     * @brief Envía un comando modo 22 con cabezal CAN configurado.
     * @param pidHex PID en hex (ej: "1940", "F190").
     * @return Datos de respuesta después del header "62+pid",
     *         o vacío si hay error NRC.
     */
    std::string sendMode22(const std::string& pidHex);

    // ========== Decodificadores de respuestas modo 22 ==========

    std::string decodeKilometers(const std::string& hexResponse);
    std::string decodeCatalystTemp(const std::string& hexResponse);
    std::string decodeFuelPressure(const std::string& hexResponse);
    std::string decodeEngineTorque(const std::string& hexResponse);
    std::string decodeECUVoltage(const std::string& hexResponse);
    std::string decodeTransmissionTemp(const std::string& hexResponse);
    std::string decodeOilPressure(const std::string& hexResponse);
    std::string decodeKnockRetard(const std::string& hexResponse);
    
private:
    ELM327* elm;
    
    // Configurar cabezal CAN para modo 22 (7E0/7E8 = ECU motor)
    /** @brief Configura cabezales CAN 7E0/7E8 para modo 22. */
    bool setupGMHeader();

    /** @brief Envía modo 22 con cabezal y parsea respuesta NRC. */
    std::string sendMode22WithHeader(const std::string& pidHex);

    /** @brief Muestra error de comando en stderr. */
    void showError(const std::string& cmd, const std::string& response);
};
