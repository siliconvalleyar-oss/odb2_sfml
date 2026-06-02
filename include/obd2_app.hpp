/**
 * @file obd2_app.hpp
 * @brief Aplicación principal del escáner OBD-II profesional (Qt5).
 *
 * Esta clase implementa un escáner de diagnóstico automotriz completo
 * con interfaz de menú interactivo por terminal e interfaz gráfica Qt5.
 * Soporta:
 *
 * - Lectura de parámetros del motor (RPM, velocidad, temperaturas, etc.)
 * - Códigos de error DTC (leer, borrar, pendientes, permanentes)
 * - Datos en vivo: dashboard, gráficos, osciloscopio O2 vía Qt5
 * - Servicios especiales: Oil Reset, EPB, BMS, SAS, DPF
 * - Funciones avanzadas GM: adaptativos, kilometraje, historial
 * - Auto-Scan: escaneo completo de módulos CAN
 * - Freeze Frame (Modo 02), Monitoreo OBD (Modo 06)
 * - Logging CSV y session logging detallado
 * - Timeout adaptativo para evitar saturación del bus CAN
 *
 * @note Requiere Qt5 (qtbase5-dev) y libbluetooth-dev.
 */

#pragma once

#include "elm327.hpp"
#include "gm_commands.hpp"
#include "logger.hpp"
#include "qt_display.hpp"

#include <QApplication>
#include <QCoreApplication>

#include <iostream>
#include <iomanip>
#include <limits>
#include <memory>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <ctime>
#include <cstring>
#include <vector>

// Variable global para controlar el logger (manejada por señal)
/**
 * @brief Variable global que controla el logger cíclico.
 * Se pone a false vía signalHandler() cuando se presiona Ctrl+C.
 */
static std::atomic<bool> g_runningLogger(true);

/**
 * @brief Manejador de señal SIGINT (Ctrl+C).
 * Detiene el logger cíclico de forma segura.
 */
static void signalHandler(int /*sig*/) {
    g_runningLogger = false;
}

namespace OBD {

// ============================================================================
// OBD::App — Aplicación de diagnóstico OBD-II
// ============================================================================

/**
 * @brief Clase principal del escáner de diagnóstico OBD-II.
 *
 * Encapsula toda la lógica de la aplicación: menú interactivo,
 * conexión Bluetooth, ejecución de diagnósticos, logging, y
 * gestión de errores.
 *
 * Flujo de uso:
 * 1. Constructor: inicializa tiempo de muestreo.
 * 2. run(): conecta con ELM327, entra en bucle de menú.
 * 3. processOption(): ejecuta la opción seleccionada.
 * 4. Las opciones continuas (dashboard, gráfico, etc.) usan
 *    g_runningLogger para detenerse con Ctrl+C.
 * 5. Las opciones gráficas (41-43) abren ventanas Qt5 con
 *    QPainter para visualización en tiempo real.
 *
 * @dot
 * digraph menu_flow {
 *     rankdir = TB;
 *     node [shape = box, style = "rounded,filled", fontname = "Sans"];
 *     compound = true;
 *
 *     init [label = "App()\nrun()", fillcolor = "#C6E2FF"];
 *
 *     subgraph cluster_setup {
 *         label = "Configuracion";
 *         style = filled;
 *         fillcolor = "#F0F8FF";
 *         log  [label = "Crear Logger\ny SessionLogger"];
 *         bt   [label = "Conectar ELM327\nvia Bluetooth"];
 *         proto [label = "Detectar\nProtocolo CAN"];
 *         gm   [label = "Crear\nGMCommands"];
 *         log -> bt -> proto -> gm;
 *     }
 *
 *     menu [label = "Menu Principal\n(ciclo while)", fillcolor = "#FFD700",
 *           shape = rect, style = "rounded,filled,bold"];
 *
 *     subgraph cluster_options {
 *         label = "processOption(1..43)";
 *         style = dashed;
 *         fillcolor = "#FFFAF0";
 *
 *         sistema  [label = "1-6: Sistemas\n(AutoScan, Motor,\nTCM, ABS, Airbag)"];
 *         live     [label = "7-13: Datos Vivo\n(Dashboard, Grafico,\nLogger, O2 Scope)"];
 *         dtc      [label = "14-20: DTCs\n(Leer, Borrar,\nFreeze, Modo 06)"];
 *         service  [label = "21-28: Servicios\n(Oil, EPB, BMS,\nSAS, DPF, ABS)"];
 *         gm_func  [label = "29-36: GM Avanzado\n(Adaptativos, Temp,\nTorque, Voltaje)"];
 *         info     [label = "37-40: Info y\nHerramientas"];
 *         qt       [label = "41-43: Qt5\n(Dashboard, Graficos,\nOsciloscopio)", fillcolor = "#E8D0FF"];
 *     }
 *
 *     salir [label = "0: Salir", shape = ellipse,
 *            fillcolor = "#FFB5B5"];
 *
 *     init -> menu [label = "ingresa"];
 *     menu -> sistema;
 *     menu -> live;
 *     menu -> dtc;
 *     menu -> service;
 *     menu -> gm_func;
 *     menu -> info;
 *     menu -> qt;
 *     menu -> salir [label = "corta ciclo"];
 *     salir -> init [style = dotted, arrowhead = none,
 *                    label = "fin"];
 * }
 * @enddot
 */
class App {
public:
    /** @brief Constructor. Inicializa tiempo de muestreo a 4s. */
    App() : m_sampleTimeSec(4) {}

    /**
     * @brief Inicia la aplicación: conecta, configura y entra en bucle.
     *
     * Pasos:
     * 1. Limpia pantalla y muestra cabecera.
     * 2. Crea Logger y SessionLogger.
     * 3. Conecta al ELM327 por Bluetooth.
     * 4. Detecta protocolo CAN.
     * 5. Entra en bucle principal mostrando el menú.
     * 6. Ejecuta opciones hasta que el usuario elija "Salir".
     */
    void run() {
        clearScreen();
        printHeader();

        m_logger = std::make_unique<Logger>();
        if (!m_logger->open()) {
            std::cerr << "No se pudo crear archivo de log\n";
        }

        // Crear session logger (registra toda la sesión: opciones, comandos, hex, interpretado)
        m_sessionLog = std::make_unique<SessionLogger>();
        if (!m_sessionLog->open()) {
            std::cerr << "No se pudo crear archivo de session log\n";
        }

        std::string mac = "00:1D:A5:07:23:6E";
        std::cout << "Dirección MAC del ELM327 (ej: 00:1D:A5:07:23:6E): ";
        std::cout << mac << " (usando por defecto)\n";

        m_elm = std::make_unique<ELM327>(mac);
        
        // Asignar session logger al ELM327 para que loguee automáticamente
        m_elm->setSessionLogger(m_sessionLog.get());
        
        if (!m_elm->connectBT()) {
            std::cerr << "\n❌ No se pudo conectar al ELM327\n";
            std::cerr << "Verifique:\n";
            std::cerr << "  - El dispositivo Bluetooth está encendido\n";
            std::cerr << "  - La dirección MAC es correcta\n";
            std::cerr << "  - El ELM327 está emparejado con el sistema\n";
            if (m_sessionLog) m_sessionLog->logError("Conexión fallida", mac);
            return;
        }
        
        if (m_sessionLog) m_sessionLog->logInfo("Conexión establecida: " + mac);

        std::cout << "\n✅ Conexión establecida correctamente\n";
        std::string proto = m_elm->getProtocol();
        std::cout << "📊 Protocolo detectado: " << proto << "\n\n";

        m_gm = std::make_unique<GMCommands>(m_elm.get());

        std::signal(SIGINT, ::signalHandler);

        bool running = true;
        int option;
        while (running) {
            printMenu();
            std::cin >> option;

            if (std::cin.fail()) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Opción inválida\n";
                continue;
            }

            running = processOption(option);

            if (running && option != 0 && !isContinuousOption(option)
                && option < 41) { // Options 41-43 (Qt) are self-contained windows
                std::cout << "\nPresione Enter para continuar...";
                std::cin.ignore();
                std::cin.get();
                clearScreen();
                printHeader();
                std::cout << "Protocolo: " << m_elm->getProtocol() << "\n\n";
            }
        }

        std::cout << "✅ Programa finalizado\n";
    }

private:
    int m_sampleTimeSec;                    ///< Tiempo entre muestras (segundos)
    std::unique_ptr<ELM327> m_elm;          ///< Conexión con el adaptador ELM327
    std::unique_ptr<GMCommands> m_gm;       ///< Comandos específicos GM
    std::unique_ptr<Logger> m_logger;       ///< Logger CSV de datos
    std::unique_ptr<SessionLogger> m_sessionLog; ///< Logger de sesión detallado

    // ========== Utilidades de pantalla ==========

    /** @brief Limpia la terminal usando código de escape ANSI. */
    void clearScreen() const {
        std::cout << "\033[2J\033[1;1H";
    }

    /** @brief Muestra el encabezado de la aplicación. */
    void printHeader() const {
        std::cout << "========================================\n";
        std::cout << "   ELM327 OBD-II DIAGNÓSTICO VEHICULAR\n";
        std::cout << "========================================\n\n";
    }

    /**
     * @brief Obtiene el título descriptivo de una opción del menú.
     * @param option Número de opción (1-43).
     * @return Título descriptivo, o vacío si no existe.
     */
    std::string getOptionTitle(int option) const {
        static const char* titles[] = {
            "",
            "AUTO SCAN - ESCANEO COMPLETO",
            "MOTOR / PCM - PARAMETROS",
            "TRANSMISION / TCM",
            "ABS / FRENOS",
            "AIRBAG / SRS",
            "BCM / CARROCERIA",
            "DASHBOARD TIEMPO REAL",
            "TODOS LOS SENSORES (LISTADO)",
            "GRAFICO MULTIPARAMETRO",
            "OSCILOSCOPIO O2",
            "LOGGER MOTOR (CSV)",
            "LOG COMPLETO SENSORES",
            "DIAGNOSTICO P0171",
            "LEER CODIGOS DE ERROR",
            "BORRAR CODIGOS DE ERROR",
            "CODIGOS PENDIENTES (MODO 07)",
            "CODIGOS PERMANENTES (MODO 0A)",
            "DATOS DE CONGELACION (FREEZE FRAME)",
            "MONITOREO INTERNO OBD (MODO 06)",
            "PRUEBA SENSOR O2 (MODO 08)",
            "RESET DE ACEITE (OIL LIFE)",
            "FRENO DE MANO ELECTRICO (EPB)",
            "REGISTRO DE BATERIA (BMS)",
            "SENSOR ANGULO DIRECCION (SAS)",
            "FILTRO DE PARTICULAS (DPF)",
            "REAPRENDIZAJE ACELERADOR",
            "PURGA ABS / CAMBIO PASTILLAS",
            "CODIFICACION DE INYECTORES",
            "RESET ADAPTATIVOS GM",
            "KILOMETRAJE ECU (GM)",
            "TEMPERATURA CATALIZADOR",
            "PRESION COMBUSTIBLE",
            "TORQUE MOTOR",
            "VOLTAJE ECU",
            "TEMPERATURA TRANSMISION",
            "HISTORIAL ERRORES GM",
            "INFORMACION DEL VEHICULO",
            "DETECTAR MODULOS CAN",
            "COMANDO PERSONALIZADO",
            "CONFIGURACION AVANZADA",
            "DASHBOARD QT5 (VENTANA GRAFICA)",
            "GRAFICOS QT5 (LINEAS)",
            "OSCILOSCOPIO O2 QT5",
            "DEMO QT5 (DATOS SIMULADOS SIN ELM327)"
        };
        if (option >= 1 && option <= 44) return titles[option];
        return "";
    }
    
    /// Imprime el título de la opción seleccionada
    void printOptionTitle(int option) const {
        std::string title = getOptionTitle(option);
        if (!title.empty()) {
            std::cout << "\n========== ► " << title << " ==========\n\n";
        }
    }

    void printMenu() const {
        // Mostrar estado de penalidad adaptativa si está activa
        if (m_elm && m_elm->getStoppedPenaltyMs() > 0) {
            std::cout << "[!] Penalidad: +" << m_elm->getStoppedPenaltyMs() << "ms "
                      << "(" << m_elm->getStoppedCount() << " STOPPED)\n";
        }
        std::cout << "\n================== ESCANER PROFESIONAL OBD2 ==================\n";
        std::cout << "\n--- SISTEMAS DEL VEHICULO ---\n";
        std::cout << "  1. Auto Scan (Escaneo Completo)\n";
        std::cout << "  2. Motor / PCM (Parametros)\n";
        std::cout << "  3. Transmision / TCM\n";
        std::cout << "  4. ABS / Frenos\n";
        std::cout << "  5. Airbag / SRS\n";
        std::cout << "  6. BCM / Carroceria\n";
        std::cout << "\n--- DATOS EN VIVO ---\n";
        std::cout << "  7. Dashboard Tiempo Real\n";
        std::cout << "  8. Todos los Sensores\n";
        std::cout << "  9. Grafico Multiparametro\n";
        std::cout << " 10. Osciloscopio O2\n";
        std::cout << " 11. Logger Motor (CSV)\n";
        std::cout << " 12. Log Completo Sensores\n";
        std::cout << " 13. Diagnostico P0171\n";
        std::cout << "\n--- CODIGOS DE ERROR DTC ---\n";
        std::cout << " 14. Leer Codigos de Error\n";
        std::cout << " 15. Borrar Codigos de Error\n";
        std::cout << " 16. Codigos Pendientes (Modo 07)\n";
        std::cout << " 17. Codigos Permanentes (Modo 0A)\n";
        std::cout << " 18. Datos de Congelacion (Freeze Frame)\n";
        std::cout << " 19. Monitoreo Interno OBD (Modo 06)\n";
        std::cout << " 20. Prueba Sensor O2 (Modo 08)\n";
        std::cout << "\n--- SERVICIOS ESPECIALES ---\n";
        std::cout << " 21. Reset de Aceite (Oil Life)\n";
        std::cout << " 22. Freno Mano Electrico (EPB)\n";
        std::cout << " 23. Registro de Bateria (BMS)\n";
        std::cout << " 24. Sensor Angulo Direccion (SAS)\n";
        std::cout << " 25. Filtro Particulas (DPF)\n";
        std::cout << " 26. Reaprendizaje Acelerador\n";
        std::cout << " 27. Purga ABS / Cambio Pastillas\n";
        std::cout << " 28. Codificacion de Inyectores\n";
        std::cout << "\n--- FUNCIONES AVANZADAS GM ---\n";
        std::cout << " 29. Reset Adaptativos ECU\n";
        std::cout << " 30. Kilometraje ECU (GM)\n";
        std::cout << " 31. Temperatura Catalizador\n";
        std::cout << " 32. Presion Combustible\n";
        std::cout << " 33. Torque Motor\n";
        std::cout << " 34. Voltaje ECU\n";
        std::cout << " 35. Temperatura Transmision\n";
        std::cout << " 36. Historial Errores GM\n";
        std::cout << "\n--- INFORMACION Y HERRAMIENTAS ---\n";
        std::cout << " 37. Informacion del Vehiculo (VIN/MIL)\n";
        std::cout << " 38. Detectar Modulos CAN\n";
        std::cout << " 39. Enviar Comando Personalizado\n";
        std::cout << " 40. Configuracion Avanzada\n";
        std::cout << "\n--- INTERFAZ GRAFICA QT5 ---\n";
        std::cout << " 41. Dashboard Qt5 (Grafico)\n";
        std::cout << " 42. Graficos Qt5 (Lineas)\n";
        std::cout << " 43. Osciloscopio O2 Qt5\n";
        std::cout << " 44. Demo Qt5 (Datos Simulados, sin ELM327)\n";
        std::cout << "\n 0. Salir\n";
        std::cout << "=============================================================\n";
        std::cout << "Opcion: ";
        std::cout.flush();
    }

    bool isContinuousOption(int option) const {
        return (option == 7 || option == 9 || option == 10 || option == 11 || option == 13);
    }

    bool processOption(int option) {
        printOptionTitle(option);
        
        // Session log: registrar opcion ejecutada
        if (m_sessionLog) {
            std::string title = getOptionTitle(option);
            if (!title.empty()) {
                m_sessionLog->logOption(option, title);
            }
        }

        switch (option) {
            // === SISTEMAS DEL VEHICULO ===
            case 1:  showAutoScan(); break;
            case 2:  showEngineParams(); break;
            case 3:  readModule("TCM", "7E1"); break;
            case 4:  readModule("ABS", "7E2"); break;
            case 5:  readModule("AIRBAG", "7E4"); break;
            case 6:  readModule("BCM", "7E6"); break;
            // === DATOS EN VIVO ===
            case 7:  liveDashboard(); break;
            case 8:  showAllSensors(); break;
            case 9:  showGraphMode(); break;
            case 10: oxygenSensorScope(); break;
            case 11: engineLogger(); break;
            case 12: fullSensorsLog(); break;
            case 13: p0171DiagnosticLog(); break;
            // === DTCs ===
            case 14: showDTCs(); break;
            case 15: clearDTCs(); break;
            case 16: showPendingDTCs(); break;
            case 17: showPermanentDTCs(); break;
            case 18: showFreezeFrame(); break;
            case 19: showMonitorTests(); break;
            case 20: showO2Test(); break;
            // === SERVICIOS ESPECIALES ===
            case 21: showServiceStub("RESET DE ACEITE (OIL LIFE)",
                "Procedimiento OBD2 estandar no disponible via ELM327.\n"
                "  Requiere escaner profesional (Autel, Launch, Tech2).\n"
                "  Metodo manual: 1) Girar llave ON sin arrancar.\n"
                "  2) Presionar pedal acelerador 3 veces.\n"
                "  3) Mantener 10 segundos. 4) Apagar y encender."); break;
            case 22: showServiceStub("FRENO DE MANO ELECTRICO (EPB)",
                "Requiere control bidireccional no disponible via ELM327.\n"
                "  Para cambiar pastillas traseras con EPB:\n"
                "  1. Conectar escaner profesional (Autel, Launch).\n"
                "  2. Seleccionar EPB Service Mode.\n"
                "  3. Seguir instrucciones del escaner."); break;
            case 23: showServiceStub("REGISTRO DE BATERIA (BMS)",
                "Registro de bateria nueva en vehiculos con BMS.\n"
                "  No disponible via ELM327 basico.\n"
                "  Requiere: Autel, Launch, o herramienta especifica BMS.\n"
                "  Tras cambiar bateria, registrar con escaner profesional."); break;
            case 24: showServiceStub("SENSOR ANGULO DIRECCION (SAS)",
                "Calibracion del sensor de angulo de direccion.\n"
                "  No disponible via ELM327 basico.\n"
                "  Requiere escaner profesional.\n"
                "  Metodo manual (algunos vehiculos): girar volante\n"
                "  completamente a izquierda, luego derecha, y centrar."); break;
            case 25: showServiceStub("FILTRO DE PARTICULAS (DPF)",
                "Regeneracion forzada del filtro de particulas diesel.\n"
                "  Requiere escaner profesional con funciones DPF.\n"
                "  No disponible via ELM327 basico.\n"
                "  Consultar manual del vehiculo para regeneracion en ruta."); break;
            case 26:
                // Intentar reset via modo 22 F10E primero
                std::cout << "Intentando reset de adaptativos...\n";
                m_gm->resetAdaptations();
                showServiceStub("REAPRENDIZAJE ACELERADOR",
                "Si el reset via OBD2 no funciona, metodo manual:\n"
                "  1. Girar llave ON (no arrancar) por 10 segundos.\n"
                "  2. Apagar llave por 10 segundos.\n"
                "  3. Arrancar y dejar ralentí 5 minutos.\n"
                "  4. Conducir suavemente hasta que el PCM reaprenda.");
                break;
            case 27: showServiceStub("PURGA ABS / CAMBIO PASTILLAS",
                "Sangrado del sistema de frenos ABS.\n"
                "  Requiere control bidireccional de valvulas ABS.\n"
                "  No disponible via ELM327 basico.\n"
                "  Requiere escaner profesional con funcion de sangrado ABS."); break;
            case 28: showServiceStub("CODIFICACION DE INYECTORES",
                "Codificacion de inyectores tras reemplazo.\n"
                "  Requiere escaner profesional o herramienta especifica.\n"
                "  No disponible via ELM327 basico.\n"
                "  Los codigos IMA/IVA se programan con escaner."); break;
            // === FUNCIONES AVANZADAS GM ===
            case 29: showResetAdaptationsMenu(); break;
            case 30: showGMResult("Kilometraje ECU", m_gm->getKilometers()); break;
            case 31: showGMResult("Temp Catalizador", m_gm->getCatalystTemp()); break;
            case 32: showGMResult("Presion Combustible", m_gm->getFuelPressure()); break;
            case 33: showGMResult("Torque Motor", m_gm->getEngineTorque()); break;
            case 34: showGMResult("Voltaje ECU", m_gm->getECUVoltage()); break;
            case 35: showGMResult("Temp Transmision", m_gm->getTransmissionTemp()); break;
            case 36: showGMResult("Historial GM", m_gm->getGMHistory()); break;
            // === INFORMACION Y HERRAMIENTAS ===
            case 37: showVehicleInfo(); break;
            case 38: detectModules(); break;
            case 39: sendCustomCommand(); break;
            case 40: showConfigurationMenu(); break;
            // === INTERFAZ GRAFICA QT5 ===
            case 41: {
                QtDisplay display(m_elm.get());
                display.show();
                QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
                while (app && display.isVisible()) {
                    app->processEvents(QEventLoop::AllEvents);
                    app->sendPostedEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
                break;
            }
            case 42: {
                QtDisplay display(m_elm.get());
                display.show();
                QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
                while (app && display.isVisible()) {
                    app->processEvents(QEventLoop::AllEvents);
                    app->sendPostedEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
                break;
            }
            case 43: {
                // Osciloscopio O2 con auto-select de pestaña (index 2)
                QtDisplay display(m_elm.get());
                display.setActiveTab(2);
                display.show();
                QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
                while (app && display.isVisible()) {
                    app->processEvents(QEventLoop::AllEvents);
                    app->sendPostedEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
                break;
            }
            case 44: {
                // MODO DEMO - interfaz gráfica con datos simulados
                std::cout << "\n🎮 MODO DEMO - Datos simulados (sin ELM327)\n\n";
                QtDisplay display(static_cast<ELM327*>(nullptr), static_cast<QWidget*>(nullptr), true);
                display.show();
                QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
                while (app && display.isVisible()) {
                    app->processEvents(QEventLoop::AllEvents);
                    app->sendPostedEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
                break;
            }
            case 0: return false;
            default: std::cout << "Opcion invalida\n"; break;
        }
        return true;
    }

    // --- Helpers de impresión compartidos ---

    void printVal(const std::string& label, int val, const std::string& unit) const {
        if (val >= 0) {
            std::cout << label << ": " << val << " " << unit << "\n";
        } else {
            std::cout << label << ": N/A\n";
        }
        if (m_logger) m_logger->log(label, val);
        if (m_sessionLog) {
            std::stringstream ss;
            if (val >= 0) ss << val << " " << unit;
            else ss << "N/A";
            m_sessionLog->logResult(label, ss.str());
        }
    }

    void printValD(const std::string& label, double val, const std::string& unit,
                   double minValid = 0.0, double maxValid = 1e9) const {
        bool valid = (val >= minValid && val <= maxValid);
        if (valid) {
            std::cout << label << ": " << std::fixed << std::setprecision(1) << val << " " << unit << "\n";
        } else {
            std::cout << label << ": N/A\n";
        }
        if (m_logger) m_logger->log(label, val);
        if (m_sessionLog) {
            std::stringstream ss;
            if (valid) ss << std::fixed << std::setprecision(1) << val << " " << unit;
            else ss << "N/A";
            m_sessionLog->logResult(label, ss.str());
        }
    }

    void showGMResult(const std::string& label, const std::string& result) {
        std::cout << label << ": " << result << std::endl;
        if (m_logger) m_logger->log(label, result);
        if (m_sessionLog) m_sessionLog->logResult(label, result);
    }

    // --- Sensores ---

    void showAllSensors() const {
        std::cout << "\n=== LECTURA COMPLETA DE SENSORES ===\n";

        // Los siguientes PIDs son estándar OBD-II (SAE J1979) y deberían funcionar
        // en cualquier vehículo compatible (VW, Chevrolet, Toyota, etc.)
        // Algunos PIDs (FuelLevel 012F, BaroPressure 0133) son opcionales.
        // NOTA: delay de 50ms entre cada consulta para evitar estado STOPPED

        printVal("RPM",                m_elm->getRPM(),              "RPM");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Velocidad",          m_elm->getSpeed(),            "km/h");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Temp. motor",        m_elm->getCoolantTemp(),      "°C");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Carga motor",        m_elm->getEngineLoad(),       "%");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printValD("Acelerador",        m_elm->getThrottlePosition(), "%");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Presión colector",   m_elm->getIntakePressure(),   "kPa");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Temp. admisión",     m_elm->getIntakeTemp(),       "°C");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Timing: rango -64° a 64° (valores fuera indican error)
        printValD("Avance encendido", m_elm->getTimingAdvance(), "°", -64.0, 64.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printValD("MAF",               m_elm->getMAF(),              "g/s");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printValD("Combustible",       m_elm->getFuelLevel(),        "%");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printValD("Presión barométrica", m_elm->getBarometricPressure(), "kPa");
    }

    void showOxygenSensors() const {
        auto sensors = m_elm->getOxygenSensors();
        if (sensors.empty()) {
            std::cout << "No se detectaron sensores de oxígeno activos\n";
            std::cout << "Posibles causas:\n";
            std::cout << "  - Motor apagado\n";
            std::cout << "  - Sensores no soportados\n";
            std::cout << "  - Sensores en calentamiento\n";
            if (m_sessionLog) m_sessionLog->logResult("OxygenSensors", "No detectados");
            return;
        }

        for (const auto& sensor : sensors) {
            std::cout << "Banco " << sensor.bank << " - Sensor " << sensor.sensor << "\n";
            std::cout << "  Voltaje: " << std::fixed << std::setprecision(3) << sensor.voltage << " V";
            if (sensor.voltage < 0.1)      std::cout << " (pobre)";
            else if (sensor.voltage > 0.9) std::cout << " (rica)";
            else if (sensor.voltage > 0.3 && sensor.voltage < 0.7) std::cout << " (normal)";
            std::cout << "\n";
            std::cout << "  Trim ST: " << std::fixed << std::setprecision(1) << sensor.shortTermTrim << "%";
            if (sensor.shortTermTrim > 10)       std::cout << " (enriqueciendo)";
            else if (sensor.shortTermTrim < -10) std::cout << " (empobreciendo)";
            std::cout << "\n\n";
            
            // Session log
            if (m_sessionLog) {
                std::stringstream ss;
                ss << "B" << sensor.bank << "S" << sensor.sensor
                   << " V=" << std::fixed << std::setprecision(3) << sensor.voltage << "V"
                   << " ST=" << std::fixed << std::setprecision(1) << sensor.shortTermTrim << "%";
                m_sessionLog->logResult("O2_B" + std::to_string(sensor.bank) + "S" + std::to_string(sensor.sensor), ss.str());
            }
        }
    }

    // --- DTCs ---

    void showDTCs() const {
        auto dtcs = m_elm->getDTCs();
        std::cout << "\n=== CÓDIGOS DE ERROR (DTC) ===\n";
        if (dtcs.empty()) {
            std::cout << "No se pudo leer DTCs (error de comunicación)\n";
            if (m_sessionLog) m_sessionLog->logError("DTCs", "Error de comunicación");
            return;
        }
        if (dtcs.size() == 1 && dtcs[0].code == "NONE") {
            std::cout << dtcs[0].description << std::endl;
            if (m_sessionLog) m_sessionLog->logResult("DTCs", "Sin códigos");
        } else {
            std::cout << "Total de códigos encontrados: " << dtcs.size() << "\n\n";
            for (const auto& dtc : dtcs) {
                if (dtc.code != "NONE") {
                    std::cout << "🔧 " << dtc.description << std::endl;
                    if (m_sessionLog) m_sessionLog->logResult("DTC_" + dtc.code, dtc.description);
                }
            }
        }
    }

    void clearDTCs() {
        char confirm;
        std::cout << "⚠️  ¿Está seguro de borrar todos los códigos de error? (s/N): ";
        std::cin >> confirm;
        if (confirm == 's' || confirm == 'S') {
            bool ok = m_elm->clearDTCs();
            if (ok) {
                std::cout << "✅ Códigos borrados correctamente\n";
                if (m_sessionLog) m_sessionLog->logResult("ClearDTCs", "Éxito");
            } else {
                std::cout << "❌ No se pudieron borrar los códigos\n";
                if (m_sessionLog) m_sessionLog->logError("ClearDTCs", "Falló");
            }
        }
    }

    void showVehicleInfo() const {
        std::string info = m_elm->getVehicleInfo();
        std::cout << info << std::endl;
        if (m_logger) m_logger->log("VehicleInfo", info);
        if (m_sessionLog) m_sessionLog->logResult("VehicleInfo", info);
    }

    void readModule(const std::string& name, const std::string& header) {
        std::cout << "Leyendo módulo " << name << " (" << header << ")...\n";
        m_elm->send("AT SH " + header, 50);
        m_elm->send("AT CRA " + std::to_string(std::stoi(header, nullptr, 16) + 8), 50);
        usleep(50000);
        std::string r = m_elm->send("03", 300);
        if (r.find("43") != std::string::npos) {
            std::cout << "Módulo " << name << " responde. DTCs detectados.\n";
        } else if (r.find("NO DATA") != std::string::npos) {
            std::cout << "Módulo " << name << " OK - sin DTCs\n";
        } else {
            std::cout << "Módulo " << name << " no responde o no disponible\n";
        }
        // Restaurar header
        m_elm->send("AT SH", 30);
        m_elm->send("AT CRA", 30);
    }

    // --- Tiempo real ---

    void liveDashboard() const {
        std::cout << "=== DASHBOARD MOTOR TIEMPO REAL ===\n";
        std::cout << "Muestreo cada " << m_sampleTimeSec << " segundos (configurable en Opción 40)\n";
        std::cout << "Presione Ctrl+C para salir\n\n";
        while (g_runningLogger) {
            auto d = m_elm->getDashboardFast();
            if (d.valid) {
                std::cout << "\rRPM: " << d.rpm
                          << " | SPD: " << d.speed << "km/h"
                          << " | TEMP: " << d.coolant << "°C"
                          << " | LOAD: " << d.load << "%"
                          << " | THR: " << std::fixed << std::setprecision(0) << d.throttle << "%"
                          << " | MAF: " << std::fixed << std::setprecision(0) << d.maf << "g/s  "
                          << std::flush;
            } else {
                std::cout << "\r⏳ Esperando datos..." << std::flush;
            }
            std::this_thread::sleep_for(std::chrono::seconds(m_sampleTimeSec));
        }
        g_runningLogger = true;
    }

    void oxygenSensorScope() const {
        std::cout << "=== OSCILOSCOPIO SENSOR O2 ===\n";
        std::cout << "Monitoreando voltaje O2 B1S1\n";
        std::cout << "Muestreo cada " << m_sampleTimeSec << " segundos (configurable en Opción 40)\n";
        std::cout << "Presione Ctrl+C para salir\n\n";

        // Gráfica ASCII simple
        const int width = 50;
        while (g_runningLogger) {
            auto sensor = m_elm->getO2Sensor(1, 1);
            if (sensor.voltage >= 0) {
                int pos = (int)(sensor.voltage * width);
                if (pos > width) pos = width;
                std::cout << "\r[";
                for (int i = 0; i < width; i++) {
                    if (i == pos) std::cout << "*";
                    else if (i == width/3 && pos != width/3) std::cout << ".";
                    else if (i == 2*width/3 && pos != 2*width/3) std::cout << ".";
                    else std::cout << " ";
                }
                std::cout << "] " << std::fixed << std::setprecision(3) << sensor.voltage << "V"
                          << " Trim: " << std::fixed << std::setprecision(1) << sensor.shortTermTrim << "%"
                          << std::flush;
            } else {
                std::cout << "\r⏳ Esperando sensor..." << std::flush;
            }
            std::this_thread::sleep_for(std::chrono::seconds(m_sampleTimeSec));
        }
        g_runningLogger = true;
    }

    void engineLogger() {
        Logger::ensureLogsDir();
        time_t now = time(nullptr);
        tm* t = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y_%m_%d_%H%M%S", t);
        std::string filename = "logs/all_motor_" + std::string(ts) + ".csv";
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error abriendo archivo\n";
            return;
        }
        std::cout << "Guardando log en: " << filename << std::endl;
        std::cout << "Muestreo cada " << m_sampleTimeSec << " segundos (configurable en Opción 40)\n";
        std::cout << "Presione Ctrl+C para detener.\n";
        file << "timestamp,rpm,speed,coolant,load,throttle,maf,timing,intakeTemp\n";

        g_runningLogger = true;
        int step = 0;
        while (g_runningLogger) {
            time_t now = time(nullptr);
            auto d = m_elm->getDashboardFast();
            if (d.valid) {
                file << now << ","
                     << d.rpm << ","
                     << d.speed << ","
                     << d.coolant << ","
                     << d.load << ","
                     << d.throttle << ","
                     << d.maf << ","
                     << d.timing << ","
                     << d.intakeTemp << "\n";
            }
            file.flush();
            step++;
            if (step % 10 == 0)
                std::cout << "Registros: " << step << "\r" << std::flush;
            for (int i = 0; i < m_sampleTimeSec; i++) {
                if (!g_runningLogger) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        file.close();
        std::cout << "\nLogger detenido. " << step << " registros guardados.\n";
        g_runningLogger = true;
    }

    void detectModules() const {
        std::cout << "=== ESCANEO DE MÓDULOS CAN ===\n";
        std::cout << "Buscando módulos en bus CAN...\n\n";

        // IDs comunes de módulos GM
        const int moduleIds[] = {
            0x7E0, 0x7E1, 0x7E2, 0x7E3, 0x7E4, 0x7E5, 0x7E6, 0x7E7,
            0x7C0, 0x7C4, 0x7C8, 0x7CC, 0x720, 0x728, 0x730, 0x738
        };
        const int numModules = sizeof(moduleIds) / sizeof(moduleIds[0]);

        struct ModuleInfo {
            int id;
            std::string name;
        };
        const ModuleInfo knownModules[] = {
            {0x7E0, "ECU Motor"},
            {0x7E1, "ECU Motor 2"},
            {0x7E2, "ABS"},
            {0x7E3, "Suspensión"},
            {0x7E4, "Airbag / SRS"},
            {0x7E5, "Instrument Cluster"},
            {0x7E6, "BCM / Cuerpo"},
            {0x7E7, "TCM / Transmisión"}
        };

        for (int i = 0; i < numModules; i++) {
            int id = moduleIds[i];
            std::stringstream ss;
            ss << std::hex << id;
            std::string idStr = ss.str();

            std::string name = "0x" + idStr;
            for (const auto& known : knownModules) {
                if (known.id == id) {
                    name = known.name + " (0x" + idStr + ")";
                    break;
                }
            }

            std::cout << "  Probando " << name << "... ";
            std::cout.flush();

            m_elm->send("AT SH " + idStr, 30);
            m_elm->send("AT CRA " + std::to_string(id + 8), 30);
            usleep(100000);  // 100ms pausa para evitar STOPPED
            std::string r = m_elm->send("0100", 150);

            if (r.find("41") != std::string::npos) {
                std::cout << "✅ RESPONDE\n";
            } else {
                std::cout << "-\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Restaurar header ECU motor
        m_elm->send("AT SH", 30);
        m_elm->send("AT CRA", 30);

        std::cout << "\nEscaneo completado (16 módulos en <5s)\n";
    }

    void sendCustomCommand() {
        std::string cmd;
        std::cin.ignore();
        std::cout << "Ingrese comando (ej: 010C para RPM, 22 1940 para modo 22): ";
        std::getline(std::cin, cmd);
        std::cout << "\n[TX] " << cmd << "\n";

        // Session log
        if (m_sessionLog) m_sessionLog->logInfo("Comando personalizado: " + cmd);

        // Si es modo 22, usar sendMode22
        if (cmd.find("22 ") == 0 || cmd.find("22\t") == 0) {
            std::string pid = cmd.substr(3);
            pid.erase(0, pid.find_first_not_of(" \t"));
            std::string resp = m_gm->sendMode22(pid);
            std::cout << "[RX] " << resp << std::endl;
            if (m_sessionLog) m_sessionLog->logResult("CustomCMD_22_" + pid, resp);
        } else {
            std::string resp = m_elm->send(cmd, 500);
            std::cout << "[RX] " << resp << std::endl;
            if (m_sessionLog) m_sessionLog->logResult("CustomCMD_" + cmd, resp);
        }
    }

    void fullSensorsLog() {
        Logger::ensureLogsDir();
        time_t now = time(nullptr);
        tm* t = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y_%m_%d_%H%M%S", t);
        std::string logfile = "logs/all_completo_" + std::string(ts) + ".csv";
        m_elm->logAllSensorsRaw(logfile);
    }

    void p0171DiagnosticLog() {
        Logger::ensureLogsDir();
        time_t now = time(nullptr);
        tm* t = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y_%m_%d_%H%M%S", t);
        std::string logfile = "logs/all_p0171_" + std::string(ts) + ".csv";
        
        std::cout << "Duración en segundos (default 60): ";
        int duration = 60;
        std::cin >> duration;
        if (std::cin.fail() || duration < 1) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            duration = 60;
        }
        
        m_elm->logP0171Diagnostic(logfile, duration);
    }

    // --- Configuración avanzada ---

    void showConfigurationMenu() {
        bool inConfig = true;
        while (inConfig) {
            std::cout << "\n============== CONFIGURACIÓN AVANZADA ==============\n";
            std::cout << "  1. Tiempo de muestreo/debugger: " << m_sampleTimeSec << " segundos\n";
            std::cout << "  2. Cambiar tiempo de muestreo\n";
            std::cout << "  3. Información del sistema\n";
            std::cout << "  0. Volver al menú principal\n";
            std::cout << "=====================================================\n";
            std::cout << "Opción: ";
            std::cout.flush();

            int opt;
            std::cin >> opt;

            if (std::cin.fail()) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Opción inválida\n";
                continue;
            }

            switch (opt) {
                case 1:
                    std::cout << "\n📊 Tiempo de muestreo actual: " << m_sampleTimeSec << " segundos\n";
                    std::cout << "   Este valor afecta a:\n";
                    std::cout << "   - Dashboard tiempo real (Opción 7)\n";
                    std::cout << "   - Grafico Multiparametro (Opción 9)\n";
                    std::cout << "   - Osciloscopio O2 (Opción 10)\n";
                    std::cout << "   - Logger motor (Opción 11)\n";
                    std::cout << "   - Diagnostico P0171 (Opción 13)\n";
                    std::cout << "\n   Presione Enter para continuar...";
                    std::cin.ignore();
                    std::cin.get();
                    break;

                case 2: {
                    std::cout << "\n⏱️  Ingrese nuevo tiempo de muestreo (1-30 segundos): ";
                    int newTime;
                    std::cin >> newTime;
                    if (std::cin.fail() || newTime < 1 || newTime > 30) {
                        std::cin.clear();
                        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                        std::cout << "❌ Valor inválido. Debe ser entre 1 y 30 segundos.\n";
                    } else {
                        m_sampleTimeSec = newTime;
                        std::cout << "✅ Tiempo de muestreo actualizado a " << m_sampleTimeSec << " segundos\n";
                        if (m_logger) m_logger->log("SampleTimeSec", m_sampleTimeSec);
                    }
                    break;
                }

                case 3:
                    std::cout << "\n=== INFORMACIÓN DEL SISTEMA ===\n";
                    std::cout << "Protocolo: " << m_elm->getProtocol() << "\n";
                    {
                        auto d = m_elm->getDashboardFast();
                        if (d.valid) {
                            std::cout << "RPM: " << d.rpm << " RPM\n";
                            std::cout << "Velocidad: " << d.speed << " km/h\n";
                            std::cout << "Temp. motor: " << d.coolant << " °C\n";
                        }
                    }
                    std::cout << "Tiempo muestreo: " << m_sampleTimeSec << "s\n";
                    std::cout << "Estado: " << (m_elm->isConnected() ? "✅ Conectado" : "❌ Desconectado") << "\n";
                    std::cout << "\n   Presione Enter para continuar...";
                    std::cin.ignore();
                    std::cin.get();
                    break;

                case 0:
                    inConfig = false;
                    break;

                default:
                    std::cout << "Opción inválida\n";
                    break;
            }
        }
    }

    // --- Reset adaptativos ---

    void showResetAdaptationsMenu() {
        std::cout << "\n=== RESET ADAPTATIVOS CHEVROLET/GM ===\n";
        std::cout << "⚠️  Modo 22 (F10E/F10D/F11C) NO soportado en CAN moderno\n";
        std::cout << "    Los adaptativos se resetean:\n";
        std::cout << "    1. Desconectando batería 10min\n";
        std::cout << "    2. O con escáner Tech2/GM MDI\n";
        std::cout << "\nOpciones disponibles:\n";
        std::cout << "1. Ver Fuel Trims actuales (LTFT/STFT)\n";
        std::cout << "2. Intentar reset vía modo 22 F10E (puede fallar)\n";
        std::cout << "3. Reset completo intentando todos los métodos\n";
        std::cout << "0. Cancelar\n";
        std::cout << "Opción: ";
        std::cout.flush();

        int opt;
        std::cin >> opt;

        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Opción inválida\n";
            return;
        }

        switch(opt) {
            case 1: {
                auto ft = m_elm->getAllFuelTrims();
                if (ft.available) {
                    std::cout << "\nFuel Trims actuales:\n";
                    
                    // Mostrar cada trim, indicando si hubo error de comunicación
                    auto printTrim = [](const std::string& label, double val) {
                        std::cout << "  " << label << ": ";
                        if (val < -50.0) {  // -999.0 = error de comunicación (sentinel)
                            std::cout << "⚠️  Error de lectura\n";
                        } else {
                            std::cout << std::fixed << std::setprecision(1) << val << "%\n";
                        }
                    };
                    
                    printTrim("STFT B1", ft.shortTermBank1);
                    printTrim("STFT B2", ft.shortTermBank2);
                    printTrim("LTFT B1", ft.longTermBank1);
                    printTrim("LTFT B2", ft.longTermBank2);
                    
                    // Mostrar alerta solo si hay valores REALMENTE fuera de rango
                    if (ft.longTermBank1 > 25 || (ft.longTermBank1 < -25 && ft.longTermBank1 >= -50)) {
                        std::cout << "\n⚠️  LTFT fuera de rango! Posible problema de mezcla\n";
                    }
                    // Session log
                    if (m_sessionLog) {
                        std::stringstream ss;
                        ss << "STFT_B1=" << std::fixed << std::setprecision(1) << ft.shortTermBank1
                           << "% STFT_B2=" << ft.shortTermBank2
                           << "% LTFT_B1=" << ft.longTermBank1
                           << "% LTFT_B2=" << ft.longTermBank2 << "%";
                        m_sessionLog->logResult("FuelTrims", ss.str());
                    }
                } else {
                    std::cout << "Fuel Trims no disponibles\n";
                    if (m_sessionLog) m_sessionLog->logResult("FuelTrims", "No disponibles");
                }
                break;
            }
            case 2:
                m_gm->resetAdaptations();
                break;
            case 3:
                m_gm->resetAdaptations();
                std::cout << "\n📌 Verificando Fuel Trims post-reset...\n";
                {
                    auto ft = m_elm->getAllFuelTrims();
                    if (ft.available) {
                        auto printTrim = [](const std::string& label, double val) {
                            std::cout << "   " << label << ": ";
                            if (val < -50.0)
                                std::cout << "⚠️  Error\n";
                            else
                                std::cout << std::fixed << std::setprecision(1) << val << "%\n";
                        };
                        printTrim("STFT B1", ft.shortTermBank1);
                        printTrim("LTFT B1", ft.longTermBank1);
                    }
                }
                break;
            default:
                std::cout << "Cancelado\n";
                break;
        }
    }

    // =========== NUEVAS FUNCIONES DEL ESCANER PROFESIONAL ===========

    void showEngineParams() {
        std::cout << "Parametros del Motor (PCM):\n";
        printVal("RPM",                m_elm->getRPM(),              "RPM");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Velocidad",          m_elm->getSpeed(),            "km/h");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Temp. Motor",        m_elm->getCoolantTemp(),      "°C");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Carga Motor",        m_elm->getEngineLoad(),       "%");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printValD("Acelerador",        m_elm->getThrottlePosition(), "%");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printVal("Presion Colector",   m_elm->getIntakePressure(),   "kPa");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        printValD("MAF",               m_elm->getMAF(),              "g/s");
    }

    void showAutoScan() {
        std::cout << "=== AUTO SCAN - ESCANEO COMPLETO DEL VEHICULO ===\n\n";
        std::cout << "Escaneando todos los modulos del vehiculo...\n\n";
        auto results = m_elm->autoScan();
        std::cout << "\n--- RESULTADOS DEL AUTO SCAN ---\n";
        int totalDTCs = 0;
        for (const auto& mod : results) {
            std::cout << "  " << mod.name << " (0x" << std::hex << mod.id << std::dec << "): ";
            if (mod.responds) {
                std::cout << "RESPONDE";
                if (!mod.dtcs.empty()) {
                    std::cout << " - " << mod.dtcs.size() << " DTC(s): ";
                    for (const auto& dtc : mod.dtcs) {
                        std::cout << dtc.code << " ";
                        totalDTCs++;
                    }
                } else {
                    std::cout << " - Sin DTCs";
                }
            } else {
                std::cout << "No responde";
            }
            std::cout << "\n";
        }
        std::cout << "\nTotal de modulos: " << results.size() << " | Responden: ";
        int responding = 0;
        for (const auto& mod : results) if (mod.responds) responding++;
        std::cout << responding << " | DTCs encontrados: " << totalDTCs << "\n";
    }

    void showGraphMode() const {
        std::cout << "=== GRAFICO MULTIPARAMETRO ===\n";
        std::cout << "Monitoreando: RPM, Carga, Acelerador, MAF\n";
        std::cout << "Presione Ctrl+C para salir\n\n";
        const int width = 40;
        while (g_runningLogger) {
            int rpm = m_elm->getRPM();
            int load = m_elm->getEngineLoad();
            double thr = m_elm->getThrottlePosition();
            double maf = m_elm->getMAF();
            // Grafico RPM
            std::cout << "\rRPM: " << std::setw(5) << rpm;
            if (rpm > 0) {
                int bar = std::min(rpm / 100, width);
                std::cout << " |";
                for (int i = 0; i < bar; i++) std::cout << "#";
                for (int i = bar; i < width; i++) std::cout << " ";
                std::cout << "|";
            }
            std::cout << " LOAD:" << std::setw(3) << load << "% THR:" << std::setw(3) << (int)thr << "% MAF:" << std::setw(4) << (int)maf << "  " << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        g_runningLogger = true;
    }

    void showFreezeFrame() const {
        std::string result = m_elm->getFreezeFrame();
        std::cout << result << std::endl;
    }

    void showMonitorTests() const {
        std::string result = m_elm->getMonitorTests();
        std::cout << result << std::endl;
    }

    void showO2Test() const {
        std::cout << "Seleccione banco y sensor para prueba:\n";
        std::cout << "Banco (1-2): ";
        int bank; std::cin >> bank;
        if (std::cin.fail() || bank < 1 || bank > 2) {
            std::cin.clear(); std::cin.ignore();
            std::cout << "Banco invalido\n"; return;
        }
        std::cout << "Sensor (1-4): ";
        int sensor; std::cin >> sensor;
        if (std::cin.fail() || sensor < 1 || sensor > 4) {
            std::cin.clear(); std::cin.ignore();
            std::cout << "Sensor invalido\n"; return;
        }
        m_elm->requestO2Test(bank, sensor);
    }

    void showPendingDTCs() const {
        auto dtcs = m_elm->getPendingDTCs();
        std::cout << "\n=== CODIGOS PENDIENTES (MODO 07) ===\n";
        if (dtcs.empty()) {
            std::cout << "No se pudieron leer codigos pendientes\n";
            return;
        }
        for (const auto& dtc : dtcs) {
            if (dtc.code == "NONE") {
                std::cout << dtc.description << std::endl;
            } else {
                std::cout << "  " << dtc.description << std::endl;
            }
        }
    }

    void showPermanentDTCs() const {
        auto dtcs = m_elm->getPermanentDTCs();
        std::cout << "\n=== CODIGOS PERMANENTES (MODO 0A) ===\n";
        if (dtcs.empty()) {
            std::cout << "No se pudieron leer codigos permanentes\n";
            return;
        }
        for (const auto& dtc : dtcs) {
            if (dtc.code == "NONE") {
                std::cout << dtc.description << std::endl;
            } else {
                std::cout << "  " << dtc.description << std::endl;
            }
        }
    }

    void showServiceStub(const std::string& name, const std::string& instructions) const {
        std::cout << "\n=== " << name << " ===\n\n";
        std::cout << "[!] Esta funcion requiere un escaner profesional.\n\n";
        std::cout << instructions << std::endl;
        std::cout << "\n[!] ELM327 no tiene capacidad bidireccional completa.\n";
        std::cout << "    Para estas funciones, use Autel, Launch o Tech2.\n";
    }

    std::string createLogFilename() const {
        Logger::ensureLogsDir();
        std::time_t now = std::time(nullptr);
        std::tm* t = std::localtime(&now);
        std::stringstream ss;
        ss << "logs/all_" << std::put_time(t, "%Y_%m_%d_%H%M%S") << ".csv";
        return ss.str();
    }
};

} // namespace OBD
