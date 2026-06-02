cmake_minimum_required(VERSION 3.14)
project(elm327_app_v7
    VERSION 7.0
    DESCRIPTION "Escáner OBD-II profesional con interfaz gráfica Qt5"
    LANGUAGES CXX
)

# Estándar C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt5 automoc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# Dependencias: Qt5 + Bluetooth
# ============================================================================

# Qt5 Widgets (para la interfaz gráfica)
find_package(Qt5 REQUIRED COMPONENTS Widgets)
message(STATUS "Qt5 detectado: ${Qt5Widgets_VERSION}")

# Qt5 Charts (opcional, para gráficos QChart avanzados)
find_package(Qt5 QUIET COMPONENTS Charts)
if(Qt5Charts_FOUND)
    message(STATUS "Qt5 Charts detectado: ${Qt5Charts_VERSION} — usando QChart para gráficos")
    add_definitions(-DUSE_QT5_CHARTS)
else()
    message(STATUS "Qt5 Charts no instalado — usando QPainter para gráficos")
    message(STATUS "  Instalar: sudo apt install libqt5charts5-dev")
endif()

# Bluetooth (para conexión ELM327)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BLUEZ bluez)
endif()

# ============================================================================
# Fuentes
# ============================================================================

set(SOURCES
    src/main.cpp
    src/qt_display.cpp
    src/elm327.cpp
    src/gm_commands.cpp
    src/logger.cpp
    src/obd2_app.cpp
)

set(HEADERS
    include/elm327.hpp
    include/gm_commands.hpp
    include/logger.hpp
    include/obd2_app.hpp
    include/qt_display.hpp
)

# ============================================================================
# Definiciones del compilador
# ============================================================================

add_definitions(-DUSE_QT5)

if(BLUEZ_FOUND)
    add_definitions(-DUSE_BLUEZ)
endif()

# ============================================================================
# Ejecutable
# ============================================================================

add_executable(elm327_app_v7
    ${SOURCES}
    ${HEADERS}
)

# Directorios de includes
target_include_directories(elm327_app_v7 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Enlazar Qt5
target_link_libraries(elm327_app_v7 PRIVATE
    Qt5::Widgets
)

# Enlazar Qt5 Charts (opcional)
if(Qt5Charts_FOUND)
    target_link_libraries(elm327_app_v7 PRIVATE
        Qt5::Charts
    )
endif()

# Enlazar Bluetooth
if(BLUEZ_FOUND)
    target_link_libraries(elm327_app_v7 PRIVATE
        ${BLUEZ_LIBRARIES}
    )
    target_include_directories(elm327_app_v7 PRIVATE
        ${BLUEZ_INCLUDE_DIRS}
    )
    message(STATUS "Bluetooth (bluez) detectado: sí")
else()
    message(WARNING "Bluetooth (bluez) no detectado — compilando sin soporte Bluetooth")
endif()

# ============================================================================
# Flags del compilador
# ============================================================================

# Advertencias
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(elm327_app_v7 PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ============================================================================
# Targets auxiliares
# ============================================================================

# make run: ejecuta la aplicación
add_custom_target(run
    COMMAND ./elm327_app_v7
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS elm327_app_v7
)

# make clean_logs: limpia los archivos de log
add_custom_target(clean_logs
    COMMAND rm -rf logs/
    COMMENT "Logs eliminados"
)

# ============================================================================
# Instalación
# ============================================================================

install(TARGETS elm327_app_v7 DESTINATION bin)
