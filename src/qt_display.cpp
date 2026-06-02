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
#include <iostream>

// ============================================================================
// Helpers de datos simulados
// ============================================================================

ELM327::DashboardData QtDisplay::generateDemoData() {
    static unsigned int step = 0;
    step++;

    // Simular variación sinusoidal + ruido para datos realistas
    float t = step * 0.1f;
    ELM327::DashboardData d;
    d.valid = true;
    d.rpm     = static_cast<int>(750 + 500 * std::sin(t * 0.7f) + 200 * std::sin(t * 0.13f));
    d.speed   = static_cast<int>(60 + 40 * std::sin(t * 0.5f) + 10 * std::sin(t * 0.11f));
    d.coolant = static_cast<int>(85 + 8 * std::sin(t * 0.3f));
    d.load    = static_cast<int>(35 + 25 * std::sin(t * 0.6f));
    d.throttle = 20.0f + 15.0f * std::sin(t * 0.8f);
    d.maf      = 8.0f + 5.0f * std::sin(t * 0.65f);
    d.timing   = 12.0f + 8.0f * std::sin(t * 0.4f);
    d.intakeTemp = 40.0f + 5.0f * std::sin(t * 0.2f);
    d.fuelLevel  = 55.0f + 5.0f * std::sin(t * 0.1f);
    return d;
}

OxygenSensor QtDisplay::generateDemoO2() {
    static unsigned int step = 0;
    step++;

    // Onda cuadrada suavizada típica de sensor O2: alterna entre ~0.1V y ~0.9V
    float t = step * 0.05f;
    float wave = std::sin(t * 1.5f);
    float voltage = 0.5f + 0.4f * wave;
    // Agregar ruido
    voltage += ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 0.05f;
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > 1.0f) voltage = 1.0f;

    OxygenSensor o2;
    o2.voltage = voltage;
    o2.bank = 1;
    o2.sensor = 1;
    o2.shortTermTrim = voltage > 0.6f ? 5.0f : -5.0f;
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
}

QtDisplay::~QtDisplay() {
    saveScreenshot();
}

void QtDisplay::setActiveTab(int index) {
    if (m_tabs && index >= 0 && index < m_tabs->count()) {
        m_tabs->setCurrentIndex(index);
    }
}

void QtDisplay::saveScreenshot() {
    // Crear directorio screenshots/
    struct stat st;
    if (stat("screenshots", &st) != 0) {
        mkdir("screenshots", 0755);
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
}

void DashboardWidget::tick() {
    if (m_elm) {
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
    if (m_elm) {
        auto o2 = m_elm->getO2Sensor(1, 1);
        o2v = static_cast<float>(o2.voltage);
    } else {
        o2v = static_cast<float>(QtDisplay::generateDemoO2().voltage);
    }

    p.drawText(QRectF(vsp * 1 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("O2: %1V").arg(o2v, 0, 'f', 3));
    p.drawText(QRectF(vsp * 2 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("STFT: %1%").arg(m_elm ? m_elm->getShortTermTrimBank1() : (o2v > 0.6 ? 5.0 : -5.0), 0, 'f', 1));
    p.drawText(QRectF(vsp * 3 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("LTFT: %1%").arg(m_elm ? m_elm->getLongTermTrimBank1() : 2.5, 0, 'f', 1));
    p.drawText(QRectF(vsp * 4 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("ADM: %1°C").arg(static_cast<double>(m_data.intakeTemp), 0, 'f', 0));
    p.drawText(QRectF(vsp * 5 - 60, vy, 120, 20), Qt::AlignCenter,
               QString("MAF: %1 g/s").arg(m_data.maf, 0, 'f', 1));
}

void DashboardWidget::drawGauge(QPainter& p, const QRectF& rect, const QString& label,
                                 float value, float minVal, float maxVal,
                                 float warnVal, float critVal,
                                 const QString& unit, const QColor& color) {
    p.save();

    float cx = rect.center().x();
    float cy = rect.center().y();
    float radius = std::min(rect.width(), rect.height()) * 0.42f;

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
    if (m_elm) {
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
    if (m_elm) {
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
    if (m_elm) {
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
