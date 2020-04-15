/**
  * @file EnigmaIOTGateway.h
  * @version 0.8.2
  * @date 03/03/2020
  * @author German Martin
  * @brief Library to build a gateway for EnigmaIoT system
  */

#ifndef _ENIGMAIOTGATEWAY_h
#define _ENIGMAIOTGATEWAY_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif
#include "EnigmaIoTconfig.h"
#include "NodeList.h"
#include "Filter.h"
#include "Comms_hal.h"
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <DNSServer.h>
#include <queue>


/**
  * @brief Message code definition
  */
enum gatewayMessageType_t {
    SENSOR_DATA = 0x01, /**< Data message from sensor node */
    UNENCRYPTED_NODE_DATA = 0x11, /**< Data message from sensor node. Unencrypted */
    DOWNSTREAM_DATA_SET = 0x02, /**< Data message from gateway. Downstream data for user commands */
    DOWNSTREAM_DATA_GET = 0x12, /**< Data message from gateway. Downstream data for user commands */
	CONTROL_DATA = 0x03, /**< Internal control message from sensor to gateway. Used for OTA, settings configuration, etc */
	DOWNSTREAM_CTRL_DATA = 0x04, /**< Internal control message from gateway to sensor. Used for OTA, settings configuration, etc */
    CLOCK_REQUEST = 0x05, /**< Clock request message from node */
    CLOCK_RESPONSE = 0x06, /**< Clock response message from gateway */
    CLIENT_HELLO = 0xFF, /**< ClientHello message from sensor node */
    SERVER_HELLO = 0xFE, /**< ServerHello message from gateway */
    //KEY_EXCHANGE_FINISHED = 0xFD, /**< KeyExchangeFinished message from sensor node */
    //CYPHER_FINISHED = 0xFC, /**< CypherFinished message from gateway */
    INVALIDATE_KEY = 0xFB /**< InvalidateKey message from gateway */
};

enum gatewayPayload_type_t {
    RAW = 0x00,
    CAYENNE = 0x81,
    PROT_BUF = 0x82,
    MSG_PACK = 0x83,
    BSON = 0x84,
    ENIGMAIOT = 0xFF
};

/**
  * @brief Key invalidation reason definition
  */
enum gwInvalidateReason_t {
    UNKNOWN_ERROR = 0x00, /**< Unknown error. Not used by the moment */
    WRONG_CLIENT_HELLO = 0x01, /**< ClientHello message received was invalid */
    //WRONG_EXCHANGE_FINISHED = 0x02, /**< KeyExchangeFinished message received was invalid. Probably this means an error on shared key */
    WRONG_DATA = 0x03, /**< Data message received could not be decrypted successfuly */
    UNREGISTERED_NODE = 0x04, /**< Data received from an unregistered node*/
    KEY_EXPIRED = 0x05 /**< Node key has reached maximum validity time */
};

#if defined ARDUINO_ARCH_ESP8266 || defined ARDUINO_ARCH_ESP32
#include <functional>
typedef std::function<void (uint8_t* mac, uint8_t* buf, uint8_t len, uint16_t lostMessages, bool control, gatewayPayload_type_t payload_type)> onGwDataRx_t;
typedef std::function<void (uint8_t* mac, uint16_t node_id)> onNewNode_t;
typedef std::function<void (uint8_t* mac, gwInvalidateReason_t reason)> onNodeDisconnected_t;
typedef std::function<void (boolean status)> onWiFiManagerExit_t;
typedef std::function<void (void)> onWiFiManagerStarted_t;
#else
typedef void (*onGwDataRx_t)(uint8_t* mac, uint8_t* data, uint8_t len, uint16_t lostMessages, bool control, gatewayPayload_type_t payload_type);
typedef void (*onNewNode_t)(uint8_t* mac, uint16_t node_id);
typedef void (*onNodeDisconnected_t)(uint8_t* mac, gwInvalidateReason_t reason);
typedef void (*onWiFiManagerExit_t)(boolean status);
typedef void (*onWiFiManagerStarted_t)(void);
#endif

typedef struct {
	uint8_t channel = DEFAULT_CHANNEL; /**< Channel used for communications*/
	uint8_t networkKey[KEY_LENGTH];   /**< Network key to protect key agreement*/
	char networkName[NETWORK_NAME_LENGTH];   /**< Network name, used to help nodes to find gateway*/
} gateway_config_t;

typedef struct {
    uint8_t *addr; /**< Message address*/
    uint8_t *data; /**< Message buffer*/
    size_t len; /**< Message length*/
} msg_queue_item_t;

template <typename Telement>
class EnigmaIOTRingBuffer {
protected:
    int maxSize;
    int size = 0;
    int readIndex = 0;
    int writeIndex = 0;
    Telement* buffer;

public:
    EnigmaIOTRingBuffer <Telement>(int range) : maxSize (range) {
        buffer = new Telement[maxSize];
    }
    int getSize () { return size; }
    bool isFull () { return size == maxSize; }
    bool isEmpty () { return size == 0; }
    bool push (Telement *item) {
        bool wasFull = isFull ();
        memcpy (buffer[writeIndex], item, sizeof (Telement));
        writeIndex++;
        if (writeIndex >= maxSize) {
            writeIndex %= maxSize;
        }
        if (wasFull) { // old value is no longer valid
            readIndex++;
            if (readIndex >= maxSize) {
                readIndex %= maxSize;
            }
        } else {
            size++;
        }
        return !wasFull;
    }
    bool pop () {
        bool wasEmpty = isEmpty ();
        if (!wasEmpty) {
            readIndex++;
            if (readIndex >= maxSize) {
                readIndex %= maxSize;
            }
            size--;
        }
        return !wasEmpty;
    }
    Telement* front () {
        if (!isEmpty ()) {
            return buffer[readIndex];
        } else {
            return NULL;
        }
    }
};

/**
  * @brief Main gateway class. Manages communication with nodes and sends data to upper layer
  *
  */
class EnigmaIOTGatewayClass
{
 protected:
     uint8_t myPublicKey[KEY_LENGTH]; ///< @brief Temporary public key store used during key agreement
     bool flashTx = false; ///< @brief `true` if Tx LED should flash
     volatile bool flashRx = false; ///< @brief `true` if Rx LED should flash
     node_t node; ///< @brief temporary store to keep node data while processing a message
     NodeList nodelist; ///< @brief Node database that keeps status and shared keys
     Comms_halClass *comm; ///< @brief Instance of physical communication layer
     int8_t txled = -1; ///< @brief I/O pin to connect a led that flashes when gateway transmits data
     int8_t rxled = -1; ///< @brief I/O pin to connect a led that flashes when gateway receives data
     unsigned long txLedOnTime; ///< @brief Flash duration for Tx LED
     unsigned long rxLedOnTime; ///< @brief Flash duration for Rx LED
     onGwDataRx_t notifyData; ///< @brief Callback function that will be invoked when data is received fron a node
     onNewNode_t notifyNewNode; ///< @brief Callback function that will be invoked when a new node is connected
     onNodeDisconnected_t notifyNodeDisconnection; ///< @brief Callback function that will be invoked when a node gets disconnected
     bool useCounter = true; ///< @brief `true` if counter is used to check data messages order
	 gateway_config_t gwConfig; ///< @brief Gateway specific configuration to be stored on flash memory
     char networkKey[KEY_LENGTH]; ///< @brief Temporary store for textual network key

     std::queue<msg_queue_item_t*> input_queue; ///< @brief Input messages buffer. It acts as a FIFO queue

	 AsyncWebServer* server; ///< @brief WebServer that holds configuration portal
	 DNSServer* dns; ///< @brief DNS server used by configuration portal
	 AsyncWiFiManager* wifiManager; ///< @brief Wifi configuration portal
	 onWiFiManagerExit_t notifyWiFiManagerExit; ///< @brief Function called when configuration portal exits
	 onWiFiManagerStarted_t notifyWiFiManagerStarted; ///< @brief Function called when configuration portal is started

	 /**
	  * @brief Activates a flag that signals that configuration has to be saved
	  */
	 static void doSave (void);

     /**
      * @brief Build a **ServerHello** messange and send it to node
      * @param key Node public key to be used on Diffie Hellman algorithm
      * @param node Entry in node list database where node will be registered
      * @return Returns `true` if ServerHello message was successfully sent. `false` otherwise
      */
     bool serverHello (const uint8_t *key, Node *node);

     /**
      * @brief Gets a buffer containing a **ClientHello** message and process it. This carries node public key to be used on Diffie Hellman algorithm
      * @param mac Address where this message was received from
      * @param buf Pointer to the buffer that contains the message
      * @param count Message length in number of bytes of ClientHello message
      * @param node Node entry that Client Hello message comes from
      * @return Returns `true` if message could be correcly processed
      */
     bool processClientHello (const uint8_t mac[6], const uint8_t* buf, size_t count, Node *node);

	 /**
	  * @brief Starts clock sync procedure from node to gateway
	  * @param mac Address where this message was received from
	  * @param buf Pointer to the buffer that contains the message
	  * @param count Message length in number of bytes of ClockRequest message
	  * @param node Node entry that Client Hello message comes from
	  * @return Returns `true` if message could be correcly processed
	  */
     bool processClockRequest (const uint8_t mac[6], const uint8_t* buf, size_t count, Node* node);
	  
	 /**
	  * @brief Returns timestaps needed so that node can calculate time difference
	  * @param node Pointer to data that corresponds to originating node
	  * @return Returns `true` if message could be correcly processed
	  */
     bool clockResponse (Node* node);

     /**
      * @brief Creates an **InvalidateKey** message and sned it. This trigger a new key agreement to start on related node
      * @param node Node to send Invalidate Key message to
      * @param reason Reason that produced key invalidation in gwInvalidateReason_t format
      * @return Returns `true` if message could be correcly sent
      */
     bool invalidateKey (Node *node, gwInvalidateReason_t reason);

     /**
      * @brief Processes data message from node
      * @param mac Node address
      * @param buf Buffer that stores received message
      * @param count Length of received data
      * @param node Node where data message comes from
      * @param encrypted `true` if received message is encrypted
      * @return Returns `true` if message could be correcly decoded
      */
     bool processDataMessage (const uint8_t mac[6], uint8_t* buf, size_t count, Node *node, bool encrypted = true);

     /**
      * @brief Processes unencrypted data message from node
      * @param mac Node address
      * @param buf Buffer that stores received message
      * @param count Length of received data
      * @param node Node where data message comes from
      * @return Returns `true` if message could be correcly decoded
      */
     bool processUnencryptedDataMessage (const uint8_t mac[6], uint8_t* buf, size_t count, Node* node);
     
     /**
      * @brief Builds, encrypts and sends a **DownstreamData** message.
      * @param node Node that downstream data message is going to
      * @param data Buffer to store payload to be sent
      * @param len Length of payload data
	  * @param controlData Content data type if control data
      * @return Returns `true` if message could be correcly sent or scheduled
      */
	 bool downstreamDataMessage (Node* node, const uint8_t* data, size_t len, control_message_type_t controlData);

	 /**
	 * @brief Processes control message from node
	 * @param mac Node address
	 * @param buf Buffer that stores received message
	 * @param count Length of received data
	 * @param node Node where data message comes from
	 * @return Returns `true` if message could be correcly decoded
	 */
	 bool processControlMessage (const uint8_t mac[6], uint8_t* buf, size_t count, Node* node);

     /**
      * @brief Process every received message.
      *
      * It starts clasiffying message usint the first byte. After that it passes it to the corresponding method for decoding
      * @param mac Address of message sender
      * @param buf Buffer that stores message bytes
      * @param count Length of message in number of bytes
      */
     void manageMessage (const uint8_t* mac, uint8_t* buf, uint8_t count);

     /**
      * @brief Function that will be called anytime this gateway receives a message
      * @param mac_addr Address of message sender
      * @param data Buffer that stores message bytes
      * @param len Length of message in number of bytes
      */
     static void rx_cb (uint8_t *mac_addr, uint8_t *data, uint8_t len);

     /**
      * @brief Function that will be called anytime this gateway sends a message
      * to indicate status result of sending process
      * @param mac_addr Address of message destination
      * @param status Result of sending process
      */
     static void tx_cb (uint8_t *mac_addr, uint8_t status);

     /**
      * @brief Functrion to debug send status.
      * @param mac_addr Address of message sender
      * @param status Result status code
      */
     void getStatus (uint8_t *mac_addr, uint8_t status);

	 /**
	 * @brief Loads configuration from flash memory
	 * @return Returns `true` if data was read successfuly. `false` otherwise
	 */
	 bool loadFlashData ();

	/**
	 * @brief Saves configuration to flash memory
	 * @return Returns `true` if data could be written successfuly. `false` otherwise
	 */
	 bool saveFlashData ();

 public:
	/**
	 * @brief Gets flag that indicates if configuration should be saved
	 * @return Returns `true` if config data should be saved. `false` otherwise
	 */
	 bool getShouldSave ();

	/**
	 * @brief Gets EnigmaIOT network name
	 * @return Returns EnigmaIOT network name
	 */
	 char* getNetworkName () {
		 return gwConfig.networkName;
	 }

	/**
	 * @brief Gets EnigmaIOT network key
	 * @return Returns EnigmaIOT network key
	 */
	 char* getNetworkKey () {
		 return (char*)(gwConfig.networkKey);
	 }

	/**
	 * @brief Adds a parameter to configuration portal
	 * @param p Configuration parameter
	 */
	 void addWiFiManagerParameter (AsyncWiFiManagerParameter* p) {
		 if (wifiManager) {
			 wifiManager->addParameter (p);
		 }
	 }

	/**
	 * @brief Register callback to be called on wifi manager exit
	 * @param handle Callback function pointer
	 */
	 void onWiFiManagerExit (onWiFiManagerExit_t handle) {
		 notifyWiFiManagerExit = handle;
	 }

	/**
	 * @brief Register callback to be called on wifi manager start
	 * @param handle Callback function pointer
	 */
	 void onWiFiManagerStarted (onWiFiManagerStarted_t handle) {
		 notifyWiFiManagerStarted = handle;
	 }

	 /**
	 * @brief Starts configuration AP and web server and gets settings from it
	 * @return Returns `true` if data was been correctly configured. `false` otherwise
	 */
	 bool configWiFiManager ();

     /**
      * @brief Initalizes communication basic data and starts accepting node registration
      * @param comm Physical layer to be used on this network
      * @param networkKey Network key to protect shared key agreement
      * @param useDataCounter Indicates if a counter is going to be added to every message data to check message sequence. `true` by default
      */
     void begin (Comms_halClass *comm, uint8_t *networkKey = NULL, bool useDataCounter = true);

     /**
      * @brief This method should be called periodically for instance inside `loop()` function.
      * It is used for internal gateway maintenance tasks
      */
     void handle ();

     /**
      * @brief Sets a LED to be flashed every time a message is transmitted
      * @param led LED I/O pin
      * @param onTime Flash duration. 100ms by default.
      */
     void setTxLed (uint8_t led, time_t onTime = FLASH_LED_TIME);

     /**
      * @brief Sets a LED to be flashed every time a message is received
      * @param led LED I/O pin
      * @param onTime Flash duration. 100ms by default.
      */
     void setRxLed (uint8_t led, time_t onTime = FLASH_LED_TIME);

     /**
      * @brief Defines a function callback that will be called on every downlink data message that is received from a node
      *
      * Use example:
      * ``` C++
      * // First define the callback function
      * void processRxData (const uint8_t* mac, const uint8_t* buffer, uint8_t length, uint16_t lostMessages) {
      *   // Do whatever you need with received data
      * }
      *
      * void setup () {
      *   .....
      *   // Now register function as data message handler
      *   EnigmaIOTNode.onDataRx (processRxData);
      *   .....
      * }
      *
      * void loop {
      *   .....
      *   EnigmaIOTNode.handle();
      *   .....
      * }
      * ```
      * @param handler Pointer to the function
      */
     void onDataRx (onGwDataRx_t handler) {
         notifyData = handler;
     }

     /**
      * @brief Gets packet error rate of node that has a specific address
      * @param address Node address
      * @return Packet error rate
      */
     double getPER (uint8_t *address);

     /**
      * @brief Gets total packets sent by node that has a specific address
      * @param address Node address
      * @return Node total sent packets
      */
     uint32_t getTotalPackets (uint8_t *address);

     /**
      * @brief Gets number of errored packets of node that has a specific address
      * @param address Node address
      * @return Node errored packets
      */
     uint32_t getErrorPackets (uint8_t *address);

     /**
      * @brief Gets packet rate sent by node that has a specific address, in packets per hour
      * @param address Node address
      * @return Node packet rate
      */
     double getPacketsHour (uint8_t *address);

     /**
      * @brief Starts a downstream data message transmission
      * @param mac Node address
      * @param data Payload buffer
      * @param len Payload length
      * @param controlData Indicates if data is control data and its class
      * @return Returns true if everything went ok
      */
     bool sendDownstream (uint8_t* mac, const uint8_t *data, size_t len, control_message_type_t controlData);

     /**
      * @brief Defines a function callback that will be called every time a node gets connected or reconnected
      *
      * Use example:
      * ``` C++
      * // First define the callback function
      * void newNodeConnected (uint8_t* mac) {
      *   // Do whatever you need new node address
      * }
      *
      * void setup () {
      *   .....
      *   // Now register function as new node condition handler
      *   EnigmaIOTGateway.onNewNode (newNodeConnected);
      *   .....
      * }
      *
      * void loop {
      *   .....
      *   EnigmaIOTNode.handle();
      *   .....
      * }
      * ```
      * @param handler Pointer to the function
      */
     void onNewNode (onNewNode_t handler) {
         notifyNewNode = handler;
     }

     /**
      * @brief Defines a function callback that will be called every time a node is disconnected
      *
      * Use example:
      * ``` C++
      * // First define the callback function
      * void nodeDisconnected (uint8_t* mac, gwInvalidateReason_t reason) {
      *   // Do whatever you need node address and disconnection reason
      * }
      *
      * void setup () {
      *   .....
      *   // Now register function as new node condition handler
      *   EnigmaIOTGateway.onNodeDisconnected (nodeDisconnected);
      *   .....
      * }
      *
      * void loop {
      *   .....
      *   EnigmaIOTNode.handle();
      *   .....
      * }
      * ```
      * @param handler Pointer to the function
      */
     void onNodeDisconnected (onNodeDisconnected_t handler) {
         notifyNodeDisconnection = handler;
     }

    /**
      * @brief Add message to input queue
      * @param addr Origin address
      * @param msg EnigmaIoT message
      * @param len Message length
      */
     bool addInputMsgQueue (const uint8_t* addr, const uint8_t* msg, size_t len);

      /**
      * @brief Gets next item in the queue
      * @return Next message to be processed
      */
     msg_queue_item_t* getInputMsgQueue ();

    /**
      * @brief Deletes next item in the queue
      */
     void popInputMsgQueue ();

};

extern EnigmaIOTGatewayClass EnigmaIOTGateway;

#endif

