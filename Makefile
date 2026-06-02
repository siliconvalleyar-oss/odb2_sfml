#!/usr/bin/env make
# Makefile simplificado para ELM327 OBD-II v7 (Qt5)
# Alternativa a CMake para compilación rápida

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -MMD -MP -fPIC -Wno-unused-parameter
CXXFLAGS += -I$(OBJ_DIR)
LDLIBS   = -lbluetooth -lpthread

# Qt5 support
QT5_CFLAGS := $(shell pkg-config --cflags Qt5Widgets 2>/dev/null)
QT5_LIBS   := $(shell pkg-config --libs   Qt5Widgets 2>/dev/null)

ifneq ($(QT5_LIBS),)
    CXXFLAGS += $(QT5_CFLAGS) -DUSE_QT5
    LDLIBS   += $(QT5_LIBS)
    $(info ✅ Qt5 detectado - Interfaz gráfica disponible)
else
    $(warning ⚠️  Qt5 no instalado. Instalar: sudo apt install qtbase5-dev)
    $(error Qt5 es requerido para obd2_v7)
endif

# Qt5 Charts support (opcional)
QT5_CHARTS_CFLAGS := $(shell pkg-config --cflags Qt5Charts 2>/dev/null)
QT5_CHARTS_LIBS   := $(shell pkg-config --libs   Qt5Charts 2>/dev/null)

ifneq ($(QT5_CHARTS_LIBS),)
    CXXFLAGS += $(QT5_CHARTS_CFLAGS) -DUSE_QT5_CHARTS
    LDLIBS   += $(QT5_CHARTS_LIBS)
    $(info ✅ Qt5 Charts detectado - Graficos QChart disponibles)
else
    $(info ℹ️  Qt5 Charts no detectado - Usando QPainter para graficos)
    $(info    Instalar: sudo apt install libqt5charts5-dev)
endif

# Directorios
SRC_DIR  = src
INC_DIR  = include
OBJ_DIR  = obj
BIN_DIR  = bin

# Archivos fuente - buscar todos los .cpp en src/
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/elm327_app_v7

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)
	@echo "Directorio de objetos: $(OBJ_DIR)"
	@echo "Directorio de binarios: $(BIN_DIR)"

$(TARGET): $(OBJS)
	@echo "=== Vinculando $@ ==="
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -o $@ $^ $(LDLIBS)
	@echo "✅ Compilación exitosa!"
	@ls -lh $@

# Regla genérica para compilar cualquier .cpp
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "  Compilando $<..."
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c -o $@ $<

# Regla específica para qt_display.o: requiere MOC generado primero
$(OBJ_DIR)/qt_display.o: $(SRC_DIR)/qt_display.cpp $(OBJ_DIR)/moc_qt_display.cpp
	@echo "  Compilando $<..."
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c -o $@ $<

# Auto-generación de dependencias MOC para Qt
# Usar los mismos defines que el compilador (importante para USE_QT5_CHARTS)
MOC_FLAGS = $(filter -D%,$(CXXFLAGS))
MOC_HEADERS = include/qt_display.hpp
$(OBJ_DIR)/moc_%.cpp: include/%.hpp
	@echo "  Generando MOC para $<..."
	moc $(MOC_FLAGS) $< -o $@

# Incluir dependencias generadas por MMD
-include $(OBJ_DIR)/*.d

clean:
	@echo "=== Limpiando ==="
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "✅ Limpieza completada"

# Generar documentación Doxygen
docs:
	@echo "=== Generando documentación Doxygen ==="
	doxygen Doxyfile 2>&1 || echo "⚠️  doxygen no instalado. Instalar con: sudo apt install doxygen graphviz"
	@echo "📄 Documentación generada en docs/doxygen/html/"
	@echo "   Abrir en navegador: docs/doxygen/html/index.html"

# Mostrar información de depuración
debug:
	@echo "=== INFORMACIÓN DE DEBUG ==="
	@echo "SRC_DIR: $(SRC_DIR)"
	@echo "INC_DIR: $(INC_DIR)"
	@echo "OBJ_DIR: $(OBJ_DIR)"
	@echo "BIN_DIR: $(BIN_DIR)"
	@echo "SRCS encontrados: $(SRCS)"
	@echo "OBJS generados: $(OBJS)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDLIBS: $(LDLIBS)"
	@echo "============================"
