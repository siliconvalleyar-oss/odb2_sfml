/**
 * @file main.cpp
 * @brief Punto de entrada principal del escáner OBD-II ELM327 (Qt5).
 * @details Inicializa QApplication para la interfaz gráfica Qt5,
 *          luego ejecuta el bucle principal del menú interactivo
 *          de diagnóstico automotriz.
 */

#include "obd2_app.hpp"

#include <QApplication>

#include <memory>
#include <iostream>

/**
 * @brief Punto de entrada de la aplicación Qt5.
 *
 * Crea el QApplication con argumentos dummy para habilitar
 * el procesamiento de eventos Qt desde el menú de terminal.
 * Luego inicia la aplicación OBD::App que gestiona la conexión
 * ELM327, el menú y las ventanas gráficas Qt5.
 *
 * @param argc Número de argumentos de línea de comandos.
 * @param argv Vector de argumentos de línea de comandos.
 * @return 0 al finalizar correctamente.
 */
int main(int argc, char* argv[]) {
    // Inicializar QApplication ANTES de cualquier widget Qt
    QApplication qtApp(argc, argv);
    qtApp.setApplicationName("OBD-II Scanner Qt5");
    qtApp.setApplicationVersion("7.0");
    qtApp.setOrganizationName("Freebuff");

    // Crear y ejecutar la aplicación de diagnóstico
    auto device = std::make_unique<OBD::App>();
    device->run();

    return 0;
}
