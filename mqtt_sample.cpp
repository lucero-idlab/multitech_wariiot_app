/**
 * mqtt_sample.cpp  —  lora_edge_gateway
 *
 * CAMBIOS RESPECTO A LA VERSIÓN ANTERIOR:
 *
 *  FIX 1 — MIN_HEX_LEN corregido a 26 chars (13 bytes).
 *           La versión anterior exigía 28 chars (14 bytes) pero el
 *           dispositivo AIDI-421A envía exactamente 13 bytes según el
 *           layout del código original que funcionaba.
 *
 *  FIX 2 — Offsets de DI1/DI2 corregidos.
 *           El código anterior leía DI1 en chars [6..7] (1 byte completo)
 *           y AI1 en chars [10..13], dejando 2 chars muertos en [8..9].
 *           El layout real del AIDI-421A es:
 *             bytes 0-2  (chars  0-5)  : device type
 *             byte  3    (chars  6-7)  : DI1  (1 byte)
 *             byte  4    (chars  8-9)  : DI2  (1 byte)
 *             bytes 5-6  (chars 10-13) : AI1  (uint16 BE)
 *             bytes 7-8  (chars 14-17) : AI2
 *             bytes 9-10 (chars 18-21) : AI3
 *             bytes 11-12(chars 22-25) : AI4
 *             —— total 13 bytes / 26 chars ——
 *           El DO (byte 13) era opcional en el dispositivo original y
 *           se omite hasta confirmar el layout real con mosquitto_sub.
 *
 *  FIX 3 — Campo JSON flexible.
 *           El LoRa Network Server del Conduit puede publicar el payload
 *           en "data", "payload" o "payload_raw" según la versión del
 *           firmware. Se intenta cada campo en orden.
 *
 *  FIX 4 — Log de depuración ampliado.
 *           Se imprime el JSON crudo completo y el hex decodificado para
 *           poder diagnosticar problemas sin recompilar.
 *
 *  FIX 5 — Eliminado strtok() (mutaba el buffer de topic en la versión
 *           original). Reemplazado por extractEuiFromTopic() con find().
 */

#include "mqtt_sample.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// ============================================================
//  Base64
// ============================================================
static const std::string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string MqttLoraGateway::base64Decode(const std::string &in)
{
    static int  lookupTable[256];
    static bool tableInitialized = false;
    if (!tableInitialized) {
        std::fill(lookupTable, lookupTable + 256, -1);
        for (int i = 0; i < 64; ++i)
            lookupTable[static_cast<unsigned char>(BASE64_CHARS[i])] = i;
        tableInitialized = true;
    }

    std::string out;
    out.reserve(in.size() * 3 / 4);

    int accumulator = 0;
    int bitsInAcc   = -8;

    for (unsigned char c : in) {
        int val = lookupTable[c];
        if (val == -1) break;           // '=' de relleno u otro no-B64
        accumulator = (accumulator << 6) + val;
        bitsInAcc  += 6;
        if (bitsInAcc >= 0) {
            out.push_back(static_cast<char>((accumulator >> bitsInAcc) & 0xFF));
            bitsInAcc -= 8;
        }
    }
    return out;
}

// ============================================================
//  binToHex
// ============================================================
std::string MqttLoraGateway::binToHex(const std::string &bin)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : bin)
        oss << std::setw(2) << static_cast<unsigned int>(c);
    return oss.str();
}

// ============================================================
//  extractEuiFromTopic
//  "lora/<EUI>/up" → "<EUI>"
// ============================================================
std::string MqttLoraGateway::extractEuiFromTopic(const std::string &topic)
{
    std::size_t first = topic.find('/');
    if (first == std::string::npos) return "";

    std::size_t second = topic.find('/', first + 1);
    if (second == std::string::npos) return "";

    return topic.substr(first + 1, second - first - 1);
}

// ============================================================
//  Constructor / Destructor
// ============================================================
MqttLoraGateway::MqttLoraGateway(const char *client_id,
                                  const char *host,
                                  int         port)
    : mosqpp::mosquittopp(client_id)
    , m_connected(false)
{
    m_readerBuilder["collectComments"] = false;
    m_readerBuilder["allowComments"]   = false;
    m_readerBuilder["strictRoot"]      = false;

    int rc = connect(host, port, KEEPALIVE_SECS);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[ERROR] connect() inicial: "
                  << mosqpp::strerror(rc) << std::endl;
    }
}

MqttLoraGateway::~MqttLoraGateway()
{
    if (m_connected)
        disconnect();
}

// ============================================================
//  Callbacks
// ============================================================
void MqttLoraGateway::on_connect(int rc)
{
    if (rc == MOSQ_ERR_SUCCESS) {
        m_connected = true;
        std::cout << "[INFO] Conectado al broker MQTT." << std::endl;

        int sub_rc = subscribe(nullptr, SUB_TOPIC_UP, 1);
        if (sub_rc != MOSQ_ERR_SUCCESS)
            std::cerr << "[ERROR] subscribe(): "
                      << mosqpp::strerror(sub_rc) << std::endl;
    } else {
        m_connected = false;
        std::cerr << "[WARN] on_connect rc=" << rc
                  << " (" << mosqpp::strerror(rc) << ")" << std::endl;
    }
}

void MqttLoraGateway::on_disconnect(int rc)
{
    m_connected = false;
    if (rc != 0)
        std::cerr << "[WARN] Desconexión inesperada rc=" << rc << std::endl;
    else
        std::cout << "[INFO] Desconectado limpiamente." << std::endl;
}

void MqttLoraGateway::on_subscribe(int /*mid*/,
                                    int   qos_count,
                                    const int *granted_qos)
{
    std::cout << "[INFO] Suscripción confirmada (QoS:";
    for (int i = 0; i < qos_count; ++i)
        std::cout << " " << granted_qos[i];
    std::cout << ")." << std::endl;
}

void MqttLoraGateway::on_publish(int /*mid*/) {}

void MqttLoraGateway::on_message(const struct mosquitto_message *message)
{
    if (!message || !message->payload || message->payloadlen == 0) {
        std::cerr << "[WARN] Mensaje vacío recibido." << std::endl;
        return;
    }

    // 1. EUI del tópico
    const std::string topic(message->topic);
    const std::string eui = extractEuiFromTopic(topic);
    if (eui.empty()) {
        std::cerr << "[WARN] No se pudo extraer EUI del tópico: "
                  << topic << std::endl;
        return;
    }

    // 2. Parsear JSON
    const std::string payload(static_cast<const char *>(message->payload),
                              static_cast<std::size_t>(message->payloadlen));

    // LOG completo del JSON crudo — clave para diagnóstico
    std::cout << "[DEBUG] topic=" << topic
              << " payload=" << payload << std::endl;

    Json::Value root;
    std::string parseErrors;
    std::unique_ptr<Json::CharReader> reader(m_readerBuilder.newCharReader());

    bool ok = reader->parse(payload.c_str(),
                            payload.c_str() + payload.size(),
                            &root,
                            &parseErrors);
    if (!ok) {
        std::cerr << "[ERROR] JSON inválido en " << topic
                  << ": " << parseErrors << std::endl;
        return;
    }

    // 3. Extraer campo base64
    //    FIX 3: intentar varios nombres de campo que usa el LNS del Conduit
    std::string base64Data;
    const char *dataFields[] = { "data", "payload", "payload_raw", nullptr };
    for (int i = 0; dataFields[i] != nullptr; ++i) {
        if (root.isMember(dataFields[i]) && root[dataFields[i]].isString()) {
            base64Data = root[dataFields[i]].asString();
            std::cout << "[DEBUG] Campo de datos encontrado: '"
                      << dataFields[i] << "'" << std::endl;
            break;
        }
    }

    if (base64Data.empty()) {
        // Imprimir las claves disponibles para diagnóstico
        std::cerr << "[WARN] No se encontró campo de datos. Claves en JSON:";
        for (const auto &key : root.getMemberNames())
            std::cerr << " '" << key << "'";
        std::cerr << std::endl;
        return;
    }

    // 4. Decodificar base64 → hex
    const std::string binaryData = base64Decode(base64Data);
    const std::string hexData    = binToHex(binaryData);

    std::cout << "[DEBUG] base64='" << base64Data << "'"
              << " hex='"  << hexData  << "'"
              << " len="   << binaryData.size() << " bytes" << std::endl;

    // 5. Identificar tipo de dispositivo (primeros 3 bytes = 6 chars hex)
    if (hexData.size() < 6) {
        std::cerr << "[WARN] Payload demasiado corto ("
                  << hexData.size() << " chars)." << std::endl;
        return;
    }

    const std::string deviceType = hexData.substr(0, 6);
    std::cout << "[DEBUG] device_type='" << deviceType << "'" << std::endl;

    if (deviceType == DEVICE_TYPE_AIDI421A) {
        std::cout << "[INFO] Dispositivo AIDI-421A (AAAA), eui=" << eui << std::endl;
        decodeAidi421a(eui, hexData);

    } else if (deviceType == DEVICE_TYPE_AIRTD402) {
        std::cout << "[INFO] Dispositivo AIRTD402 (AACC) eui=" << eui << std::endl;
	decodeAirtd402(eui, hexData);

    } else {
        std::cerr << "[WARN] device_type desconocido: '"
                  << deviceType << "' eui=" << eui << std::endl;
    }
}

// ============================================================
//  Decodificador AIDI-421A
//
//  Layout REAL confirmado (12 bytes = 24 chars hex):
//  Verificado con dos payloads reales del sensor:
//    "qswA////////////"    → "aacc00ff ffffffffffff"        (sin sensores)
//    "qswA/wfQB9ACYwIG"   → "aacc00ff 07d007d002630206"   (con sensores)
//  y correlacionado con el debug del firmware del propio dispositivo.
//
//  Mapa de bytes:
//    bytes  0-2  (chars  0-5)  : device type = "aacc00"
//    byte   3    (chars  6-7)  : DI byte
//                                  DI1 = byte3 & 0x0F        (nibble bajo)
//                                  DI2 = (byte3 >> 4) & 0x0F (nibble alto)
//                                  0xF = sin señal / no conectado
//    bytes  4-5  (chars  8-11) : AI1  (uint16 BE, 0xFFFF = canal OFF)
//    bytes  6-7  (chars 12-15) : AI2  (uint16 BE)
//    bytes  8-9  (chars 16-19) : AI3  (uint16 BE)
//    bytes 10-11 (chars 20-23) : AI4  (uint16 BE)
//
//  Factor de escala AI confirmado:
//    corriente_mA = raw_uint16 / 100.0
//    Ejemplo: raw=2000 → 20.000 mA | raw=611 → 6.110 mA | raw=518 → 5.180 mA
//
//  Total: 3 + 1 + 2 + 2 + 2 + 2 = 12 bytes ✓
// ============================================================

// Escala para convertir raw AI a mA (confirmado con valores del sensor)
static const double AI_SCALE = 100.0;

void MqttLoraGateway::decodeAidi421a(const std::string &eui,
                                      const std::string &hexData)
{
    static const std::size_t MIN_HEX_LEN = 26; // 12 bytes mínimos

    if (hexData.size() < MIN_HEX_LEN) {
        std::cerr << "[ERROR] AIDI-421A: payload corto ("
                  << hexData.size() << " chars, mínimo " << MIN_HEX_LEN
                  << "). hex='" << hexData << "'" << std::endl;
        return;
    }

    try {
        // --- byte 3: DI1 (nibble bajo) y DI2 (nibble alto) ---
        const uint8_t diByte = static_cast<uint8_t>(std::stoul(hexData.substr(6, 2), nullptr, 16));
        const uint8_t di1Val = diByte & 0x0F;
        const uint8_t di2Val = (diByte >> 4) & 0x0F;

        // --- bytes 4-11: cuatro AI de 16 bits big-endian ---
        const uint16_t ai1Raw = static_cast<uint16_t>(std::stoul(hexData.substr(8,  4), nullptr, 16));
        const uint16_t ai2Raw = static_cast<uint16_t>(std::stoul(hexData.substr(12, 4), nullptr, 16));
        const uint16_t ai3Raw = static_cast<uint16_t>(std::stoul(hexData.substr(16, 4), nullptr, 16));
        const uint16_t ai4Raw = static_cast<uint16_t>(std::stoul(hexData.substr(20, 4), nullptr, 16));
	
	// DO: byte extra al final
        const uint8_t doRaw = static_cast<uint8_t>(std::stoul(hexData.substr(24, 2), nullptr, 16));
	uint8_t doVal ;
        //std::string doStr;
        switch (doRaw) {
            case 0x00: doVal = 0;      break;
            case 0x01: doVal = 1;       break;
            default: doVal = 3; break;
            
        }
	std::ostringstream doStr;
        doStr << std::fixed << std::setprecision(0) << (doVal);


        const bool ai1Off = (ai1Raw == 0xFFFF);
        const bool ai2Off = (ai2Raw == 0xFFFF);
        const bool ai3Off = (ai3Raw == 0xFFFF);
        const bool ai4Off = (ai4Raw == 0xFFFF);

        // Valores en mA con 3 decimales
        std::ostringstream ai1Str, ai2Str, ai3Str, ai4Str;
        ai1Str << std::fixed << std::setprecision(3) << (ai1Raw / AI_SCALE);
        ai2Str << std::fixed << std::setprecision(3) << (ai2Raw / AI_SCALE);
        ai3Str << std::fixed << std::setprecision(3) << (ai3Raw / AI_SCALE);
        ai4Str << std::fixed << std::setprecision(3) << (ai4Raw / AI_SCALE);

        std::cout << "[DATA] eui=" << eui
                  << " DI1="  << static_cast<unsigned>(di1Val)
                  << " DI2="  << static_cast<unsigned>(di2Val)
                  << " AI1="  << (ai1Off ? "null" : ai1Str.str() )
                  << " AI2="  << (ai2Off ? "null" : ai2Str.str() )
                  << " AI3="  << (ai3Off ? "null" : ai3Str.str() )
                  << " AI4="  << (ai4Off ? "null" : ai4Str.str() )
                  << std::endl;

        const std::string base = std::string(PUB_PREFIX) + "/AIDI/" + eui + "/";

        // DI: publicar el nibble (0-15); 0xF = sin señal
	auto diToStr = [](uint8_t v)->std::string{
		if(v==0) return "0";
		if(v==1) return "1";
		return "null";
	};
        //publishValue(base + "DI1", std::to_string(static_cast<unsigned>(di1Val)));
        //publishValue(base + "DI2", std::to_string(static_cast<unsigned>(di2Val)));
	publishValue(base + "DI1", diToStr(di1Val));
	publishValue(base + "DI2", diToStr(di2Val));

        // AI: publicar valor en mA con 3 decimales; cadena vacía si canal OFF
        publishValue(base + "AI1", ai1Off ? "null" : ai1Str.str());
        publishValue(base + "AI2", ai2Off ? "null" : ai2Str.str());
        publishValue(base + "AI3", ai3Off ? "null" : ai3Str.str());
        publishValue(base + "AI4", ai4Off ? "null" : ai4Str.str());

	publishValue(base + "DO", (doVal==3) ? "null" : doStr.str());

    } catch (const std::exception &ex) {
        std::cerr << "[ERROR] decodeAidi421a() excepción: "
                  << ex.what() << std::endl;
    }
}

void MqttLoraGateway::decodeAirtd402(const std::string &eui,
                                      const std::string &hexData)
{
    static const std::size_t MIN_HEX_LEN = 24; // 12 bytes mínimos

    if (hexData.size() < MIN_HEX_LEN) {
        std::cerr << "[ERROR] AIRTD: payload corto ("
                  << hexData.size() << " chars, mínimo " << MIN_HEX_LEN
                  << "). hex='" << hexData << "'" << std::endl;
        return;
    }

    try {
        // --- byte 3: DI1 (nibble bajo) y DI2 (nibble alto) ---
        const uint8_t diByte = static_cast<uint8_t>(std::stoul(hexData.substr(6, 2), nullptr, 16));
        const uint8_t di1Val = diByte & 0x0F;
        const uint8_t di2Val = (diByte >> 4) & 0x0F;

        // --- bytes 4-11: cuatro AI de 16 bits big-endian ---
        const uint16_t ai1Raw = static_cast<uint16_t>(std::stoul(hexData.substr(8,  4), nullptr, 16));
        const uint16_t ai2Raw = static_cast<uint16_t>(std::stoul(hexData.substr(12, 4), nullptr, 16));
        const uint16_t ai3Raw = static_cast<uint16_t>(std::stoul(hexData.substr(16, 4), nullptr, 16));
        const uint16_t ai4Raw = static_cast<uint16_t>(std::stoul(hexData.substr(20, 4), nullptr, 16));

        const bool ai1Off = (ai1Raw == 0xFFFF);
        const bool ai2Off = (ai2Raw == 0xFFFF);
        const bool ai3Off = (ai3Raw == 0xFFFF);
        const bool ai4Off = (ai4Raw == 0xFFFF);

        // Valores en mA con 3 decimales
        std::ostringstream ai1Str, ai2Str, ai3Str, ai4Str;
        ai1Str << std::fixed << std::setprecision(3) << (ai1Raw / AI_SCALE);
        ai2Str << std::fixed << std::setprecision(3) << (ai2Raw / AI_SCALE);
        ai3Str << std::fixed << std::setprecision(3) << (ai3Raw / AI_SCALE);
        ai4Str << std::fixed << std::setprecision(3) << (ai4Raw / AI_SCALE);

        std::cout << "[DATA] eui=" << eui
                  << " DI1="  << static_cast<unsigned>(di1Val)
                  << " DI2="  << static_cast<unsigned>(di2Val)
                  << " AI1="  << (ai1Off ? "null" : ai1Str.str())
                  << " AI2="  << (ai2Off ? "null" : ai2Str.str())
                  << " AI3="  << (ai3Off ? "null" : ai3Str.str())
                  << " AI4="  << (ai4Off ? "null" : ai4Str.str())
                  << std::endl;

        const std::string base = std::string(PUB_PREFIX) + "/AIRTD402/" + eui + "/";

        // DI: publicar el nibble (0-15); 0xF = sin señal
	auto diToStr = [](uint8_t v)->std::string{
		if(v==0) return "0";
		if(v==1) return "1";
		return "null";
	};
        //publishValue(base + "DI1", std::to_string(static_cast<unsigned>(di1Val)));
        //publishValue(base + "DI2", std::to_string(static_cast<unsigned>(di2Val)));
	publishValue(base + "DI1", diToStr(di1Val));
	publishValue(base + "DI2", diToStr(di2Val));

        // AI: publicar valor en mA con 3 decimales; cadena vacía si canal OFF
        publishValue(base + "AI1", ai1Off ? "null" : ai1Str.str());
        publishValue(base + "AI2", ai2Off ? "null" : ai2Str.str());
        publishValue(base + "AI3", ai3Off ? "null" : ai3Str.str());
        publishValue(base + "AI4", ai4Off ? "null" : ai4Str.str());

    } catch (const std::exception &ex) {
        std::cerr << "[ERROR] decodeAidi421a() excepción: "
                  << ex.what() << std::endl;
    }
}

// ============================================================
//  publishValue — QoS 1, sin retain
// ============================================================
void MqttLoraGateway::publishValue(const std::string &topic,
                                    const std::string &payload)
{
    int mid = 0;
    int rc  = publish(&mid,
                      topic.c_str(),
                      static_cast<int>(payload.size()),
                      payload.c_str(),
                      1,      // QoS 1
                      false); // no retain

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[ERROR] publish() '" << topic
                  << "': " << mosqpp::strerror(rc) << std::endl;
    } else {
        std::cout << "[PUB] " << topic << " = " << payload << std::endl;
    }
}
