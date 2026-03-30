/**
 * main.cpp
 *
 * Punto de entrada de la aplicación de edge computing LoRa
 * para el gateway Multitech MTCDT (mLinux 6.3.x).
 *
 * El proceso:
 *   1. Inicializa la librería Mosquitto.
 *   2. Instancia MqttLoraGateway, que se conecta al broker local.
 *   3. Ejecuta el loop de red de Mosquitto usando loop_forever(),
 *      que maneja automáticamente reconexiones y keepalives.
 *   4. Termina limpiamente ante SIGINT / SIGTERM (Ctrl-C o kill).
 *
 * Importante para mLinux:
 *   - El broker Mosquitto interno escucha en 127.0.0.1:1883 por defecto.
 *   - El LoRa Network Server publica en lora/<EUI>/up con QoS 1.
 *   - Esta aplicación re-publica los canales decodificados en
 *     lora/<EUI>/DI1, /DI2, /AI1..AI4, /DO — accesibles también
 *     externamente si el broker está configurado para ello.
 */

#include "mqtt_sample.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>   // gethostname()

// ============================================================
//  Signal handling para shutdown limpio
// ============================================================
static volatile bool g_running = true;

static void signalHandler(int signum)
{
    std::cout << "\n[INFO] Señal " << signum
              << " recibida, cerrando..." << std::endl;
    g_running = false;
}

// ============================================================
//  Generador de client_id único basado en hostname + PID
// ============================================================
static std::string makeClientId()
{
    char hostname[64] = "conduit";
    gethostname(hostname, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = '\0';

    std::ostringstream oss;
    oss << "lora_edge_" << hostname << "_" << getpid();
    return oss.str();
}

// ============================================================
//  main
// ============================================================
int main(int /*argc*/, char * /*argv*/[])
{
    // Instalar manejadores de señales
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Inicializar librería Mosquitto (una sola vez por proceso)
    mosqpp::lib_init();

    const std::string clientId = makeClientId();
    std::cout << "[INFO] Iniciando gateway LoRa edge, client_id="
              << clientId << std::endl;

    // Instanciar cliente MQTT; la conexión se intenta en el constructor
    MqttLoraGateway gateway(clientId.c_str(), BROKER_HOST, BROKER_PORT);

    /**
     * loop_forever() es el método correcto de mosquittopp:
     *   - Llama internamente a loop() con bloqueo (select/poll).
     *   - Gestiona keepalives automáticamente.
     *   - Reintenta la conexión ante desconexiones (reconnect_delay).
     *   - Retorna sólo si se llama a disconnect() o hay un error fatal.
     *
     * NO usar while(1) + loop() + reconnect() manualmente:
     *   - El busy-loop consume 100% de CPU.
     *   - loop_forever() ya maneja la reconexión internamente cuando
     *     se construye con reconnect_delay habilitado (por defecto).
     */
    int rc = gateway.loop_forever();

    if (rc != MOSQ_ERR_SUCCESS && g_running) {
        std::cerr << "[ERROR] loop_forever() terminó con error: "
                  << mosqpp::strerror(rc) << std::endl;
    }

    std::cout << "[INFO] Limpiando recursos Mosquitto." << std::endl;
    mosqpp::lib_cleanup();

    return (rc == MOSQ_ERR_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
