#define ESP_DRD_USE_SPIFFS true

// Include Libraries

// WiFi Library
#include <WiFi.h>
// File System Library
#include <FS.h>
// SPI Flash Syetem Library
#include <SPIFFS.h>
// WiFiManager Library
#include <WiFiManager.h>
// Arduino JSON library
#include <ArduinoJson.h>

#include <Arduino_MQTT_Client.h>
#include <Server_Side_RPC.h>
#include <Attribute_Request.h>
#include <Shared_Attribute_Update.h>
#include <ThingsBoard.h>

// #include "DHT.h"

// #define DHTPIN 2
// #define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

// JSON configuration file
#define JSON_CONFIG_FILE "/node_config.json"

#ifndef LED_BUILTIN
#define LED_BUILTIN 99
#endif

#define CONF_DEMAND_PIN 12
//#define CONF_RESET_PIN 14

#define OPMODE_LED 99
#define WIFI_STATUS_LED 99
#define TELEMETRY_UPLOAD_LED 99

// Change to true when testing to force configuration every time we run
bool forceConfig = false;
// Flag for saving data
bool shouldSaveConfig = false;

const char *ap_ssid = "DSALARM";
const char *ap_password = "password123";

// MQTT Variables
char tokenString[31] = "YOUR_DEVICE_ACCESS_TOKEN";
char serverString[21] = "demo.thingsboard.io";
char descriptionString[25] = "DESCRIBE DEVICE";
int portNumber = 1883;
int telemetrySendInterval = 1;
unsigned long prevHoldMillis = 0;
uint8_t holdCounter = 0;

constexpr uint32_t MAX_MESSAGE_SIZE = 256U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

// Maximum amount of attributs we can request or subscribe, has to be set both in the ThingsBoard template list and Attribute_Request_Callback template list
// and should be the same as the amount of variables in the passed array. If it is less not all variables will be requested or subscribed
constexpr size_t MAX_ATTRIBUTES = 3U;

constexpr uint64_t REQUEST_TIMEOUT_MICROSECONDS = 5000U * 1000U;

// Attribute names for attribute request and attribute updates functionality

constexpr const char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr const char LED_MODE_ATTR[] = "ledMode";
constexpr const char LED_STATE_ATTR[] = "ledState";

// Initialize underlying client, used to establish a connection
WiFiClient wifiClient;

// Initalize the Mqtt client instance
Arduino_MQTT_Client mqttClient(wifiClient);

// Initialize used apis
Server_Side_RPC<3U, 5U> rpc;
Attribute_Request<2U, MAX_ATTRIBUTES> attr_request;
Shared_Attribute_Update<3U, MAX_ATTRIBUTES> shared_update;

const std::array<IAPI_Implementation*, 3U> apis = {
    &rpc,
    &attr_request,
    &shared_update
};

// Initialize ThingsBoard instance with the maximum needed buffer size, stack size and the apis we want to use
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE, Default_Max_Stack_Size, apis);

// handle led state and mode changes
volatile bool attributesChanged = false;

// LED modes: 0 - continious state, 1 - blinking
volatile int ledMode = 0;

// Current led state
volatile bool ledState = false;

// Settings for interval in blinking mode
constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange;

// For telemetry
uint32_t previousDataSend;

// List of shared attributes for subscribing to their updates
constexpr std::array<const char *, 2U> SHARED_ATTRIBUTES_LIST = {
  LED_STATE_ATTR,
  BLINKING_INTERVAL_ATTR
};

// List of client attributes for requesting them (Using to initialize device states)
constexpr std::array<const char *, 1U> CLIENT_ATTRIBUTES_LIST = {
  LED_MODE_ATTR
};

// WiFiManager Global variable
WiFiManager wm;

// Device Name TextBox
WiFiManagerParameter *description_textbox;

// Token TextBox
WiFiManagerParameter *token_textbox;
// Server TextBox
WiFiManagerParameter *server_textbox;
// Port TextBox (Number) - 6 + 1 spare characters maximum
WiFiManagerParameter *port_textbox_num;

// send Interval millisecond (max 30000)
WiFiManagerParameter *interval_textbox_num;

void saveConfigFile() // Save Config in JSON format
{
  Serial.println(F("Saving configuration..."));
  
  // Create a JSON document
  StaticJsonDocument<512> json;
  
  json["token"] = tokenString;
  json["server"] = serverString;
  json["port"] = portNumber;
  json["interval"] = telemetrySendInterval;
  json["decription"] = descriptionString;

  // Open config file
  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w");
  if (!configFile)
  {
    // Error, file did not open
    Serial.println("failed to open config file for writing");
  }

  // Serialize JSON data to write to file
  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0)
  {
    // Error writing file
    Serial.println(F("Failed to write to file"));
  }
  // Close file
  configFile.close();
}

bool loadConfigFile() // Load existing configuration file
{
  // Uncomment if we need to format filesystem
  // SPIFFS.format();

  // Read configuration from FS json
  Serial.println("Mounting File System...");

  // May need to make it begin(true) first time you are using SPIFFS
  if (SPIFFS.begin(false))
  {
    Serial.println("mounted file system");
    if (SPIFFS.exists(JSON_CONFIG_FILE))
    {
      // The file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
      if (configFile)
      {
        Serial.println("Opened configuration file");
        StaticJsonDocument<512> json;
        DeserializationError error = deserializeJson(json, configFile);
        serializeJsonPretty(json, Serial);
        if (!error)
        {
          Serial.println("Parsing JSON");

          strcpy(tokenString, json["token"]);
          strcpy(serverString, json["server"]);
          portNumber = json["port"].as<int>();
          telemetrySendInterval = json["interval"].as<int>();
          strcpy(descriptionString, json["decription"]);
          return true;
        }
        else
        {
          // Error loading JSON data
          Serial.println("Failed to load json config");
        }
      }
    }
  }
  else
  {
    // Error mounting file system
    Serial.println("Failed to mount FS");
  }

  return false;
}


void saveConfigCallback() // Callback notifying us of the need to save configuration
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *myWiFiManager) // Called when config mode launched
{
  Serial.println("Entered Configuration Mode");

  Serial.print("Config SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());

  Serial.print("Config IP Address: ");
  Serial.println(WiFi.softAPIP());
}

/// @brief Processes function for RPC call "setLedMode"
/// RPC_Data is a JSON variant, that can be queried using operator[]
/// See https://arduinojson.org/v5/api/jsonvariant/subscript/ for more details
/// @param data Data containing the rpc data that was called and its current value
void processSetLedMode(const JsonVariantConst &data, JsonDocument &response) {
  Serial.println("Received the set led state RPC method");

  // Process data
  int new_mode = data;

  Serial.print("Mode to change: ");
  Serial.println(new_mode);
  StaticJsonDocument<1> response_doc;

  if (new_mode != 0 && new_mode != 1) {
    response_doc["error"] = "Unknown mode!";
    response.set(response_doc);
    return;
  }

  ledMode = new_mode;

  attributesChanged = true;

  // Returning current mode
  response_doc["newMode"] = (int)ledMode;
  response.set(response_doc);
}


// Optional, keep subscribed shared attributes empty instead,
// and the callback will be called for every shared attribute changed on the device,
// instead of only the one that were entered instead
const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "setLedMode", processSetLedMode }
};

/// @brief Update callback that will be called as soon as one of the provided shared attributes changes value,
/// if none are provided we subscribe to any shared attribute change instead
/// @param data Data containing the shared attributes that were changed and their current value
void processSharedAttributes(const JsonObjectConst &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), BLINKING_INTERVAL_ATTR) == 0) {
      const uint16_t new_interval = it->value().as<uint16_t>();
      if (new_interval >= BLINKING_INTERVAL_MS_MIN && new_interval <= BLINKING_INTERVAL_MS_MAX) {
        blinkingInterval = new_interval;
        Serial.print("Blinking interval is set to: ");
        Serial.println(new_interval);
      }
    } else if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0) {
      ledState = it->value().as<bool>();
      if (LED_BUILTIN != 99) {
        digitalWrite(LED_BUILTIN, ledState);
      }
      Serial.print("LED state is set to: ");
      Serial.println(ledState);
    }
  }
  attributesChanged = true;
}

void processClientAttributes(const JsonObjectConst &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), LED_MODE_ATTR) == 0) {
      const uint16_t new_mode = it->value().as<uint16_t>();
      ledMode = new_mode;
    }
  }
}

// Attribute request did not receive a response in the expected amount of microseconds 
void requestTimedOut() {
  Serial.printf("Attribute request timed out did not receive a response in (%llu) microseconds. Ensure client is connected to the MQTT broker and that the keys actually exist on the target device\n", REQUEST_TIMEOUT_MICROSECONDS);
}

const Shared_Attribute_Callback<MAX_ATTRIBUTES> attributes_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
const Attribute_Request_Callback<MAX_ATTRIBUTES> attribute_shared_request_callback(&processSharedAttributes, REQUEST_TIMEOUT_MICROSECONDS, &requestTimedOut, SHARED_ATTRIBUTES_LIST);
const Attribute_Request_Callback<MAX_ATTRIBUTES> attribute_client_request_callback(&processClientAttributes, REQUEST_TIMEOUT_MICROSECONDS, &requestTimedOut, CLIENT_ATTRIBUTES_LIST);

void setup()
{
  bool spiffsSetup = loadConfigFile();
  if (!spiffsSetup)
  {
    Serial.println(F("Forcing config mode as there is no saved config"));
    forceConfig = true;
  }

  // Explicitly set WiFi mode
  WiFi.mode(WIFI_STA);

  // Setup Serial monitor
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(CONF_DEMAND_PIN, INPUT);
  // pinMode(CONF_RESET_PIN, INPUT);
  delay(10);
  digitalWrite(LED_BUILTIN, LOW);

  // Reset settings (only for development)
  // wm.resetSettings();

  wm.setShowStaticFields(true);
  wm.setShowDnsFields(true);
      
  // Need to convert numerical input to string to display the default value.
  char numericBuffer[8];
  sprintf(numericBuffer, "%d", portNumber); 
  // Server TextBox
  server_textbox = new WiFiManagerParameter("server", "Thingsboard Server (max 20 characters)", serverString, 20);
  // Port TextBox (Number) - 6 + 1 spare characters maximum
  port_textbox_num = new WiFiManagerParameter("port_num", "Thingsboard Server Port Number", numericBuffer, 7);
  // Token TextBox
  token_textbox = new WiFiManagerParameter("token", "Thingsboard Token (max 30 characters)", tokenString, 30);

  char intervalBuffer[7];
  sprintf(intervalBuffer, "%d", telemetrySendInterval); 
  // Send Interval TextBox (Number) - 5 + 1 spare characters maximum
  interval_textbox_num = new WiFiManagerParameter("interval_num", "Telemetry send Interval (second)", intervalBuffer, 6);

  // Device description TextBox
  description_textbox = new WiFiManagerParameter("description", "Device Description (max 24 characters)", descriptionString, 24);
    
  wm.addParameter(token_textbox);
  wm.addParameter(server_textbox);
  wm.addParameter(port_textbox_num);
  wm.addParameter(interval_textbox_num);
  wm.addParameter(description_textbox);

  // Set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);

  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback(configModeCallback);
  wm.setConnectTimeout(10);
  wm.setConfigPortalTimeout(120);
}

bool wifiConnected = false;
bool disableAutoConnect = false;

void loop() {
  delay(10);
  // // put your main code here, to run repeatedly:
  // int currentDemandState = digitalRead(CONF_DEMAND_PIN);
  // if(forceConfig || currentDemandState == HIGH) {
  //   wm.setEnableConfigPortal(true);
  //   if (!wm.startConfigPortal(ap_ssid, ap_password))
  //   {
  //     Serial.println("failed to connect and hit timeout");
  //     delay(3000);
  //     //reset and try again
  //     ESP.restart();
  //     delay(5000);
  //   }
  //   wifiConnected = true;
  //   forceConfig = false;
  // }

  // unsigned long currentMillis = millis();
  // if (WiFi.status() != WL_CONNECTED) {
  //   wm.setEnableConfigPortal(false);
  //   Serial.print(millis());
  //   Serial.println("Reconnecting to WiFi...");
  //   if (wm.autoConnect(ap_ssid, ap_password))
  //   {
  //     wifiConnected = true;
  //   } else {
  //     wifiConnected = false;
  //   }
  // }

  // // Save the custom parameters to FS
  // if (shouldSaveConfig) {
  //   // If we get here, we are connected to the WiFi
  //   Serial.println("");
  //   Serial.println("WiFi connected");
  //   Serial.print("IP address: ");
  //   Serial.println(WiFi.localIP());
  //   Serial.print("subnetMask: ");
  //   Serial.println(WiFi.subnetMask());
  //   Serial.print("gatewayIP: ");
  //   Serial.println(WiFi.gatewayIP());

  //   // Lets deal with the user config values
  //   // Copy the string value
  //   Serial.print("tokenString: ");
  //   Serial.println(tokenString);
  //   strncpy(tokenString, token_textbox->getValue(), sizeof(tokenString));

  //   strncpy(serverString, server_textbox->getValue(), sizeof(serverString));
  //   Serial.print("serverString: ");
  //   Serial.println(serverString);

  //   //Convert the number value
  //   portNumber = atoi(port_textbox_num->getValue());
  //   Serial.print("portNumber Str: ");
  //   Serial.println(port_textbox_num->getValue());
  //   Serial.print("portNumber: ");
  //   Serial.println(portNumber);

  //   int buffIntval = atoi(interval_textbox_num->getValue());
  //   telemetrySendInterval = buffIntval > 86400 ? 86400 : buffIntval;
  //   Serial.print("Interval Str: ");
  //   Serial.println(interval_textbox_num->getValue());
  //   Serial.print("Interval: ");
  //   Serial.println(telemetrySendInterval);

  //   strncpy(descriptionString, description_textbox->getValue(), sizeof(descriptionString));
  //   Serial.print("descriptionString: ");
  //   Serial.println(descriptionString);

  //   saveConfigFile();
  //   shouldSaveConfig = false;
  // }

  // int currentResetState = digitalRead(CONF_RESET_PIN);
  // if(currentResetState == HIGH) {
  //   Serial.println("Resetting configuration");
  //   wm.resetSettings();
  //   SPIFFS.remove(JSON_CONFIG_FILE);
  //   delay(3000);
  //   ESP.restart();
  //   delay(5000);
  // }

  // if(wifiConnected) {
  //   if (!tb.connected()) {
  //     // Connect to the ThingsBoard
  //     Serial.print("Connecting to: ");
  //     Serial.print((const char*)&serverString);
  //     Serial.print(" with token ");
  //     Serial.println((const char*)&tokenString);
  //     if (!tb.connect((const char*)&serverString, (const char*)&tokenString, portNumber)) {
  //       Serial.println("Failed to connect");
  //       return;
  //     }
  //     // Sending a MAC address as an attribute
  //     tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());

  //     Serial.println("Subscribing for RPC...");
  //     // Perform a subscription. All consequent data processing will happen in
  //     // processSetLedState() and processSetLedMode() functions,
  //     // as denoted by callbacks array.
  //     if (!rpc.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
  //       Serial.println("Failed to subscribe for RPC");
  //       return;
  //     }

  //     if (!shared_update.Shared_Attributes_Subscribe(attributes_callback)) {
  //       Serial.println("Failed to subscribe for shared attribute updates");
  //       return;
  //     }

  //     Serial.println("Subscribe done");

  //     // Request current states of shared attributes
  //     if (!attr_request.Shared_Attributes_Request(attribute_shared_request_callback)) {
  //       Serial.println("Failed to request for shared attributes");
  //       return;
  //     }

  //     // Request current states of client attributes
  //     if (!attr_request.Client_Attributes_Request(attribute_client_request_callback)) {
  //       Serial.println("Failed to request for client attributes");
  //       return;
  //     }
  //   }

  //   if (attributesChanged) {
  //     attributesChanged = false;
  //     if (ledMode == 0) {
  //       previousStateChange = millis();
  //     }
  //     tb.sendTelemetryData(LED_MODE_ATTR, ledMode);
  //     tb.sendTelemetryData(LED_STATE_ATTR, ledState);
  //     tb.sendAttributeData(LED_MODE_ATTR, ledMode);
  //     tb.sendAttributeData(LED_STATE_ATTR, ledState);
  //   }

  //   if (ledMode == 1 && millis() - previousStateChange > blinkingInterval) {
  //     previousStateChange = millis();
  //     ledState = !ledState;
  //     tb.sendTelemetryData(LED_STATE_ATTR, ledState);
  //     tb.sendAttributeData(LED_STATE_ATTR, ledState);
  //     if (LED_BUILTIN == 99) {
  //       Serial.print("LED state changed to: ");
  //       Serial.println(ledState);
  //     } else {
  //       digitalWrite(LED_BUILTIN, ledState);
  //     }
  //   }

  //   // Sending telemetry every telemetrySendInterval time
  //   if (millis() - previousDataSend > (telemetrySendInterval * 1000)) {
  //     previousDataSend = millis();
  //     tb.sendTelemetryData("temperature", random(10, 20));
  //     tb.sendTelemetryData("humidity", random(0, 100));
  //     tb.sendTelemetryData("heat_index", random(0, 100));
  //     tb.sendAttributeData("rssi", WiFi.RSSI());
  //     tb.sendAttributeData("channel", WiFi.channel());
  //     tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
  //     tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
  //     tb.sendAttributeData("ssid", WiFi.SSID().c_str());
  //   }
  // }

  // tb.loop();

  int currentDemandState = digitalRead(CONF_DEMAND_PIN);
  // if button pressed
  if(currentDemandState == HIGH) 
  {
    if((millis() - prevHoldMillis) >= 1000) 
    {
      holdCounter += 1;
      if(holdCounter > 3) holdCounter = 1;
      for(int i = 0; i < holdCounter; i++) {
        Serial.println("led On");
        delay(400);
        Serial.println("led Off");
      }
      prevHoldMillis = millis();
      disableAutoConnect = true;
    }
  }
  else if(currentDemandState == LOW && holdCounter > 0) 
  {
      switch(holdCounter) {
        case 1:
          Serial.println("Halted for 1 second");
          if(!forceConfig) forceConfig = true;
        break;

        case 2:
          Serial.println("Halted for 2 second");
          {
            Serial.println("Resetting configuration");
            wm.resetSettings();
            SPIFFS.remove(JSON_CONFIG_FILE);
            delay(3000);
            ESP.restart();
            delay(5000);
          }
        break;

        default:
          Serial.println("Halted for 3 second");
        break;
      }
      
      prevHoldMillis = 0;
      holdCounter = 0;
      disableAutoConnect = false;
  }

  if(forceConfig) 
  {
    if (!wm.startConfigPortal(ap_ssid, ap_password))
    {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again
      ESP.restart();
      delay(5000);
    }
    wifiConnected = true;
    forceConfig = false;
  }
  else {
    if(holdCounter == 0) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.print(millis());
        Serial.println("Reconnecting to WiFi...");
        wm.setEnableConfigPortal(false);
        if(wm.autoConnect(ap_ssid, ap_password)) 
        {
          wifiConnected = true;
        } else {
          wifiConnected = false;
        }
      }

      // other process here
      if(wifiConnected) {}
    }
  }

  // Save the custom parameters to FS
  if (shouldSaveConfig) {
    // If we get here, we are connected to the WiFi
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("subnetMask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("gatewayIP: ");
    Serial.println(WiFi.gatewayIP());

    // Lets deal with the user config values
    // Copy the string value
    Serial.print("tokenString: ");
    Serial.println(tokenString);
    strncpy(tokenString, token_textbox->getValue(), sizeof(tokenString));

    strncpy(serverString, server_textbox->getValue(), sizeof(serverString));
    Serial.print("serverString: ");
    Serial.println(serverString);

    //Convert the number value
    portNumber = atoi(port_textbox_num->getValue());
    Serial.print("portNumber Str: ");
    Serial.println(port_textbox_num->getValue());
    Serial.print("portNumber: ");
    Serial.println(portNumber);

    int buffIntval = atoi(interval_textbox_num->getValue());
    telemetrySendInterval = buffIntval > 86400 ? 86400 : buffIntval;
    Serial.print("Interval Str: ");
    Serial.println(interval_textbox_num->getValue());
    Serial.print("Interval: ");
    Serial.println(telemetrySendInterval);

    strncpy(descriptionString, description_textbox->getValue(), sizeof(descriptionString));
    Serial.print("descriptionString: ");
    Serial.println(descriptionString);

    saveConfigFile();
    shouldSaveConfig = false;
  }
}