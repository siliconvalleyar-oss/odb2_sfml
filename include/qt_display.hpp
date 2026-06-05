/**
 * @file qt_display.hpp
 * @brief Interfaz gráfica Qt5 para el escáner OBD-II.
 *
 * Proporciona tres modos de visualización con QPainter:
 * - Dashboard: 4 indicadores analógicos (RPM, velocidad, temperatura, carga).
 * - Gráficos: 4 gráficos de líneas en tiempo real.
 * - Osciloscopio O2: forma de onda del sensor de oxígeno.
 *
 * @note Requiere Qt5 (qtbase5-dev). Opcional: libqt5charts5-dev para gráficos QChart.
 * Compilar con CMake y find_package(Qt5 COMPONENTS Widgets Charts).
 */

#pragma once

#include "elm327.hpp"

#include <QWidget>
#include <QMainWindow>
#include <QTabWidget>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QApplication>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

#ifdef USE_QT5_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QCategoryAxis>
QT_CHARTS_USE_NAMESPACE
#endif

#include <deque>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>

// ============================================================================
// Constantes
// ============================================================================

/// @brief Número máximo de puntos históricos para gráficos de líneas.
const int QT_MAX_HISTORY = 360;

/// @brief Colores del tema oscuro profesional.
namespace Theme {
    const QColor BG_DARK(20, 22, 32);
    const QColor BG_PANEL(30, 34, 48);
    const QColor TEXT_WHITE(220, 225, 240);
    const QColor TEXT_DIM(140, 145, 160);
    const QColor GAUGE_ARC(60, 140, 255);
    const QColor GAUGE_NEEDLE(255, 80, 80);
    const QColor BAR_NORMAL(60, 200, 100);
    const QColor BAR_WARN(255, 180, 40);
    const QColor BAR_CRIT(255, 60, 60);
    const QColor GRAPH_LINE(80, 180, 255);
    const QColor GRID_LINE(50, 55, 70);
    const QColor O2_WAVE(100, 255, 100);
    const QColor O2_BG(25, 30, 25);
}

// ============================================================================
// Forward declarations
// ============================================================================

class DashboardWidget;
#ifndef USE_QT5_CHARTS
class GraphWidget;
#endif
#ifdef USE_QT5_CHARTS
class ChartsGraphWidget;
#endif
class O2ScopeWidget;

// ============================================================================
// QtDisplay — Ventana principal con pestañas
// ============================================================================

// ============================================================================
// Demo phases — escenarios simulados (paridad con v10)
// ============================================================================

/// @brief Fases del demo sincronizadas entre sensores.
enum class DemoPhase {
    Normal,
    AccelerationBrusca,  // ⚡ Aceleración brusca
    PostAccel,           // ↘ Desaceleración post-aceleración
    FallaSensor,         // ⚠ Falla intermitente de sensor
    RalentiIrregular,    // 🌀 Ralentí irregular
    DTCTrigger,          // 🔴 Disparo múltiple de DTC
    FuelPumpFailure,     // 💀 Presión combustible baja
    SlowO2Sensor,        // ⏳ Sensor O2 perezoso
    Overheating,         // 🔥 Sobrecalentamiento
    Recovery             // 🔧 Recuperación post-falla
};

/**
 * @brief Ventana principal con QTabWidget para los 3 modos de visualización.
 *
 * Uso:
 * @code
 * QtDisplay display(m_elm.get());
 * display.show();  // Muestra la ventana con las 3 pestañas
 * @endcode
 */
class QtDisplay : public QMainWindow {
    Q_OBJECT
public:
    /**
     * @brief Constructor.
     * @param elm Puntero al adaptador ELM327 conectado.
     * @param parent Widget padre (opcional).
     */
    explicit QtDisplay(ELM327* elm, QWidget* parent = nullptr, bool demoMode = false);

    /** @brief Destructor. Guarda screenshot y cierra. */
    ~QtDisplay();

    /**
     * @brief Selecciona la pestaña activa por índice.
     * @param index Índice de pestaña (0=Dashboard, 1=Gráficos, 2=Osciloscopio).
     */
    void setActiveTab(int index);

protected:
    void closeEvent(QCloseEvent* event) override {
        m_closed = true;
        saveScreenshot();
        QMainWindow::closeEvent(event);
    }

private:
    ELM327* m_elm;              ///< Puntero al ELM327 (null si demo mode)
    bool m_demoMode;             ///< True = generar datos simulados
    bool m_closed = false;       ///< Flag de ventana cerrada
    QPushButton* m_btnDemo;     ///< Botón de alternar demo en la barra de pestañas
    QTabWidget* m_tabs;         ///< Pestañas de modos
    DashboardWidget* m_dashboard; ///< Widget de dashboard
#ifdef USE_QT5_CHARTS
    ChartsGraphWidget* m_graphs;  ///< Widget de gráficos QChart
#else
    GraphWidget* m_graphs;       ///< Widget de gráficos QPainter
#endif
    O2ScopeWidget* m_scope;     ///< Widget de osciloscopio O2

    /** @brief Guarda captura de pantalla de la ventana actual. */
    void saveScreenshot();

private slots:
    /** @brief Alterna entre modo demo y datos reales. */
    void toggleDemo();

// --- Público para acceso desde widgets hijos ---
public:
    /** @brief Genera datos simulados de DashboardData. */
    static ELM327::DashboardData generateDemoData();

    /** @brief Genera voltaje simulado de sensor O2. */
    static OxygenSensor generateDemoO2();

    // --- Estado compartido de escenarios demo (fijo) ---
    static DemoPhase demoPhase();
    static void resetDemo();

    /** @brief Nombre legible del escenario actual. */
    static QString demoPhaseName();

    /** @brief Short-term fuel trim actual. */
    static double demoSTFT();

    /** @brief Long-term fuel trim actual. */
    static double demoLTFT();

    /** @brief Si el escenario actual debe encender MIL. */
    static bool demoCheckEngine();

    /** @brief Códigos DTC formateados (ej. "P0087\nP0171") para el escenario activo. */
    static QString demoDTCsText();
};

// ============================================================================
// DashboardWidget — Indicadores analógicos
// ============================================================================

/**
 * @brief Widget que dibuja 4 indicadores analógicos tipo aguja + barras.
 *
 * Actualiza datos del ELM327 cada ~1s vía QTimer.
 * Datos: RPM, velocidad, temperatura, carga, acelerador, MAF, combustible.
 */
class DashboardWidget : public QWidget {
    Q_OBJECT
public:
    explicit DashboardWidget(ELM327* elm, QWidget* parent = nullptr, bool demoMode = false);

    /** @brief Cambia la fuente de datos (real o demo según flag global). */
    void setELM(ELM327* elm) { m_elm = elm; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    /** @brief Obtiene nuevos datos del ELM327 y repinta. */
    void tick();

    /** @brief Alterna el estado de parpadeo del indicador de sobrecalentamiento. */
    void blinkTick();

private:
    ELM327* m_elm;
    QTimer* m_timer;
    QTimer* m_blinkTimer;        ///< Timer para parpadeo de alerta crítica
    bool m_blinkOn = true;       ///< Estado actual del parpadeo
    ELM327::DashboardData m_data;

    /// Dibuja un indicador analógico con aguja.
    void drawGauge(QPainter& p, const QRectF& rect, const QString& label,
                   float value, float minVal, float maxVal,
                   float warnVal, float critVal, const QString& unit,
                   const QColor& color);

    /// Dibuja una barra de progreso horizontal.
    void drawBar(QPainter& p, const QRectF& rect, const QString& label,
                 float value, float maxVal, const QColor& color);
};

#ifndef USE_QT5_CHARTS
// ============================================================================
// GraphWidget — Gráficos de líneas (QPainter)
// ============================================================================

/**
 * @brief Widget que dibuja 4 gráficos de líneas en tiempo real.
 *
 * Muestra historial de RPM, carga, acelerador y MAF.
 * Actualiza cada ~1s vía QTimer.
 */
class GraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit GraphWidget(ELM327* elm, QWidget* parent = nullptr, bool demoMode = false);

    /** @brief Cambia la fuente de datos. */
    void setELM(ELM327* elm) { m_elm = elm; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void tick();

private:
    ELM327* m_elm;
    QTimer* m_timer;

    std::deque<float> m_rpmHist;
    std::deque<float> m_loadHist;
    std::deque<float> m_throttleHist;
    std::deque<float> m_mafHist;

    /// Dibuja un gráfico de líneas.
    void drawGraph(QPainter& p, const QRectF& rect, const std::deque<float>& data,
                   const QString& label, const QColor& color,
                   float minY, float maxY);

    /// Dibuja una cuadrícula.
    void drawGrid(QPainter& p, const QRectF& rect, int divisions);
};
#endif

#ifdef USE_QT5_CHARTS
// ============================================================================
// ChartsGraphWidget — Gráficos con Qt5 Charts (QChart)
// ============================================================================

/**
 * @brief Widget con 4 gráficos QLineSeries en grid 2x2.
 *
 * Reemplaza a GraphWidget cuando libqt5charts5-dev está instalado.
 * Usa QChart + QChartView + QLineSeries con dark theme.
 * Actualiza cada ~1s vía QTimer.
 */
class ChartsGraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChartsGraphWidget(ELM327* elm, QWidget* parent = nullptr, bool demoMode = false);

    /** @brief Cambia la fuente de datos. */
    void setELM(ELM327* elm) { m_elm = elm; }

private slots:
    void tick();

private:
    ELM327* m_elm;
    QTimer* m_timer;

    QChart* m_chartRPM;
    QChart* m_chartLoad;
    QChart* m_chartThrottle;
    QChart* m_chartMAF;
    QChartView* m_viewRPM;
    QChartView* m_viewLoad;
    QChartView* m_viewThrottle;
    QChartView* m_viewMAF;

    QLineSeries* m_seriesRPM;
    QLineSeries* m_seriesLoad;
    QLineSeries* m_seriesThrottle;
    QLineSeries* m_seriesMAF;

    int m_counter;

    /** @brief Crea un QChart con tema oscuro y ejes. */
    QChart* createChart(const QString& title, const QColor& color,
                        float minY, float maxY);

    /** @brief Crea un QChartView envuelto en estilo oscuro. */
    QChartView* createChartView(QChart* chart);
};
#endif // USE_QT5_CHARTS

// ============================================================================
// O2ScopeWidget — Osciloscopio O2
// ============================================================================

/**
 * @brief Widget que dibuja la forma de onda del sensor O2 B1S1.
 *
 * Muestra voltaje del sensor, líneas de referencia pobre/rica,
 * y estado actual (POBRE / NORMAL / RICA / TRANSICION).
 * Actualiza cada ~200ms vía QTimer.
 */
class O2ScopeWidget : public QWidget {
    Q_OBJECT
public:
    explicit O2ScopeWidget(ELM327* elm, QWidget* parent = nullptr, bool demoMode = false);

    /** @brief Cambia la fuente de datos. */
    void setELM(ELM327* elm) { m_elm = elm; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void tick();

private:
    ELM327* m_elm;
    QTimer* m_timer;
    std::deque<float> m_o2Hist;
};
