/**
 * @file elm327.hpp
 * @brief Interfaz principal para comunicación con el adaptador ELM327.
 *
 * Proporciona métodos para:
 * - Conexión Bluetooth (RFCOMM) con el adaptador ELM327.
 * - Envío de comandos AT y PIDs OBD-II (Modos 01 a 0A).
 * - Lectura de parámetros del motor (RPM, velocidad, temperatura, etc.).
 * - Detección y recuperación automática del estado STOPPED.
 * - Timeout adaptativo para evitar saturación del bus CAN.
 * - Escaneo multi-módulo (Auto-Scan) y Modo 09 extendido.
 *
 * @note Requiere Bluetooth RFCOMM (librería bluez).
 * @see https://www.elmelectronics.com/help/obd/tips/
 */

#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>

// Forward declaration
class SessionLogger;

/**
 * @brief Datos de un sensor de oxígeno.
 */
struct OxygenSensor {
    int bank;              ///< Banco del motor (1 o 2)
    int sensor;            ///< Número de sensor (1-8)
    double voltage;        ///< Voltaje del sensor (V), -1 si no disponible
    double shortTermTrim;  ///< Fuel trim de corto plazo (%), -999 si error
};

/**
 * @brief Código de error DTC (Diagnostic Trouble Code).
 */
struct DTCCode {
    std::string code;         ///< Código alfanumérico (ej: "P0171")
    std::string description;  ///< Descripción con tipo (Powertrain, Chasis, etc.)
};

/**
 * @brief Fuel trims de todos los bancos.
 */
struct FuelTrim {
    double shortTermBank1;  ///< STFT Banco 1 (%)
    double shortTermBank2;  ///< STFT Banco 2 (%)
    double longTermBank1;   ///< LTFT Banco 1 (%)
    double longTermBank2;   ///< LTFT Banco 2 (%)
    bool available;         ///< true si al menos un trim fue leído
};

// ============================================================================
// ELM327 — Clase principal de comunicación
// ============================================================================

/**
 * @brief Interfaz de alto nivel para el adaptador ELM327 vía Bluetooth.
 *
 * Esta clase maneja la conexión RFCOMM, el envío/recepción de comandos
 * OBD-II y AT, y el parseo de respuestas hexadecimales.
 *
 * Características clave:
 * - Conexión Bluetooth automática por dirección MAC.
 * - Timeout adaptativo: aumenta la espera tras detectar STOPPED.
 * - Recuperación automática de errores de bus CAN.
 * - Soporte para Modos 01-0A del estándar OBD-II.
 * - Dashboard rápido multiparámetro (<3s).
 * - Auto-Scan de módulos CAN (ECU, ABS, Airbag, BCM, etc.).
 *
 * @dot
 * digraph conexion_bt {
 *     rankdir = TB;
 *     node [shape = box, style = "rounded,filled", fillcolor = "#E8F0FE"];
 *     fontname = "Sans";
 *
 *     init   [label = "ELM327(mac, channel)", fillcolor = "#C6E2FF"];
 *     sock   [label = "socket(AF_BLUETOOTH,\nSOCK_STREAM, BTPROTO_RFCOMM)"];
 *     addr   [label = "str2ba(mac) → addr.rc_bdaddr"];
 *     conn   [label = "connect(sock, &addr)", fillcolor = "#FFE4B5"];
 *     atz    [label = "ATZ (Reset)"];
 *     config [label = "ATE0 ATL0 ATS0 ATSP0\nATAT1 ATST20", fillcolor = "#C6FFC6"];
 *     ok     [label = "✅ Conectado", shape = ellipse, fillcolor = "#90EE90"];
 *
 *     init -> sock -> addr -> conn -> atz -> config -> ok;
 *
 *     fail   [label = "❌ Error", shape = ellipse, fillcolor = "#FFB5B5"];
 *     sock  -> fail [label = "socket() < 0", style = dashed, color = red];
 *     conn  -> fail [label = "connect() < 0", style = dashed, color = red];
 * }
 * @enddot
 *
 * @dot
 * digraph recovery {
 *     rankdir = TB;
 *     node [shape = box, style = "rounded,filled", fillcolor = "#FFECD2"];
 *     fontname = "Sans";
 *
 *     send   [label = "send() / sendRaw()", fillcolor = "#E8F0FE"];
 *     check  [label = "¿Respuesta contiene\n'STOPPED'?", shape = diamond,
 *             fillcolor = "#FFFACD"];
 *     normal [label = "✅ Comando OK\n+ success decay", shape = ellipse,
 *             fillcolor = "#90EE90"];
 *
 *     subgraph cluster_recovery {
 *         label = "recoverFromStopped()";
 *         style = dashed;
 *         fillcolor = "#FFF0F0";
 *
 *         wake  [label = "Enviar retorno\nde carro (wake)"];
 *         atd   [label = "ATD (Defaults)"];
 *         atz   [label = "ATZ (fallback)", fillcolor = "#FFB5B5"];
 *         flush [label = "Flush buffer\nselect() x5"];
 *         reconf [label = "Reconfigurar\nATE0 ATH0 ATS0\nATSP0 ATAT1"];
 *
 *         wake -> atd;
 *         atd  -> atz [label = "si ATD falla", style = dashed];
 *         atz  -> flush;
 *         atd  -> flush [label = "si ATD ok"];
 *         flush -> reconf;
 *     }
 *
 *     penalty [label = "Penalidad += 100ms\n(max 1000ms)",
 *              fillcolor = "#FFB5B5"];
 *     retry  [label = "Reintentar comando\ncon penalidad",
 *             fillcolor = "#FFE4B5"];
 *     decay  [label = "10 exitos →\nPenalidad -= 25ms\n→ 0ms",
 *             fillcolor = "#C6FFC6"];
 *
 *     send   -> check;
 *     check  -> normal [label = "NO"];
 *     check  -> penalty [label = "SÍ"];
 *     penalty -> reconf [style = dotted, arrowhead = none];
 *     reconf -> retry;
 *     retry  -> send [style = dashed, label = "recursivo"];
 *     normal -> decay [label = "si penalty > 0", style = dotted];
 * }
 * @enddot
 *
 * @note Los métodos devuelven -1 o "No disponible" ante errores.
 */
class ELM327
{
public:
    ELM327(const std::string& mac, int channel = 1);
    ~ELM327();

    bool connectBT();
    void disconnect();
    
    bool isConnected() { return sock >= 0; }
    bool isOnline() { return sock >= 0; }

    std::string send(const std::string& cmd, int delayMs = 200);

    /**
     * @brief Envío raw con select() timeout. Usado por comandos modo 22.
     * @param cmd Comando a enviar (sin \r).
     * @param timeoutMs Timeout base en ms (se suma penalidad adaptativa).
     * @return Respuesta cruda del ELM327.
     */
    std::string sendRaw(const std::string& cmd, int timeoutMs = 500);

    // ========== Parámetros básicos del motor ==========

    /** @brief RPM del motor (PID 010C). @return RPM, -1 si error. */
    int getRPM();
    /** @brief Velocidad del vehículo (PID 010D). @return km/h, -1 si error. */
    int getSpeed();
    /** @brief Alias de getCoolantTemp(). */
    int getTemp();
    /** @brief Temperatura del refrigerante (PID 0105). @return °C, -1 si error. */
    int getCoolantTemp();
    /** @brief Carga del motor (PID 0104). @return % (0-100), -1 si error. */
    int getEngineLoad();
    /** @brief Posición del acelerador (PID 0111). @return % (0-100), -1 si error. */
    double getThrottlePosition();
    /** @brief Presión absoluta del múltiple de admisión (PID 010B). @return kPa, -1 si error. */
    double getIntakePressure();
    /** @brief Temperatura del aire de admisión (PID 010F). @return °C, -1 si error. */
    int getIntakeTemp();
    /** @brief Avance de encendido (PID 010E). @return ° antes del TDC (-64 a 64), -1 si error. */
    double getTimingAdvance();
    /** @brief Presión de combustible (PID 010A). @return kPa (×3), -1 si error. */
    double getFuelPressure();
    /** @brief Flujo de aire MAF (PID 0110). @return g/s, -1 si error. */
    double getMAF();
    /** @brief Nivel de combustible (PID 012F). @return % (0-100), -1 si error. */
    double getFuelLevel();
    /** @brief Temperatura ambiente (PID 0146). @return °C, -1 si error. */
    double getAmbientTemp();
    /** @brief Temperatura de aceite (PID 015C). @return °C, -1 si error. */
    double getOilTemp();
    /** @brief EGR comandada (PID 012C). @return % (0-100), -1 si error. */
    double getCommandedEGR();
    /** @brief Error EGR (PID 012D). @return % (-100 a 100), -1 si error. */
    double getEGRError();
    /** @brief Presión del sistema EVAP (PID 0132). @return Pa (×4), -1 si error. */
    double getEVAPPressure();
    /** @brief Presión barométrica (PID 0133). @return kPa, -1 si error. */
    double getBarometricPressure();
    
    // ========== Fuel Trims ==========

    double getShortTermTrimBank1();
    double getShortTermTrimBank2();
    double getLongTermTrimBank1();
    double getLongTermTrimBank2();
    /** @brief Obtiene todos los fuel trims en una llamada. */
    FuelTrim getAllFuelTrims();

    // ========== Sensores de oxígeno ==========

    /** @brief Obtiene todos los sensores O2 disponibles (PIDs 0114-0117). */
    std::vector<OxygenSensor> getOxygenSensors();
    /** @brief Obtiene un sensor O2 específico por banco y número. */
    OxygenSensor getO2Sensor(int bank, int sensor);

    // ========== DTCs ==========

    std::vector<DTCCode> getDTCs();
    bool clearDTCs();

    // ========== Diagnóstico ==========

    std::string getProtocol();
    std::string getVehicleInfo();
    std::string getVIN();
    bool checkMIL();
    bool setProtocol(int protocol);
    bool resetELM();
    std::string sendCommand(const std::string& cmd);
    void setSessionLogger(SessionLogger* logger) { m_sessionLog = logger; }

    // ========== Recuperación de errores ==========

    bool recoverFromStopped();
    void ensureNormalConfig();

    // ========== Logging ==========

    void logAllSensorsRaw(const std::string& filename);
    void logP0171Diagnostic(const std::string& filename, int durationSec = 60);

    // ========== PIDs adicionales GM ==========

    int getCatalystTempB1S1();        ///< PID 013C — Temp. catalizador
    double getControlModuleVoltage(); ///< PID 0142 — Voltaje módulo control
    double getEngineTorqueStandard(); ///< PID 0163 — Torque motor
    int getOdometer();                ///< PID 01A6 — Odómetro
    std::vector<DTCCode> getPendingDTCs();    ///< Modo 07
    std::vector<DTCCode> getPermanentDTCs();  ///< Modo 0A
    bool clearPendingDTCs();

    // ========== Dashboard rápido ==========

    /**
     * @brief Datos comprimidos del motor para dashboard en tiempo real.
     * Se leen 10 PIDs individuales en ~3s.
     */
    struct DashboardData {
        int rpm;
        int speed;
        int coolant;
        int load;
        double throttle;
        double maf;
        double fuelLevel;
        double timing;
        double intakeTemp;
        int intakePressure;
        bool valid;  ///< true si al menos RPM fue leído
    };
    DashboardData getDashboardFast();

    // ========== Modos OBD-II avanzados ==========

    std::string getFreezeFrame();          ///< Modo 02
    std::string getMonitorTests();          ///< Modo 06
    std::string getTestResults();           ///< Modo 06 alias
    bool requestO2Test(int bank, int sensor); ///< Modo 08
    std::string getECUName();               ///< Modo 09
    std::string getCalibrationID();         ///< Modo 09 (CVN)
    std::string getCalibrationDate();       ///< Modo 09
    std::string getPerformanceTracking();   ///< Modo 09

    // ========== Auto-Scan ==========

    struct ModuleScanResult {
        int id;                       ///< ID CAN del módulo (ej: 0x7E0)
        std::string name;             ///< Nombre descriptivo (ej: "ECU Motor")
        bool responds;                ///< true si responde a PID 0100
        std::vector<DTCCode> dtcs;    ///< DTCs encontrados en el módulo
    };
    std::vector<ModuleScanResult> autoScan();

    // ========== Utilidades ==========

    std::vector<std::string> splitResponse(const std::string& response);
    int getSock() const { return sock; }
    int getStoppedPenaltyMs() const { return m_stoppedPenaltyMs; }
    int getStoppedCount() const { return m_stoppedCount; }

private:
    int sock;                       ///< Socket RFCOMM Bluetooth
    std::string mac;                ///< Dirección MAC del ELM327
    int channel;                    ///< Canal RFCOMM (usualmente 1)
    bool m_stoppedRecovery;         ///< Flag anti-recursión para STOPPED
    int m_stoppedPenaltyMs;         ///< Penalidad adaptativa (0-1000ms)
    int m_consecutiveSuccess;       ///< Comandos exitosos consecutivos
    int m_stoppedCount;             ///< Total de STOPPED detectados
    SessionLogger* m_sessionLog;    ///< Logger de sesión (no owned)

    std::string readRaw();
    std::string parseResponse(const std::string& response, const std::string& expected);
    std::string decodeDTCCode(const std::string& code);
};
