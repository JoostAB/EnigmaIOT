/**
 * @file GwOutput_serial.h
 * @version 0.0.1
 * @date 2023-01-09
 * @author Joost Bloemsma (https://github.com/JoostAB)
 * @brief Serial Gateway output module (based on Dummy Gateway by German Martin)
 */

#ifndef _GWOUT_serial_h
#define _GWOUT_serial_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#define MSG_START "###"

#include <EnigmaIOTGateway.h>
#include <GwOutput_generic.h>

class GatewayOutput_serial : public GatewayOutput_generic {
protected:
	//EnigmaIOTGatewayClass* enigmaIotGateway; ///< @brief Pointer to EnigmaIOT gateway instance
	//onDlData_t downlinkCb; ///< @brief downlink processing function handle

	/**
	  * @brief Saves output module configuration
	  * @return Returns `true` if save was successful. `false` otherwise
	  */
	bool saveConfig ();

	bool handleComInput();

	String comBuff;
	HardwareSerial* com;

public:
	//String netName; ///< @brief EnigmaIOT network name
	//String clientId; ///< @brief clientId
	//String gwTopic; ///< @brief topic for gateway

	//virtual int send () = 0;
	//virtual void onReveive () = 0;

	void initCom(HardwareSerial* c);

	/**
	  * @brief Called when wifi manager starts config portal
	  * @param enigmaIotGw Pointer to EnigmaIOT gateway instance
	  */
	void configManagerStart (EnigmaIOTGatewayClass* enigmaIotGw);

	/**
	  * @brief Called when wifi manager exits config portal
	  * @param status `true` if configuration was successful
	  */
	void configManagerExit (bool status);

	/**
	  * @brief Starts output module
	  * @return Returns `true` if successful. `false` otherwise
	  */
	bool begin ();

	/**
	  * @brief Loads output module configuration
	  * @return Returns `true` if load was successful. `false` otherwise
	  */
	bool loadConfig ();

	 /**
	  * @brief Send control data from nodes
	  * @param address Node Address
	  * @param data Message data buffer
	  * @param length Data buffer length
	  * @return Returns `true` if sending was successful. `false` otherwise
	  */
	bool outputControlSend (char* address, uint8_t* data, size_t length);

	 /**
	  * @brief Send new node notification
	  * @param address Node Address
	  * @param node_id Node Id
	  * @return Returns `true` if sending was successful. `false` otherwise
	  */
	bool newNodeSend (char* address, uint16_t node_id);

	 /**
	  * @brief Send node disconnection notification
	  * @param address Node Address
	  * @param reason Disconnection reason code
	  * @return Returns `true` if sending was successful. `false` otherwise
	  */
	bool nodeDisconnectedSend (char* address, gwInvalidateReason_t reason);

	 /**
	  * @brief Send data from nodes
	  * @param address Node Address
	  * @param data Message data buffer
	  * @param length Data buffer length
	  * @param type Type of message
	  * @return Returns `true` if sending was successful. `false` otherwise
	  */
	bool outputDataSend (char* address, char* data, size_t length, GwOutput_data_type_t type = data);

	 /**
	  * @brief Should be called often for module management
	  */
	void loop ();

	 /**
	  * @brief Set data processing function
	  * @param cb Function handle
	  */
	void setDlCallback (onDlData_t cb) {
		downlinkCb = cb;
	}
};

extern GatewayOutput_serial GwOutput;

#endif // _GWOUT_serial_h