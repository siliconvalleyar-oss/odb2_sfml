/**
 * @file qt_display.cpp
 * @brief Implementación de la interfaz gráfica Qt5 para el escáner OBD-II.
 *
 * Dibuja indicadores analógicos, barras de progreso, gráficos de líneas
 * y osciloscopio O2 en widgets Qt5 usando QPainter (o QChart) con antialiasing.
 * Los datos se obtienen del ELM327 en tiempo real mediante QTimer.
 * Si demoMode=true, genera datos simulados sin necesidad de ELM327.
 */

#include "qt_display.hpp"

#include <QScreen>
#include <QGuiApplication>
#include <QPushButton>
#include <QStringList>
#include <QHBoxLayout>
#include <QWidget>
#include <iostream>
#include <sys/stat.h>
#include <string>
#include <vector>
#ifdef Q_OS_WIN
#include <direct.h>
#endif

// ============================================================================
// Escenarios demo — estado compartido entre widgets
// ============================================================================

namespace {
    // Estado global del demo (compartido entre widgets Qt vía static)
    DemoPhase g_demoPhase = DemoPhase::Normal;
    int       g_phaseStep = 0;     // Pasos transcurridos en la fase actual
    int       g_globalStep = 0;    // Paso global (continuo, ~1Hz)
    int       g_o2Step = 0;        // Paso para O2 (~5Hz, independiente)
    double    g_overheatTemp = 90.0;  // Temp actual durante sobrecalentamiento
    double    g_stft = 2.5;        // Short-term fuel trim actual
    double    g_ltft = 2.5;        // Long-term fuel trim actual
    double    g_fuelLevel = 55.0;  // Nivel de combustible
    bool      g_milOn = false;     // MIL / Check Engine
    bool      g_demoForced = false; // Forzar demo desde botón (toggle en GUI)
    std::vector<std::string> g_demoDTCs;  // Códigos DTC activos

    // Semilla aleatoria única
    bool g_seeded = false;
    void ensureSeeded() {
        if (!g_seeded) {
            std::srand(static_cast<unsigned>(std::time(nullptr)));
            g_seeded = true;
        }
    }

    // Genera códigos DTC correspondientes a la fase actual
    std::vector<std::string> getDTCsForPhase(DemoPhase phase) {
        switch (phase) {
        case DemoPhase::FuelPumpFailure:
            return {
                "P0087 — Presión riel combustible demasiado baja",
                "P0089 — Rendimiento regulador presión combustible",
                "P0171 — Mezcla pobre (banco 1) — inyectores sin combustible",
                "P0190 — Circuito sensor presión riel combustible"
            };
        case DemoPhase::FallaSensor:
            return {
                "P0101 — Rango/rendimiento del circuito MAF",
                "P0102 — Entrada baja del circuito MAF",
                "P0171 — Mezcla pobre (banco 1)",
                "P0300 — Fallo encendido aleatorio/múltiples cilindros"
            };
        case DemoPhase::SlowO2Sensor:
            return {
                "P0130 — Circuito sensor O2 B1S1 — mal funcionamiento",
                "P0134 — Sensor O2 B1S1 — sin actividad detectada",
                "P0135 — Calentador sensor O2 B1S1 — circuito"
            };
        case DemoPhase::Overheating:
            return {
                "P0117 — Sensor ECT voltaje bajo (temp alta)",
                "P0118 — Circuito sensor ECT — voltaje alto",
                "P0128 — Termostato — temp por debajo normal",
                "P0217 — Sobrecalentamiento del motor",
                "P0480 — Circuito ventilador refrigeración",
                "P1299 — Protección sobrecalentamiento motor"
            };
        default:
            return {};
        }
    }

    // Avance de fase — llamado en cada tick de generateDemoData() (~1Hz)
    void advanceDemoPhase() {
        g_phaseStep++;
        ensureSeeded();

        switch (g_demoPhase) {
        case DemoPhase::Normal: {
            // Probabilidad de transición a escenarios especiales (~1-5% por tick)
            int r = std::rand() % 100;
            if (r < 3) {  // 3% → AccelerationBrusca
                g_demoPhase = DemoPhase::AccelerationBrusca;
                g_phaseStep = 0;
            } else if (r < 4) {  // 1% → RalentiIrregular
                g_demoPhase = DemoPhase::RalentiIrregular;
                g_phaseStep = 0;
                g_stft = -8.0;
                g_ltft = -5.0;
            } else if (r < 5) {  // 1% → DTCTrigger
                g_demoPhase = DemoPhase::DTCTrigger;
                g_phaseStep = 0;
                g_demoDTCs = {
                    "P0300 — Fallo encendido aleatorio/múltiples cilindros",
                    "P0420 — Eficiencia catalizador bajo banco 1",
                    "P0455 — Fuga grande en sistema EVAP",
                    "P0507 — Ralentí más alto de lo esperado",
                    "P1297 — Fallo conexión turbocompresor"
                };
                g_milOn = true;
            } else if (r < 7) {  // 2% → FuelPumpFailure
                g_demoPhase = DemoPhase::FuelPumpFailure;
                g_phaseStep = 0;
                g_stft = -8.0;
                g_ltft = 5.0;
                g_demoDTCs = getDTCsForPhase(DemoPhase::FuelPumpFailure);
            } else if (r < 9) {  // 2% → SlowO2Sensor
                g_demoPhase = DemoPhase::SlowO2Sensor;
                g_phaseStep = 0;
                g_stft = 1.0;
                g_ltft = 1.0;
                g_demoDTCs = getDTCsForPhase(DemoPhase::SlowO2Sensor);
            } else if (r < 10) {  // 1% → Overheating
                g_demoPhase = DemoPhase::Overheating;
                g_phaseStep = 0;
                g_overheatTemp = 90.0;
                g_demoDTCs = getDTCsForPhase(DemoPhase::Overheating);
            }
            break;
        }
        case DemoPhase::AccelerationBrusca:
            if (g_phaseStep >= 6) {
                g_demoPhase = DemoPhase::PostAccel;
                g_phaseStep = 0;
            }
            break;
        case DemoPhase::PostAccel: {
            bool triggerFalla = (std::rand() % 10) < 4;  // 40%
            if (g_phaseStep >= 8) {
                if (triggerFalla) {
                    g_demoPhase = DemoPhase::FallaSensor;
                    g_demoDTCs = getDTCsForPhase(DemoPhase::FallaSensor);
                    g_milOn = true;
                } else {
                    g_demoPhase = DemoPhase::Normal;
                }
                g_phaseStep = 0;
            }
            break;
        }
        case DemoPhase::FallaSensor:
            if (g_phaseStep >= 12) {
                g_demoPhase = DemoPhase::Recovery;
                g_phaseStep = 0;
            }
            break;
        case DemoPhase::RalentiIrregular:
            if (g_phaseStep >= 12) {
                g_demoPhase = DemoPhase::Normal;
                g_phaseStep = 0;
            }
            break;
        case DemoPhase::DTCTrigger:
            if (g_phaseStep >= 3) {
                g_demoPhase = DemoPhase::Normal;
                g_phaseStep = 0;
                // Mantener MIL encendido tras DTC
            }
            break;
        case DemoPhase::FuelPumpFailure:
            if (g_phaseStep >= 10) {
                g_demoPhase = DemoPhase::Recovery;
                g_phaseStep = 0;
            }
            break;
        case DemoPhase::SlowO2Sensor:
            if (g_phaseStep >= 12) {
                g_demoPhase = DemoPhase::Normal;
                g_phaseStep = 0;
                g_milOn = false;
                g_stft = 2.5;
                g_ltft = 2.5;
                g_demoDTCs.clear();
            }
            break;
        case DemoPhase::Overheating:
            if (g_phaseStep >= 14) {
                g_demoPhase = DemoPhase::Recovery;
                g_phaseStep = 0;
            }
            break;
        case DemoPhase::Recovery:
            if (g_phaseStep >= 5) {
                g_demoPhase = DemoPhase::Normal;
                g_phaseStep = 0;
                g_milOn = false;
                g_stft = 2.5;
                g_ltft = 2.5;
                g_overheatTemp = 90.0;
                g_demoDTCs.clear();
            }
            break;
        default:
            break;
        }
    }

    // Genera STFT según fase y paso
    void updateFuelTrims() {
        ensureSeeded();
        double t = g_globalStep * 0.05;

        switch (g_demoPhase) {
        case DemoPhase::Normal:
            g_stft = 8.0 * std::sin(t * 0.7);
            g_ltft = 3.0 * std::sin(t * 0.15);
            break;
        case DemoPhase::AccelerationBrusca:
            // Aceleración a fondo → mezcla rica, trims negativos
            g_stft = -5.0 + 2.0 * std::sin(t * 0.2);
            g_ltft = 3.0 + 2.0 * std::sin(t * 0.01);
            break;
        case DemoPhase::PostAccel:
            // Post-aceleración: trims normalizándose
            g_stft = -2.0 + 4.0 * std::sin(t * 0.15);
            g_ltft = 2.0 + 3.0 * std::sin(t * 0.01);
            break;
        case DemoPhase::FallaSensor:
            // Falla de sensor: ECM intenta compensar lecturas erráticas
            g_stft = 12.0 + 5.0 * std::sin(t * 0.05);
            g_ltft = 18.0 + 3.0 * std::sin(t * 0.01);
            break;
        case DemoPhase::RalentiIrregular:
            // Ralentí irregular: trims erráticos
            g_stft = -8.0 + 6.0 * std::sin(t * 0.1);
            g_ltft = -5.0 + 4.0 * std::sin(t * 0.02);
            break;
        case DemoPhase::DTCTrigger:
            // DTC activo: trims ligeramente fuera de rango
            g_stft = 3.0 + 8.0 * std::sin(t * 0.12);
            g_ltft = 5.0 + 5.0 * std::sin(t * 0.02);
            break;
        case DemoPhase::FuelPumpFailure: {
            // STFT intenta compensar falta de combustible → muy positivo
            double stumble = (std::rand() % 100) < 25 ? -10.0 : 0.0;
            g_stft = 20.0 + 10.0 * std::sin(t * 0.5) + stumble;
            g_ltft = std::min(30.0, g_ltft + 0.5);
            break;
        }
        case DemoPhase::SlowO2Sensor:
            // Trims vagan lentamente (el ECM no recibe feedback rápido)
            g_stft = 5.0 * std::sin(t * 0.08);
            g_ltft = 8.0 * std::sin(t * 0.03);
            break;
        case DemoPhase::Overheating:
            // Con el calor, la mezcla se empobrece y los trims suben
            g_stft = 5.0 + 5.0 * std::sin(t * 0.3);
            g_ltft = 8.0 + 4.0 * std::sin(t * 0.1);
            break;
        case DemoPhase::Recovery:
            g_stft = std::max(-2.0, g_stft - 3.0);
            g_ltft = std::max(0.0, g_ltft - 2.0);
            break;
        default:
            break;
        }
    }
}

// ============================================================================
// Accesores estáticos del estado del demo
// ============================================================================

DemoPhase QtDisplay::demoPhase() { return g_demoPhase; }
void QtDisplay::resetDemo() {
    g_demoPhase = DemoPhase::Normal;
    g_phaseStep = 0;
    g_globalStep = 0;
    g_o2Step = 0;
    g_overheatTemp = 90.0;
    g_stft = 2.5;
    g_ltft = 2.5;
    g_fuelLevel = 55.0;
    g_milOn = false;
    g_demoDTCs.clear();
}

QString QtDisplay::demoPhaseName() {
    switch (g_demoPhase) {
    case DemoPhase::Normal:             return QStringLiteral("Normal");
    case DemoPhase::AccelerationBrusca: return QStringLiteral("⚡ Aceleración Brusca");
    case DemoPhase::PostAccel:          return QStringLiteral("↘ Desaceleración");
    case DemoPhase::FallaSensor:        return QStringLiteral("⚠ Falla Intermitente Sensor");
    case DemoPhase::RalentiIrregular:   return QStringLiteral("🌀 Ralentí Irregular");
    case DemoPhase::DTCTrigger:         return QStringLiteral("🔴 DTC Activo");
    case DemoPhase::FuelPumpFailure:    return QStringLiteral("💀 Falla Bomba Combustible");
    case DemoPhase::SlowO2Sensor:       return QStringLiteral("⏳ Sensor O2 Lento");
    case DemoPhase::Overheating:        return QStringLiteral("🔥 Sobrecalentamiento");
    case DemoPhase::Recovery:           return QStringLiteral("🔧 Recuperación");
    default:                            return QStringLiteral("Normal");
    }
}

double QtDisplay::demoSTFT() { return g_stft; }
double QtDisplay::demoLTFT() { return g_ltft; }
bool QtDisplay::demoCheckEngine() { return g_milOn; }

QString QtDisplay::demoDTCsText() {
    if (g_demoDTCs.empty()) return {};
    QStringList lines;
    for (const auto& dtc : g_demoDTCs) {
        lines << QString::fromStdString(dtc);
    }
    return lines.join('\n');
}

// ============================================================================
// Helpers de datos simulados
// ============================================================================

ELM327::DashboardData QtDisplay::generateDemoData() {
    g_globalStep++;
    advanceDemoPhase();
    updateFuelTrims();
    ensureSeeded();

    float t = g_globalStep * 0.1f;
    ELM327::DashboardData d;
    d.valid = true;

    // Valores base (Normal — sinusoidal suave)
    double baseRPM     = 750 + 500 * std::sin(t * 0.7f) + 200 * std::sin(t * 0.13f);
    double baseSpeed   = 60 + 40 * std::sin(t * 0.5f) + 10 * std::sin(t * 0.11f);
    double baseCoolant = 85 + 8 * std::sin(t * 0.3f);
    double baseLoad    = 35 + 25 * std::sin(t * 0.6f);
    double baseThrottle = 20.0 + 15.0 * std::sin(t * 0.8f);
    double baseMAF      = 8.0 + 5.0 * std::sin(t * 0.65f);
    double baseTiming   = 12.0 + 8.0 * std::sin(t * 0.4f);
    double baseIntake   = 40.0 + 5.0 * std::sin(t * 0.2f);

    // Nivel de combustible general (desgaste lento)
    g_fuelLevel = std::max(10.0, g_fuelLevel - 0.01);

    // Aplicar escenario
    switch (g_demoPhase) {

    case DemoPhase::Normal:
        d.rpm       = static_cast<int>(baseRPM);
        d.speed     = static_cast<int>(baseSpeed);
        d.coolant   = static_cast<int>(baseCoolant);
        d.load      = static_cast<int>(baseLoad);
        d.throttle  = static_cast<float>(baseThrottle);
        d.maf       = static_cast<float>(baseMAF);
        d.timing    = static_cast<float>(baseTiming);
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel  = static_cast<float>(g_fuelLevel);
        break;

    case DemoPhase::AccelerationBrusca: {
        // ⚡ Aceleración brusca: RPM sube 2500→6800, velocidad surge, carga máxima
        double progress = std::min(1.0, g_phaseStep / 6.0);
        d.rpm = static_cast<int>(2500 + 4300 * std::min(1.0, progress * 1.5));
        d.speed = static_cast<int>(60 + 100 * progress);
        d.coolant = static_cast<int>(baseCoolant);
        d.load = static_cast<int>(std::min(90.0, 50.0 + 45.0 * progress));
        d.throttle = static_cast<float>(std::min(95.0, 70.0 + 25.0 * (1.0 - std::exp(-progress * 3.0))));
        d.maf = static_cast<float>((d.rpm / 1000.0) * (d.load / 15.0) + 3.0);
        d.timing = static_cast<float>(10.0 + 15.0 * (d.rpm / 6500.0) - 3.0 * progress);
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        break;
    }

    case DemoPhase::PostAccel: {
        // ↘ Desaceleración post-aceleración: valores volviendo a normal
        double phase = t;
        d.rpm = static_cast<int>(1500.0 + 2500.0 * (0.5 + 0.5 * std::sin(phase)));
        d.speed = static_cast<int>(20.0 + 80.0 * (0.5 + 0.5 * std::sin(t * 0.03)));
        d.coolant = static_cast<int>(baseCoolant);
        d.load = static_cast<int>(15.0 + 35.0 * (0.5 + 0.5 * std::sin(phase)));
        d.throttle = static_cast<float>(10.0 + 20.0 * (0.5 + 0.5 * std::sin(t * 0.06)));
        d.maf = static_cast<float>((d.rpm / 1000.0) * (d.load / 25.0) + 1.5);
        d.timing = static_cast<float>(8.0 + 20.0 * (d.rpm / 6500.0));
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        break;
    }

    case DemoPhase::FallaSensor: {
        // ⚠ Falla intermitente: MAF cae cada 2 ciclos, coolant errático
        double phase = t;
        d.rpm = static_cast<int>(1800.0 + 2000.0 * (0.5 + 0.5 * std::sin(phase)));
        d.speed = static_cast<int>(30.0 + 50.0 * (0.5 + 0.5 * std::sin(t * 0.02)));
        d.load = static_cast<int>(20.0 + 30.0 * (0.5 + 0.5 * std::sin(phase + 0.5)));
        d.throttle = static_cast<float>(15.0 + 25.0 * (0.5 + 0.5 * std::sin(t * 0.06)));
        if ((g_globalStep / 5) % 2 == 0) {
            d.maf = static_cast<float>(1.0 + 2.0 * (0.5 + 0.5 * std::sin(t * 0.2)));
        } else {
            d.maf = static_cast<float>((d.rpm / 1000.0) * (d.load / 25.0) + 1.5);
        }
        d.coolant = static_cast<int>(80 + 20 * std::sin(t * 0.15) + 5 * std::sin(t * 0.7));
        d.timing = static_cast<float>(5.0 + 15.0 * (d.rpm / 6500.0) + 5.0 * std::sin(t * 0.25));
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        break;
    }

    case DemoPhase::RalentiIrregular: {
        // 🌀 Ralentí irregular: RPM caza entre 400-2500, carga errática
        double hunt = 0.5 + 0.5 * std::sin(t * 0.3);
        double misfire = 100.0 * std::sin(t * 0.8);
        d.rpm = static_cast<int>(700 + 600 * hunt + misfire);
        if (d.rpm < 400) d.rpm = 400;
        if (d.rpm > 2500) d.rpm = 2500;
        d.speed = static_cast<int>(2.0 + 3.0 * (0.5 + 0.5 * std::sin(t * 0.1)));
        d.coolant = static_cast<int>(baseCoolant);
        d.load = static_cast<int>(25.0 + 30.0 * hunt + 10.0 * std::sin(t * 0.5));
        d.throttle = static_cast<float>(5.0 + 15.0 * hunt);
        d.maf = static_cast<float>((d.rpm / 1000.0) * (d.load / 20.0) + 1.0);
        d.timing = static_cast<float>(5.0 + 10.0 * hunt);
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        break;
    }

    case DemoPhase::DTCTrigger: {
        // 🔴 Múltiples DTCs: datos relativamente normales con algo de ruido
        double phase = t;
        d.rpm = static_cast<int>(1200.0 + 2500.0 * (0.5 + 0.5 * std::sin(phase)));
        d.speed = static_cast<int>(20.0 + 60.0 * (0.5 + 0.5 * std::sin(t * 0.025)));
        d.coolant = static_cast<int>(baseCoolant);
        d.load = static_cast<int>(20.0 + 40.0 * (0.5 + 0.5 * std::sin(phase)));
        d.throttle = static_cast<float>(10.0 + 30.0 * (0.5 + 0.5 * std::sin(t * 0.05)));
        d.maf = static_cast<float>((d.rpm / 1000.0) * (d.load / 22.0) + 1.5 + 2.0 * std::sin(t * 0.15));
        d.timing = static_cast<float>(8.0 + 15.0 * (d.rpm / 6500.0));
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        break;
    }

    case DemoPhase::FuelPumpFailure: {
        // 💀 Bomba de combustible fallando: presión baja → mezcla pobre, RPM erráticas
        double phaseT = g_phaseStep * 0.1;
        // Tropiezos cíclicos de RPM (baja presión → cilindros no reciben combustible)
        double stumble = 300.0 * std::sin(phaseT * 2.5);
        stumble = std::abs(stumble) > 200.0 ? stumble : 0.0;
        d.rpm = static_cast<int>(std::max(400.0, baseRPM - 200.0 - stumble));
        d.speed = static_cast<int>(std::max(20.0, baseSpeed - 15.0));
        d.coolant = static_cast<int>(std::min(105.0, baseCoolant + 8.0));
        // ECM intenta compensar → carga alta
        d.load = static_cast<int>(std::min(95.0, baseLoad + 35.0));
        // Conductor pisa acelerador a fondo
        d.throttle = static_cast<float>(80.0 + 12.0 * std::sin(phaseT * 0.7));
        // MAF cae por falta de combustible (menos mezcla = menos flujo)
        double mafDrop = 5.0 * std::sin(phaseT * 0.5) + 3.0;
        d.maf = static_cast<float>(std::max(1.5, baseMAF - mafDrop));
        d.timing = static_cast<float>(baseTiming - 6.0);
        d.intakeTemp = static_cast<float>(baseIntake + 3.0);
        // Combustible cae más rápido (fuga / presión baja)
        g_fuelLevel = std::max(5.0, g_fuelLevel - 0.5);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        g_milOn = true;
        break;
    }

    case DemoPhase::SlowO2Sensor:
        // ⏳ Solo el sensor O2 es lento — el resto de datos es normal
        d.rpm       = static_cast<int>(baseRPM);
        d.speed     = static_cast<int>(baseSpeed);
        d.coolant   = static_cast<int>(baseCoolant);
        d.load      = static_cast<int>(baseLoad);
        d.throttle  = static_cast<float>(baseThrottle);
        d.maf       = static_cast<float>(baseMAF);
        d.timing    = static_cast<float>(baseTiming);
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel  = static_cast<float>(g_fuelLevel);
        g_milOn = true;
        break;

    case DemoPhase::Overheating: {
        // 🔥 Temperatura subiendo progresivamente
        g_overheatTemp = std::min(125.0, g_overheatTemp + 2.8);
        double tempFactor = (g_overheatTemp - 90.0) / 35.0;  // 0.0 → 1.0

        d.coolant = static_cast<int>(g_overheatTemp);

        // RPM suben al recalentarse (ventilador, termostato, etc.)
        if (g_overheatTemp > 105.0) {
            double surge = 300.0 * std::sin(t * 0.9);
            d.rpm = static_cast<int>(baseRPM + 200.0 + surge);
        } else {
            d.rpm = static_cast<int>(baseRPM + 100.0);
        }
        d.speed = static_cast<int>(std::max(30.0, baseSpeed - 10.0));
        d.load = static_cast<int>(std::min(90.0, baseLoad + 20.0 + tempFactor * 15.0));
        d.throttle = static_cast<float>(std::min(95.0, baseThrottle + 10.0 + tempFactor * 15.0));
        d.maf = static_cast<float>(baseMAF + 2.0);
        // Avance se retarda con el calor
        d.timing = static_cast<float>(baseTiming - tempFactor * 5.0);
        // Temperatura de admisión también sube
        d.intakeTemp = static_cast<float>(40.0 + g_overheatTemp * 0.4);
        d.fuelLevel = static_cast<float>(g_fuelLevel);
        g_milOn = true;
        break;
    }

    case DemoPhase::Recovery:
        // 🔧 Recuperación progresiva a valores normales
        {
            double recoveryT = g_phaseStep / 5.0;  // 0→1 en 5 ticks
            auto lerp = [&](double normalVal, double faultVal) -> double {
                return normalVal + (faultVal - normalVal) * (1.0 - recoveryT);
            };

            // Determinar cuál era la falla previa (temperatura elevada = overheating)
            bool wasHot = g_overheatTemp > 100.0;

            d.rpm = static_cast<int>(lerp(baseRPM, baseRPM + (wasHot ? 200.0 : -100.0)));
            d.speed = static_cast<int>(lerp(baseSpeed, baseSpeed - 10.0));
            d.coolant = static_cast<int>(lerp(baseCoolant, std::max(100.0, g_overheatTemp)));
            if (wasHot) {
                g_overheatTemp = std::max(90.0, g_overheatTemp - 5.0);
            }
            d.load = static_cast<int>(lerp(baseLoad, baseLoad + 25.0));
            d.throttle = static_cast<float>(lerp(baseThrottle, baseThrottle + 20.0));
            d.maf = static_cast<float>(lerp(baseMAF, baseMAF * 0.7));
            d.timing = static_cast<float>(lerp(baseTiming, baseTiming - 4.0));
            d.intakeTemp = static_cast<float>(lerp(baseIntake, baseIntake + 5.0));
            d.fuelLevel = static_cast<float>(g_fuelLevel);

            // Apagar MIL al final de la recuperación
            if (g_phaseStep >= 4) g_milOn = false;
        }
        break;

    default:
        d.rpm       = static_cast<int>(baseRPM);
        d.speed     = static_cast<int>(baseSpeed);
        d.coolant   = static_cast<int>(baseCoolant);
        d.load      = static_cast<int>(baseLoad);
        d.throttle  = static_cast<float>(baseThrottle);
        d.maf       = static_cast<float>(baseMAF);
        d.timing    = static_cast<float>(baseTiming);
        d.intakeTemp = static_cast<float>(baseIntake);
        d.fuelLevel  = static_cast<float>(g_fuelLevel);
        break;
    }

    return d;
}

OxygenSensor QtDisplay::generateDemoO2() {
    ensureSeeded();
    g_o2Step++;
    double o2Step = g_o2Step * 0.05;
    double voltage;
    double stftLocal;

    switch (g_demoPhase) {

    case DemoPhase::AccelerationBrusca:
        // ⚡ Aceleración: mezcla rica (O2 ~0.85V)
        voltage = 0.85 + 0.1 * std::sin(o2Step * 0.5);
        voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.03;
        stftLocal = g_stft;
        break;

    case DemoPhase::PostAccel:
        // ↘ Post-aceleración: O2 normalizándose
        {
            double wave = std::sin(o2Step * 1.2);
            voltage = 0.5 + 0.35 * wave;
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.04;
            stftLocal = g_stft;
        }
        break;

    case DemoPhase::FallaSensor:
        // ⚠ Falla sensor: O2 ruidoso, a veces se congela
        if ((g_o2Step / 8) % 3 == 0) {
            voltage = 0.45 + 0.05 * std::sin(o2Step * 0.1);
        } else {
            double wave = std::sin(o2Step * 1.5);
            voltage = 0.5 + 0.4 * wave;
        }
        voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.08;
        stftLocal = g_stft;
        break;

    case DemoPhase::RalentiIrregular:
        // 🌀 Ralentí: O2 oscila erráticamente
        {
            double wave = std::sin(o2Step * 1.8 + 0.5 * std::sin(o2Step * 0.3));
            voltage = 0.5 + 0.3 * wave;
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.1;
            stftLocal = g_stft;
        }
        break;

    case DemoPhase::DTCTrigger:
        // 🔴 DTC activo: O2 algo ruidoso pero dentro de rango
        {
            double wave = std::sin(o2Step * 1.5);
            voltage = 0.5 + 0.4 * wave;
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.07;
            stftLocal = g_stft;
        }
        break;

    case DemoPhase::FuelPumpFailure:
        // 💀 O2 atascado en lean por falta de combustible
        voltage = 0.15 + 0.05 * std::sin(o2Step * 0.3);
        voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.02;
        stftLocal = 20.0;
        break;

    case DemoPhase::SlowO2Sensor:
        // ⏳ O2 perezoso: conmuta 5× más lento, amplitud reducida
        {
            double slowWave = std::sin(o2Step * 0.3);  // Normal: sin(o2Step*1.5) vs slow: sin(o2Step*0.3)
            voltage = 0.5 + 0.25 * slowWave;  // Amplitud reducida de 0.4 → 0.25
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.03;
            stftLocal = g_stft;
        }
        break;

    case DemoPhase::Overheating:
        // 🔥 O2 deriva a lean con la temperatura
        {
            double leanDrift = (g_overheatTemp - 90.0) / 50.0;  // 0 → 0.7
            double wave = std::sin(o2Step * 1.5);
            voltage = 0.5 + 0.35 * wave - 0.2 * leanDrift;
            voltage = std::max(0.05, std::min(0.95, voltage));
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.04;
            stftLocal = g_stft;
        }
        break;

    case DemoPhase::Recovery:
        // 🔧 O2 volviendo a la normalidad — detectar falla previa por estado del O2
        {
            double recoveryT = g_phaseStep / 5.0;
            // Si la falla previa era FuelPump, el voltaje estaba en ~0.15V
            // Si era Overheating, estaba derivando lean
            // Si era SlowO2, la frecuencia estaba reducida
            // Interpolar suavemente hacia la onda normal
            double normalWave = std::sin(o2Step * 1.5);
            double normalVoltage = 0.5 + 0.4 * normalWave;
            double faultVoltage = 0.2;  // Valor conservador para cualquier falla
            voltage = normalVoltage + (faultVoltage - normalVoltage) * (1.0 - recoveryT);
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.04;
            stftLocal = g_stft;
        }
        break;

    case DemoPhase::Normal:
    default:
        {
            // Onda cuadrada suavizada normal
            double wave = std::sin(o2Step * 1.5);
            voltage = 0.5 + 0.4 * wave;
            voltage += (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 0.05;
            voltage = std::max(0.0, std::min(1.0, voltage));
            stftLocal = g_stft;
        }
        break;
    }

    voltage = std::max(0.0, std::min(1.0, voltage));

    OxygenSensor o2;
    o2.voltage = voltage;
    o2.bank = 1;
    o2.sensor = 1;
    o2.shortTermTrim = stftLocal;
    return o2;
}

// ============================================================================
// QtDisplay — Ventana principal
// ============================================================================

QtDisplay::QtDisplay(ELM327* elm, QWidget* parent, bool demoMode)
    : QMainWindow(parent), m_elm(elm), m_demoMode(demoMode) {
    setWindowTitle("OBD-II Escáner Profesional - Qt5");
    resize(1100, 720);

    // Centrar en la pantalla
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (primaryScreen) {
        QRect screen = primaryScreen->availableGeometry();
        move((screen.width() - width()) / 2, (screen.height() - height()) / 2);
    }
#else
    QRect screen = QApplication::desktop()->screenGeometry();
    move((screen.width() - width()) / 2, (screen.height() - height()) / 2);
#endif

    // Crear pestañas
    m_tabs = new QTabWidget(this);
    m_dashboard = new DashboardWidget(elm, this, demoMode);
#ifdef USE_QT5_CHARTS
    m_graphs = new ChartsGraphWidget(elm, this, demoMode);
#else
    m_graphs = new GraphWidget(elm, this, demoMode);
#endif
    m_scope = new O2ScopeWidget(elm, this, demoMode);

    m_tabs->addTab(m_dashboard, "Dashboard");
    m_tabs->addTab(m_graphs, "Graficos");
    m_tabs->addTab(m_scope, "Osciloscopio O2");

    setCentralWidget(m_tabs);

    // Estilo oscuro
    setStyleSheet(
        "QMainWindow { background-color: #14161C; }"
        "QTabWidget::pane { background-color: #1E2230; border: none; }"
        "QTabBar::tab { background-color: #2A2E3E; color: #DCE1F0;"
        "  padding: 8px 20px; margin: 2px; border-radius: 4px; }"
        "QTabBar::tab:selected { background-color: #3C4158; }"
    );

    // Widget contenedor para botones Demo + Salir en la barra de pestañas
    auto* cornerWidget = new QWidget();
    auto* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 0, 4, 0);
    cornerLayout->setSpacing(4);

    // Botón Demo (alternar demo/real)
    m_btnDemo = new QPushButton(m_demoMode ? "🔌  Real" : "🎮  Demo");
    m_btnDemo->setStyleSheet(
        "QPushButton { background-color: " + QString(m_demoMode ? "#2A6B3A" : "#3A4A2A") + "; "
        "color: white; font-weight: bold; padding: 6px 12px; "
        "border: none; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: " + QString(m_demoMode ? "#3A8A4A" : "#4A6A3A") + "; }"
    );
    m_btnDemo->setFixedSize(100, 28);
    connect(m_btnDemo, &QPushButton::clicked, this, &QtDisplay::toggleDemo);
    cornerLayout->addWidget(m_btnDemo);

    // Botón Salir
    QPushButton* btnExit = new QPushButton("✕  Salir");
    btnExit->setStyleSheet(
        "QPushButton { background-color: #8C2D2D; color: white; font-weight: bold; "
        "padding: 6px 14px; border: none; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #A63A3A; }"
    );
    btnExit->setFixedSize(80, 28);
    connect(btnExit, &QPushButton::clicked, this, &QtDisplay::close);
    cornerLayout->addWidget(btnExit);

    m_tabs->setCornerWidget(cornerWidget, Qt::TopRightCorner);
}

QtDisplay::~QtDisplay() {
    saveScreenshot();
}

void QtDisplay::setActiveTab(int index) {
    if (m_tabs && index >= 0 && index < m_tabs->count()) {
        m_tabs->setCurrentIndex(index);
    }
}

void QtDisplay::toggleDemo() {
    // Alternar entre modo demo y datos reales
    g_demoForced = !g_demoForced;
    m_demoMode = g_demoForced;

    // Actualizar texto y estilo del botón
    if (g_demoForced) {
        m_btnDemo->setText("🔌  Real");
        m_btnDemo->setStyleSheet(
            "QPushButton { background-color: #2A6B3A; color: white; font-weight: bold; "
            "padding: 6px 12px; border: none; border-radius: 4px; font-size: 12px; }"
            "QPushButton:hover { background-color: #3A8A4A; }"
        );
        setWindowTitle("OBD-II Escáner Profesional - Qt5 [MODO DEMO]");
        QtDisplay::resetDemo();
    } else {
        m_btnDemo->setText("🎮  Demo");
        m_btnDemo->setStyleSheet(
            "QPushButton { background-color: #3A4A2A; color: white; font-weight: bold; "
            "padding: 6px 12px; border: none; border-radius: 4px; font-size: 12px; }"
            "QPushButton:hover { background-color: #4A6A3A; }"
        );
        setWindowTitle("OBD-II Escáner Profesional - Qt5");
    }
    std::cout << (g_demoForced ? "🎮 Demo activado (simulación)\n" : "🔌 Demo desactivado (datos reales)\n");
}

void QtDisplay::saveScreenshot() {
    // Crear directorio screenshots/ (compatible Windows/Linux)
    struct stat st;
    if (stat("screenshots", &st) != 0) {
#ifdef Q_OS_WIN
        _mkdir("screenshots");
#else
        mkdir("screenshots", 0755);
#endif
    }

    // Capturar la ventana completa
    QPixmap pixmap = grab();

    // Timestamp
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", t);

    QString filename = QString("screenshots/qt_%1.png").arg(ts);

    if (pixmap.save(filename)) {
        std::cout << "[Qt5] Screenshot guardado: " << filename.toStdString() << std::endl;
    } else {
        std::cerr << "[Qt5] Error al guardar screenshot" << std::endl;
    }
}

// ============================================================================
// DashboardWidget — Indicadores analógicos
// ============================================================================

DashboardWidget::DashboardWidget(ELM327* elm, QWidget* parent, bool demoMode)
    : QWidget(parent), m_elm(elm) {
    (void)demoMode; // El dashboard usa demo data si elm es null
    setMinimumSize(800, 500);
    setAutoFillBackground(true);

    QPalette pal;
    pal.setColor(QPalette::Window, Theme::BG_DARK);
    setPalette(pal);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DashboardWidget::tick);
    m_timer->start(1000); // 1s entre actualizaciones

    // Timer de parpadeo para alerta de sobrecalentamiento (500ms)
    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, &DashboardWidget::blinkTick);
    m_blinkTimer->start(500);
    m_blinkOn = true;
}

void DashboardWidget::blinkTick() {
    m_blinkOn = !m_blinkOn;
    // Solo forzar repintado si hay sobrecalentamiento crítico
    if (m_data.coolant >= 110) {
        update();
    }
}

void DashboardWidget::tick() {
    if (m_elm && !g_demoForced) {
        m_data = m_elm->getDashboardFast();
    } else {
        m_data = QtDisplay::generateDemoData();
    }
    update();
}

void DashboardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    float w = width();
    float h = height();

    // === Fondo oscuro con degradado ===
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0.0, QColor(16, 18, 28));
    bg.setColorAt(1.0, Theme::BG_DARK);
    p.fillRect(rect(), bg);

    // === 4 indicadores analógicos (grid 2x2 o 1x4 según width) ===
    float gaugeSize = std::min(w / 5.5f, h * 0.5f);
    float gaugeY = h * 0.35f;
    float spacing = w / 5.0f;
    QRectF rpmRect(spacing * 1 - gaugeSize / 2, gaugeY - gaugeSize / 2, gaugeSize, gaugeSize);
    QRectF speedRect(spacing * 2 - gaugeSize / 2, gaugeY - gaugeSize / 2, gaugeSize, gaugeSize);
    QRectF tempRect(spacing * 3 - gaugeSize / 2, gaugeY - gaugeSize / 2, gaugeSize, gaugeSize);
    QRectF loadRect(spacing * 4 - gaugeSize / 2, gaugeY - gaugeSize / 2, gaugeSize, gaugeSize);

    drawGauge(p, rpmRect, "RPM", m_data.rpm, 0, 8000, 5000, 6500, "x1000", Theme::GAUGE_ARC);
    drawGauge(p, speedRect, "VELOCIDAD", m_data.speed, 0, 260, 120, 180, "km/h", QColor(80, 200, 255));
    drawGauge(p, tempRect, "TEMP MOTOR", m_data.coolant, 40, 130, 100, 120, "°C", QColor(255, 160, 60));
    drawGauge(p, loadRect, "CARGA", m_data.load, 0, 100, 70, 90, "%", QColor(120, 255, 120));

    // === Indicador de sobrecalentamiento crítico (parpadeante) ===
    if (m_data.coolant >= 110) {
        p.save();
        float cx = tempRect.center().x();
        float cy = tempRect.center().y();
        float glowRadius = std::min(tempRect.width(), tempRect.height()) * 0.48f;

        // Brillo pulsante exterior (anillo rojo)
        if (m_blinkOn) {
            QRadialGradient glow(cx, cy, glowRadius * 1.3f);
            glow.setColorAt(0.0, QColor(255, 40, 40, 0));       // centro transparente
            glow.setColorAt(0.75, QColor(255, 40, 40, 0));      // interior del gauge, transparente
            glow.setColorAt(0.88, QColor(255, 60, 60, 160));    // borde del gauge, semitransparente
            glow.setColorAt(1.0, QColor(255, 20, 20, 60));      // exterior, tenue
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(QPointF(cx, cy), glowRadius * 1.3f, glowRadius * 1.3f);

            // Segundo anillo más intenso justo en el borde
            QPen pulsePen(QColor(255, 50, 50, 180), 3);
            p.setPen(pulsePen);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(cx, cy), glowRadius * 0.92f, glowRadius * 0.92f);
        }

        // Texto 🔥 CRITICAL parpadeante sobre el gauge de temperatura
        if (m_blinkOn) {
            QFont critFont("monospace", 12, QFont::Bold);
            p.setFont(critFont);
            p.setPen(QColor(255, 60, 60));
            float labelY = cy - gaugeSize * 0.65f;
            p.drawText(QRectF(cx - 80, labelY, 160, 24), Qt::AlignCenter, "🔥 CRITICAL");
        }

        p.restore();
    }

    // === Barras horizontales ===
    float barY = h * 0.72f;
    float barH = 18;
    float barW = w * 0.20f;
    float barSep = w * 0.04f;
    float barX1 = barSep;
    float barX2 = barSep * 2 + barW;
    float barX3 = barSep * 3 + barW * 2;
    float barX4 = barSep * 4 + barW * 3;

    drawBar(p, QRectF(barX1, barY, barW, barH), "ACELERADOR",
            m_data.throttle, 100, m_data.throttle > 80 ? Theme::BAR_WARN : Theme::BAR_NORMAL);
    drawBar(p, QRectF(barX2, barY, barW, barH), "MAF",
            m_data.maf, 200, Theme::GAUGE_ARC);
    drawBar(p, QRectF(barX3, barY, barW, barH), "COMBUSTIBLE",
            m_data.fuelLevel, 100, m_data.fuelLevel < 15 ? Theme::BAR_CRIT : Theme::BAR_NORMAL);
    drawBar(p, QRectF(barX4, barY, barW, barH), "AVANCE",
            m_data.timing + 64, 128, Theme::GRAPH_LINE);

    // === Valores digitales ===
    p.setPen(Theme::TEXT_DIM);
    QFont valFont("monospace", 11);
    p.setFont(valFont);
    float vy = h * 0.82f;
    float vsp = w / 6.0f;

    float o2v = 0.0f;
    if (m_elm && !g_demoForced) {
        auto o2 = m_elm->getO2Sensor(1, 1);
        o2v = static_cast<float>(o2.voltage);
    } else {
        o2v = static_cast<float>(QtDisplay::generateDemoO2().voltage);
    }

    double stft = (m_elm && !g_demoForced) ? m_elm->getShortTermTrimBank1() : QtDisplay::demoSTFT();
    double ltft = (m_elm && !g_demoForced) ? m_elm->getLongTermTrimBank1() : QtDisplay::demoLTFT();

    p.drawText(QRectF(vsp * 1 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("O2: %1V").arg(o2v, 0, 'f', 3));
    p.drawText(QRectF(vsp * 2 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("STFT: %1%").arg(stft, 0, 'f', 1));
    p.drawText(QRectF(vsp * 3 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("LTFT: %1%").arg(ltft, 0, 'f', 1));
    p.drawText(QRectF(vsp * 4 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("ADM: %1°C").arg(static_cast<double>(m_data.intakeTemp), 0, 'f', 0));
    p.drawText(QRectF(vsp * 5 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("MAF: %1 g/s").arg(m_data.maf, 0, 'f', 1));

    // === Indicador de escenario demo activo + DTCs (esquina superior derecha) ===
    if ((!m_elm || g_demoForced) && QtDisplay::demoPhase() != DemoPhase::Normal) {
        p.save();
        bool mil = QtDisplay::demoCheckEngine();
        QString phaseText = QtDisplay::demoPhaseName();
        QString dtcsText = QtDisplay::demoDTCsText();
        QColor phaseColor = mil ? QColor(255, 80, 80) : QColor(255, 200, 60);

        int tagW = 360;
        int tagH = 22;
        int dtcH = 0;
        QStringList dtcLines;
        if (mil && !dtcsText.isEmpty()) {
            dtcLines = dtcsText.split('\n');
            dtcH = static_cast<int>(dtcLines.size()) * 16 + 6;
        }
        int blockH = tagH + dtcH;
        int tagX = w - tagW - 10;
        int tagY = 10;  // ← Esquina superior derecha, sin solaparse con valores digitales

        // Fondo del bloque
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 20, 20));
        p.drawRoundedRect(tagX, tagY, tagW, blockH, 4, 4);

        // Texto del escenario
        QFont phaseFont("monospace", 10, QFont::Bold);
        p.setFont(phaseFont);
        p.setPen(phaseColor);
        p.drawText(QRectF(tagX + 4, tagY, tagW - 48, tagH), Qt::AlignLeft | Qt::AlignVCenter, phaseText);

        // MIL indicator
        if (mil) {
            QRectF milRect(tagX + tagW - 40, tagY + 3, 36, tagH - 6);
            p.setPen(QColor(255, 60, 60));
            p.setBrush(QColor(255, 60, 60, 40));
            p.drawRoundedRect(milRect, 3, 3);
            QFont milFont("monospace", 8, QFont::Bold);
            p.setFont(milFont);
            p.setPen(Qt::white);
            p.drawText(milRect, Qt::AlignCenter, "MIL");
        }

        // DTCs debajo del tag
        if (!dtcLines.isEmpty()) {
            QFont dtcFont("monospace", 8);
            p.setFont(dtcFont);
            p.setPen(QColor(255, 180, 100));
            int lineY = tagY + tagH + 2;
            for (const QString& line : dtcLines) {
                p.drawText(QRectF(tagX + 4, lineY, tagW - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, line);
                lineY += 16;
            }
        }

        p.restore();
    }
}

// Variable estática para escalar RPM automáticamente (máx histórico)
static double g_maxRPM = 0.0;

void DashboardWidget::drawGauge(QPainter& p, const QRectF& rect, const QString& label,
                                 float value, float minVal, float maxVal,
                                 float warnVal, float critVal,
                                 const QString& unit, const QColor& color) {
    p.save();

    float cx = rect.center().x();
    float cy = rect.center().y();
    float radius = std::min(rect.width(), rect.height()) * 0.42f;

    // Escala automática de RPM: ajusta maxVal al máximo histórico
    if (label == "RPM" && value > g_maxRPM) {
        g_maxRPM = value;
    }
    if (label == "RPM" && g_maxRPM > 800) {
        // Usar el máximo histórico con margen del 10%
        double autoMax = std::max(8000.0, g_maxRPM * 1.1);
        // Redondear hacia arriba al próximo múltiplo de 1000
        autoMax = std::ceil(autoMax / 1000.0) * 1000.0;
        maxVal = static_cast<float>(autoMax);
    }

    int startAngle = 225 * 16;
    int spanAngle = 270 * 16;

    // Arco de fondo
    QPen bgPen(QColor(50, 55, 70), 8);
    p.setPen(bgPen);
    p.drawArc(QRectF(cx - radius, cy - radius, radius * 2, radius * 2), startAngle, spanAngle);

    // Arco de valor (coloreado)
    float normalized = (value - minVal) / (maxVal - minVal);
    normalized = std::max(0.0f, std::min(1.0f, normalized));
    int valAngle = static_cast<int>(spanAngle * normalized);

    QColor arcColor = color;
    if (value >= critVal) arcColor = Theme::BAR_CRIT;
    else if (value >= warnVal) arcColor = Theme::BAR_WARN;

    QPen valPen(arcColor, 8);
    p.setPen(valPen);
    p.drawArc(QRectF(cx - radius, cy - radius, radius * 2, radius * 2), startAngle, valAngle);

    // Aguja
    static constexpr float PI = 3.14159265f;
    float needleRad = (225.0f + 270.0f * normalized) * PI / 180.0f;
    float needleLen = radius * 0.75f;
    float nx = cx + needleLen * std::cos(needleRad);
    float ny = cy + needleLen * std::sin(needleRad);

    QPen needlePen(Theme::GAUGE_NEEDLE, 3);
    p.setPen(needlePen);
    p.drawLine(QPointF(cx, cy), QPointF(nx, ny));

    // Centro
    p.setBrush(Theme::GAUGE_NEEDLE);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(cx, cy), 5, 5);

    QFont lblFont("monospace", 9);
    p.setFont(lblFont);
    p.setPen(Theme::TEXT_DIM);
    p.drawText(QRectF(cx - 50, cy + radius * 0.2f, 100, 16), Qt::AlignCenter, unit);

    QFont valFont("monospace", 14, QFont::Bold);
    p.setFont(valFont);
    p.setPen(Theme::TEXT_WHITE);
    p.drawText(QRectF(cx - 50, cy + radius * 0.08f, 100, 24), Qt::AlignCenter,
               QString::number(static_cast<int>(value)));

    QFont lblFont2("monospace", 10);
    p.setFont(lblFont2);
    p.setPen(Theme::TEXT_DIM);
    p.drawText(QRectF(cx - 50, cy - radius * 0.55f, 100, 16), Qt::AlignCenter, label);

    p.restore();
}

void DashboardWidget::drawBar(QPainter& p, const QRectF& rect, const QString& label,
                               float value, float maxVal, const QColor& color) {
    p.save();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 45, 60));
    p.drawRoundedRect(rect, 3, 3);

    float fill = std::max(0.0f, std::min(1.0f, value / maxVal));
    QRectF fillRect(rect.x(), rect.y(), rect.width() * fill, rect.height());
    p.setBrush(color);
    p.drawRoundedRect(fillRect, 3, 3);

    QFont f("monospace", 9);
    p.setFont(f);
    p.setPen(Theme::TEXT_WHITE);
    p.drawText(rect.adjusted(4, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, label);

    p.setPen(Theme::TEXT_DIM);
    QString valStr = QString::number(static_cast<int>(value));
    p.drawText(rect.adjusted(-4, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, valStr);

    p.restore();
}

// ============================================================================
// GraphWidget — Gráficos de líneas (QPainter)
// ============================================================================

#ifndef USE_QT5_CHARTS

GraphWidget::GraphWidget(ELM327* elm, QWidget* parent, bool demoMode)
    : QWidget(parent), m_elm(elm) {
    (void)demoMode;
    setMinimumSize(800, 500);
    setAutoFillBackground(true);

    QPalette pal;
    pal.setColor(QPalette::Window, Theme::BG_DARK);
    setPalette(pal);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &GraphWidget::tick);
    m_timer->start(1000);
}

void GraphWidget::tick() {
    ELM327::DashboardData d;
    if (m_elm && !g_demoForced) {
        d = m_elm->getDashboardFast();
    } else {
        d = QtDisplay::generateDemoData();
    }
    m_rpmHist.push_back(static_cast<float>(d.rpm));
    m_loadHist.push_back(static_cast<float>(d.load));
    m_throttleHist.push_back(static_cast<float>(d.throttle));
    m_mafHist.push_back(static_cast<float>(d.maf));

    while (m_rpmHist.size() > QT_MAX_HISTORY) m_rpmHist.pop_front();
    while (m_loadHist.size() > QT_MAX_HISTORY) m_loadHist.pop_front();
    while (m_throttleHist.size() > QT_MAX_HISTORY) m_throttleHist.pop_front();
    while (m_mafHist.size() > QT_MAX_HISTORY) m_mafHist.pop_front();

    update();
}

void GraphWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    float w = width();
    float h = height();
    float margin = 15;
    float graphW = (w - margin * 3) / 2.0f;
    float graphH = (h - margin * 3) / 2.0f;

    drawGraph(p, QRectF(margin, margin, graphW, graphH),
              m_rpmHist, "RPM", Theme::GAUGE_ARC, 0, 8000);
    drawGraph(p, QRectF(margin * 2 + graphW, margin, graphW, graphH),
              m_loadHist, "CARGA MOTOR (%)", QColor(120, 255, 120), 0, 100);
    drawGraph(p, QRectF(margin, margin * 2 + graphH, graphW, graphH),
              m_throttleHist, "ACELERADOR (%)", QColor(255, 180, 40), 0, 100);
    drawGraph(p, QRectF(margin * 2 + graphW, margin * 2 + graphH, graphW, graphH),
              m_mafHist, "MAF (g/s)", Theme::GRAPH_LINE, 0, 200);
}

void GraphWidget::drawGraph(QPainter& p, const QRectF& rect,
                             const std::deque<float>& data,
                             const QString& label, const QColor& color,
                             float minY, float maxY) {
    p.save();

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::BG_PANEL);
    p.drawRoundedRect(rect, 5, 5);

    drawGrid(p, rect, 4);

    QFont f("monospace", 10);
    p.setFont(f);
    p.setPen(Theme::TEXT_DIM);
    p.drawText(rect.adjusted(8, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, label);

    if (data.size() < 2) { p.restore(); return; }

    QPainterPath path;
    float range = maxY - minY;
    if (range < 0.001f) range = 1.0f;

    for (size_t i = 0; i < data.size(); i++) {
        float px = rect.left() + rect.width() * (1.0f - static_cast<float>(data.size() - i) / data.size());
        float py = rect.top() + rect.height() * (1.0f - (data[i] - minY) / range);
        py = std::max(static_cast<float>(rect.top()), std::min(static_cast<float>(rect.bottom()), py));

        if (i == 0) path.moveTo(px, py);
        else path.lineTo(px, py);
    }

    QPen linePen(color, 2);
    p.setPen(linePen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    if (!data.empty()) {
        QFont vf("monospace", 10, QFont::Bold);
        p.setFont(vf);
        p.setPen(color);
        QString valStr = QString::number(data.back(), 'f', 1);
        p.drawText(rect.adjusted(-8, 4, -8, 0), Qt::AlignRight | Qt::AlignTop, valStr);
    }

    p.restore();
}

void GraphWidget::drawGrid(QPainter& p, const QRectF& rect, int divisions) {
    p.save();
    QPen gridPen(Theme::GRID_LINE, 1);
    p.setPen(gridPen);

    for (int i = 1; i < divisions; i++) {
        float t = static_cast<float>(i) / divisions;
        p.drawLine(QPointF(rect.left(), rect.top() + rect.height() * t),
                   QPointF(rect.right(), rect.top() + rect.height() * t));
        p.drawLine(QPointF(rect.left() + rect.width() * t, rect.top()),
                   QPointF(rect.left() + rect.width() * t, rect.bottom()));
    }

    p.restore();
}

#endif // !USE_QT5_CHARTS

// ============================================================================
// ChartsGraphWidget — Gráficos con Qt5 Charts (QChart)
// ============================================================================

#ifdef USE_QT5_CHARTS

ChartsGraphWidget::ChartsGraphWidget(ELM327* elm, QWidget* parent, bool demoMode)
    : QWidget(parent), m_elm(elm), m_counter(0) {
    (void)demoMode;
    setMinimumSize(800, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Grid 2x2 de QChartViews
    QHBoxLayout* topRow = new QHBoxLayout();
    QHBoxLayout* bottomRow = new QHBoxLayout();

    m_seriesRPM = new QLineSeries();
    m_seriesRPM->setColor(QColor(60, 140, 255));
    m_chartRPM = createChart("RPM", QColor(60, 140, 255), 0, 8000);
    m_chartRPM->addSeries(m_seriesRPM);
    // Attach series to axes
    m_seriesRPM->attachAxis(m_chartRPM->axes(Qt::Horizontal).first());
    m_seriesRPM->attachAxis(m_chartRPM->axes(Qt::Vertical).first());
    m_viewRPM = createChartView(m_chartRPM);

    m_seriesLoad = new QLineSeries();
    m_seriesLoad->setColor(QColor(120, 255, 120));
    m_chartLoad = createChart("CARGA MOTOR (%)", QColor(120, 255, 120), 0, 100);
    m_chartLoad->addSeries(m_seriesLoad);
    m_seriesLoad->attachAxis(m_chartLoad->axes(Qt::Horizontal).first());
    m_seriesLoad->attachAxis(m_chartLoad->axes(Qt::Vertical).first());
    m_viewLoad = createChartView(m_chartLoad);

    m_seriesThrottle = new QLineSeries();
    m_seriesThrottle->setColor(QColor(255, 180, 40));
    m_chartThrottle = createChart("ACELERADOR (%)", QColor(255, 180, 40), 0, 100);
    m_chartThrottle->addSeries(m_seriesThrottle);
    m_seriesThrottle->attachAxis(m_chartThrottle->axes(Qt::Horizontal).first());
    m_seriesThrottle->attachAxis(m_chartThrottle->axes(Qt::Vertical).first());
    m_viewThrottle = createChartView(m_chartThrottle);

    m_seriesMAF = new QLineSeries();
    m_seriesMAF->setColor(QColor(80, 180, 255));
    m_chartMAF = createChart("MAF (g/s)", QColor(80, 180, 255), 0, 200);
    m_chartMAF->addSeries(m_seriesMAF);
    m_seriesMAF->attachAxis(m_chartMAF->axes(Qt::Horizontal).first());
    m_seriesMAF->attachAxis(m_chartMAF->axes(Qt::Vertical).first());
    m_viewMAF = createChartView(m_chartMAF);

    topRow->addWidget(m_viewRPM);
    topRow->addWidget(m_viewLoad);
    bottomRow->addWidget(m_viewThrottle);
    bottomRow->addWidget(m_viewMAF);

    mainLayout->addLayout(topRow);
    mainLayout->addLayout(bottomRow);

    // Timer
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ChartsGraphWidget::tick);
    m_timer->start(1000);
}

void ChartsGraphWidget::tick() {
    ELM327::DashboardData d;
    if (m_elm && !g_demoForced) {
        d = m_elm->getDashboardFast();
    } else {
        d = QtDisplay::generateDemoData();
    }

    // Limitar a QT_MAX_HISTORY puntos
    auto appendPoint = [this](QLineSeries* series, float value, int maxPts) {
        series->append(m_counter, value);
        while (series->count() > maxPts) {
            series->remove(0);
        }
    };

    appendPoint(m_seriesRPM, static_cast<float>(d.rpm), QT_MAX_HISTORY);
    appendPoint(m_seriesLoad, static_cast<float>(d.load), QT_MAX_HISTORY);
    appendPoint(m_seriesThrottle, static_cast<float>(d.throttle), QT_MAX_HISTORY);
    appendPoint(m_seriesMAF, static_cast<float>(d.maf), QT_MAX_HISTORY);

    m_counter++;
}

QChart* ChartsGraphWidget::createChart(const QString& title, const QColor& color,
                                        float minY, float maxY) {
    QChart* chart = new QChart();
    chart->setTitle(title);
    chart->setTitleBrush(Theme::TEXT_WHITE);
    chart->setBackgroundBrush(Theme::BG_PANEL);
    chart->setPlotAreaBackgroundBrush(Theme::BG_DARK);
    chart->setPlotAreaBackgroundVisible(true);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->legend()->hide();

    QValueAxis* axisX = new QValueAxis();
    axisX->setLabelsColor(Theme::TEXT_DIM);
    axisX->setGridLineColor(Theme::GRID_LINE);
    axisX->setRange(0, QT_MAX_HISTORY);
    axisX->setLabelFormat("%d");
    axisX->hide();

    QValueAxis* axisY = new QValueAxis();
    axisY->setLabelsColor(Theme::TEXT_DIM);
    axisY->setGridLineColor(Theme::GRID_LINE);
    axisY->setRange(minY, maxY);
    axisY->setLabelFormat("%.0f");

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);

    return chart;
}

QChartView* ChartsGraphWidget::createChartView(QChart* chart) {
    QChartView* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setBackgroundBrush(Theme::BG_DARK);
    return view;
}

#endif // USE_QT5_CHARTS

// ============================================================================
// O2ScopeWidget — Osciloscopio O2
// ============================================================================

O2ScopeWidget::O2ScopeWidget(ELM327* elm, QWidget* parent, bool demoMode)
    : QWidget(parent), m_elm(elm) {
    (void)demoMode;
    setMinimumSize(600, 400);
    setAutoFillBackground(true);

    QPalette pal;
    pal.setColor(QPalette::Window, Theme::O2_BG);
    setPalette(pal);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &O2ScopeWidget::tick);
    m_timer->start(200); // 200ms (~5 FPS)
}

void O2ScopeWidget::tick() {
    float voltage;
    if (m_elm && !g_demoForced) {
        auto o2 = m_elm->getO2Sensor(1, 1);
        voltage = static_cast<float>(o2.voltage);
    } else {
        voltage = QtDisplay::generateDemoO2().voltage;
    }
    m_o2Hist.push_back(voltage);
    while (m_o2Hist.size() > QT_MAX_HISTORY) m_o2Hist.pop_front();
    update();
}

void O2ScopeWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    float w = width();
    float h = height();
    float margin = 50;

    QRectF graphRect(margin, margin, w - margin * 2, h - margin * 2.2f);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::BG_PANEL);
    p.drawRoundedRect(graphRect, 5, 5);

    QFont titleFont("monospace", 13, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(Theme::O2_WAVE);
    p.drawText(QRectF(margin, 10, w - margin * 2, 30), Qt::AlignCenter,
               "Sensor O2 - Banco 1 Sensor 1");

    {
        QPen gridPen(Theme::GRID_LINE, 1);
        p.setPen(gridPen);
        for (int i = 1; i <= 4; i++) {
            float t = i / 5.0f;
            p.drawLine(QPointF(graphRect.left(), graphRect.top() + graphRect.height() * t),
                       QPointF(graphRect.right(), graphRect.top() + graphRect.height() * t));
        }
    }

    {
        QPen poorPen(QColor(100, 60, 60), 1, Qt::DashLine);
        p.setPen(poorPen);
        float poorY = graphRect.top() + graphRect.height() * (1.0f - 0.2f / 1.0f);
        p.drawLine(QPointF(graphRect.left(), poorY), QPointF(graphRect.right(), poorY));
        p.setPen(QColor(100, 60, 60));
        p.drawText(QRectF(graphRect.left() + 4, poorY - 14, 40, 14), Qt::AlignLeft, "0.2V pobre");

        QPen richPen(QColor(60, 100, 60), 1, Qt::DashLine);
        p.setPen(richPen);
        float richY = graphRect.top() + graphRect.height() * (1.0f - 0.8f / 1.0f);
        p.drawLine(QPointF(graphRect.left(), richY), QPointF(graphRect.right(), richY));
        p.setPen(QColor(60, 100, 60));
        p.drawText(QRectF(graphRect.left() + 4, richY - 14, 40, 14), Qt::AlignLeft, "0.8V rica");
    }

    {
        QFont axFont("monospace", 9);
        p.setFont(axFont);
        p.setPen(Theme::TEXT_DIM);
        p.drawText(QRectF(graphRect.left() - 36, graphRect.top() - 8, 32, 16), Qt::AlignRight, "1.0V");
        p.drawText(QRectF(graphRect.left() - 36, graphRect.bottom() - 8, 32, 16), Qt::AlignRight, "0.0V");
    }

    if (m_o2Hist.size() >= 2) {
        QPainterPath wave;
        for (size_t i = 0; i < m_o2Hist.size(); i++) {
            float px = graphRect.left() + graphRect.width() * (1.0f - static_cast<float>(m_o2Hist.size() - i) / m_o2Hist.size());
            float py = graphRect.top() + graphRect.height() * (1.0f - m_o2Hist[i] / 1.0f);
            py = std::max(static_cast<float>(graphRect.top()), std::min(static_cast<float>(graphRect.bottom()), py));
            if (i == 0) wave.moveTo(px, py);
            else wave.lineTo(px, py);
        }

        QPen wavePen(Theme::O2_WAVE, 2);
        p.setPen(wavePen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
    }

    float currO2 = m_o2Hist.empty() ? 0.0f : m_o2Hist.back();

    QFont valFont("monospace", 11);
    p.setFont(valFont);
    p.setPen(Theme::O2_WAVE);
    p.drawText(QRectF(graphRect.left(), graphRect.bottom() + 8, 200, 20), Qt::AlignLeft,
               QString("Valor: %1 V").arg(currO2, 0, 'f', 3));

    QString estado;
    QColor estadoColor;
    if (currO2 < 0.1f) { estado = "POBRE"; estadoColor = Theme::BAR_CRIT; }
    else if (currO2 > 0.9f) { estado = "RICA"; estadoColor = QColor(255, 200, 60); }
    else if (currO2 > 0.3f && currO2 < 0.7f) { estado = "NORMAL"; estadoColor = Theme::O2_WAVE; }
    else { estado = "TRANSICION"; estadoColor = Theme::BAR_WARN; }

    QFont ef("monospace", 13, QFont::Bold);
    p.setFont(ef);
    p.setPen(estadoColor);
    p.drawText(QRectF(graphRect.right() - 120, graphRect.bottom() + 8, 120, 20), Qt::AlignRight, estado);
}

// Incluir MOC generado para compatibilidad con Makefile
#include "moc_qt_display.cpp"
