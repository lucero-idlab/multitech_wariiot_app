#ifndef MQTT_SAMPLE_H
#define MQTT_SAMPLE_H

#include <mosquittopp.h>
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
//  Constantes del broker MQTT local del Conduit (mLinux)
// ============================================================
static const char *BROKER_HOST    = "127.0.0.1";
static const int   BROKER_PORT    = 1883;
static const int   KEEPALIVE_SECS = 60;

// Tópico de uplink publicado por el LoRa Network Server
// Formato: lora/<DEV-EUI>/up   (usamos wildcard '+' para cualquier EUI)
static const char *SUB_TOPIC_UP   = "lora/+/up";

// Prefijo de publicación para los valores procesados
static const char *PUB_PREFIX     = "WARIIOT";

// ============================================================
//  Identificadores de tipo de dispositivo (primeros 3 bytes)
// ============================================================
static const std::string DEVICE_TYPE_AIDI421A = "aaaa00";   // confirmado con tráfico real
static const std::string DEVICE_TYPE_AIRTD402 = "aacc00"; 

// ============================================================
//  Clase principal
// ============================================================
class MqttLoraGateway : public mosqpp::mosquittopp
{
public:
    /**
     * @param client_id  ID único del cliente MQTT. Usar un ID fijo
     *                   o generado (p.ej. basado en hostname) para
     *                   evitar colisiones en el broker.
     * @param host       Host del broker MQTT (generalmente 127.0.0.1)
     * @param port       Puerto del broker MQTT (generalmente 1883)
     */
    MqttLoraGateway(const char *client_id, const char *host, int port);
    virtual ~MqttLoraGateway();

    // Callbacks de la librería mosquittopp
    void on_connect(int rc)                                              override;
    void on_disconnect(int rc)                                           override;
    void on_message(const struct mosquitto_message *message)             override;
    void on_subscribe(int mid, int qos_count, const int *granted_qos)   override;
    void on_publish(int mid)                                             override;

private:
    // --------------------------------------------------------
    //  Helpers de bajo nivel
    // --------------------------------------------------------

    /**
     * Decodifica una cadena Base64 a binario raw.
     * El LNS publica el payload cifrado/descifrado en Base64.
     */
    static std::string base64Decode(const std::string &in);

    /**
     * Convierte un buffer binario a su representación hexadecimal
     * en minúsculas (sin separadores).
     */
    static std::string binToHex(const std::string &bin);

    /**
     * Extrae el segundo token (DEV-EUI) del tópico MQTT.
     * Ejemplo: "lora/00-80-00-00-00-00-e1-9c/up" → "00-80-00-00-00-00-e1-9c"
     */
    static std::string extractEuiFromTopic(const std::string &topic);

    /**
     * Publica un valor de telemetría en el broker local.
     * QoS 1, retain false (por defecto para datos en tiempo real).
     */
    void publishValue(const std::string &topic, const std::string &payload);

    // --------------------------------------------------------
    //  Decodificadores por tipo de dispositivo
    // --------------------------------------------------------

    /**
     * Decodifica el payload del dispositivo AIDI-421A
     * (2 DI, 4 AI, 1 DO) y publica cada canal.
     *
     * Mapa de bytes del payload (hex, cada carácter = 1 nibble):
     *   Bytes  0-2  (chars 0-5)  : tipo de dispositivo
     *   Byte   3    (char  6)    : DI1
     *   Byte   4    (char  7)    : DI2  (corrección respecto al código original)
     *   Bytes  4-5  (chars 8-11) : AI1  (uint16 big-endian)
     *   Bytes  6-7  (chars 12-15): AI2
     *   Bytes  8-9  (chars 16-19): AI3
     *   Bytes 10-11 (chars 20-23): AI4
     *   Byte  12    (chars 24-25): DO
     *
     * NOTA: los valores AI se interpretan como enteros sin signo de 16 bits.
     *       Si el sensor entrega valores con escala, aplicar factor aquí.
     */
    void decodeAidi421a(const std::string &eui, const std::string &hexData);

    /**
     * Decodifica el payload del dispositivo AIRTD-4AI0RTD2DI
     * ( 4 AI, 0 RTD 2 DI 0 DO) y publica cada canal.
     *
     * Mapa de bytes del payload (hex, cada carácter = 1 nibble):
     *   Bytes  0-2  (chars 0-5)  : tipo de dispositivo
     *   Byte   3    (char  6)    : DI1
     *   Byte   4    (char  7)    : DI2  (corrección respecto al código original)
     *   Bytes  4-5  (chars 8-11) : AI1  (uint16 big-endian)
     *   Bytes  6-7  (chars 12-15): AI2
     *   Bytes  8-9  (chars 16-19): AI3
     *   Bytes 10-11 (chars 20-23): AI4

     *   Bytes  4-5  (chars 8-11) : AI1  (uint16 big-endian)
     *   Bytes  6-7  (chars 12-15): AI2
     *   Bytes  8-9  (chars 16-19): AI3
     *   Bytes 10-11 (chars 20-23): AI4
     *
     * NOTA: los valores AI se interpretan como enteros sin signo de 16 bits.
     *       Si el sensor entrega valores con escala, aplicar factor aquí.
     */
    void decodeAirtd402(const std::string &eui, const std::string &hexData);



    // --------------------------------------------------------
    //  Estado interno
    // --------------------------------------------------------
    bool          m_connected;
    Json::CharReaderBuilder m_readerBuilder;  // jsoncpp >= 1.x (no deprecado)
};

#endif // MQTT_SAMPLE_H
