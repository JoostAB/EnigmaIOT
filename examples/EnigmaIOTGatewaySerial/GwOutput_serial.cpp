/**
 * @file GwOutput_serial.cpp
 * @version 0.0.1
 * @date 2023-01-09
 * @author Joost Bloemsma (https://github.com/JoostAB)
 * @brief Serial Gateway output module (based on Dummy Gateway by German Martin)
 */

#include <Arduino.h>
#include "GwOutput_serial.h"
#include <ESPAsyncWebServer.h>
#include <helperFunctions.h>
#include <EnigmaIOTdebug.h>

#ifdef ESP32
#include <SPIFFS.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_tls.h"
#elif defined(ESP8266)
#include <Hash.h>
#endif // ESP32

#include <FS.h>


GatewayOutput_serial GwOutput;

void GatewayOutput_serial::configManagerStart (EnigmaIOTGatewayClass* enigmaIotGw) {

}

bool GatewayOutput_serial::saveConfig () {
    return true;
}

bool GatewayOutput_serial::loadConfig () {
    return true;
}

void GatewayOutput_serial::configManagerExit (bool status) {

}

bool GatewayOutput_serial::begin () {
    DEBUG_INFO ("Begin");
    comBuff = "";
    return true;
}


void GatewayOutput_serial::loop () {
    if (com->available()) {
		char c = com->read();
		if (c == 10 || c == 13) {
			// Serial input complete
			DEBUG_VERBOSE("Serial input received: %s", comBuff);
			handleComInput();
            comBuff = "";
		} else {
			// Add input char to buff
			comBuff += c;
		}
	}
}

void GatewayOutput_serial::initCom(HardwareSerial* c) {
    com = c;
}

bool GatewayOutput_serial::handleComInput() {
    // Check message header
    if (!comBuff.startsWith(MSG_START)) {
        DEBUG_ERROR("Invalid message header. Ignoring");
        return false;
    }

    // Get character after MSG_START
    char act = comBuff.charAt(3);

    return true;
}

bool GatewayOutput_serial::outputDataSend (char* address, char* data, size_t length, GwOutput_data_type_t type) {
    DEBUG_WARN ("Output data send. Address %s. Data %.*s", address, length, data);
    return true;
}

bool GatewayOutput_serial::outputControlSend (char* address, uint8_t* data, size_t length) {
    DEBUG_INFO ("Output control send. Address %s. Data %s", address, printHexBuffer (data, length));
    return true;
}

bool GatewayOutput_serial::newNodeSend (char* address, uint16_t node_id) {
    DEBUG_WARN ("New node: %s NodeID: %d", address, node_id);
    return true;
}

bool GatewayOutput_serial::nodeDisconnectedSend (char* address, gwInvalidateReason_t reason) {
    DEBUG_WARN ("Node %s disconnected. Reason %d", address, reason);
    return true;
}