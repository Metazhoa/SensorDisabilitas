#define ESP_DRD_USE_SPIFFS false

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

#include "DHT.h"

#define DHTPIN 4
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

DHT dht(DHTPIN, DHTTYPE);

#define CONF_DEMAND_PIN 34
#define ALARM_INPUT_PIN 18

enum mode_led_t {
  OPMODE_LED,
  TELEMETRY_LED,
  ERROR_LED
};

#define RED_LED 26
#define GREEN_LED 27
#define BLUE_LED 14

// JSON configuration file
#define JSON_CONFIG_FILE "/node_config.json"

// Change to true when testing to force configuration every time we run
bool forceConfig = false;

char ap_ssid[23];
const char *ap_password = "password123";

// MQTT Variables
char tokenString[31] = "YOUR_DEVICE_ACCESS_TOKEN";
char serverString[21] = "demo.thingsboard.io";
char descriptionString[25] = "DESCRIBE DEVICE";

int portNumber = 1883;
int telemetrySendInterval = 1;

float lastHumidity;
float lastTemperature;
uint8_t lastAlarmState = 1;

constexpr uint32_t MAX_MESSAGE_SIZE = 512U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

// Initialize underlying client, used to establish a connection
WiFiClient wifiClient;

// Initalize the Mqtt client instance
Arduino_MQTT_Client mqttClient(wifiClient);

// Initialize ThingsBoard instance with the maximum needed buffer size, stack size and the apis we want to use
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

// Temporary TimeCounter
uint32_t previousDataSendMillis; // Last Telemetry Millisecond
uint32_t lastdhtSampleMillis; // Last DHT Sampling Millisecond
uint32_t prevButtonHoldMillis; // Last Mode Button Pressed Millis

uint8_t holdCounter = 0; // Temporary Button Counter
bool telemetry_sent = false;
bool sendIntervalAttribute = true;

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
    } else {
      saveConfigFile();
      delay(3000);
      //reset and try again
      ESP.restart();
      delay(5000);
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
}

void configModeCallback(WiFiManager *myWiFiManager) // Called when config mode launched
{
  Serial.println("Entered Configuration Mode");

  Serial.print("Config SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());

  Serial.print("Config IP Address: ");
  Serial.println(WiFi.softAPIP());
}

void setLedState(enum mode_led_t selectedLed, bool active) 
{
  if(active) {
    switch(selectedLed){
      case OPMODE_LED:
      {
        setRGBLed(0, 0, 255);
      }
      break;
      case ERROR_LED:
      {
        setRGBLed(255, 0, 0);
      }
      break;
      case TELEMETRY_LED:
      {
        setRGBLed(0, 255, 0);
      }
      break;
    }
  }
  else {
    setRGBLed(0, 0, 0);
  }
}

void setRGBLed(int red, int green, int blue)
{
  analogWrite(RED_LED, red);
  analogWrite(GREEN_LED, green);
  analogWrite(BLUE_LED, blue);
}

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

  dht.begin();

  // Setup Serial monitor
  Serial.begin(115200);
  pinMode(CONF_DEMAND_PIN, INPUT);
  pinMode(ALARM_INPUT_PIN, INPUT);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  delay(10);

  char ssid[23];
  snprintf(ap_ssid, 23, "SENSE-%llX", ESP.getEfuseMac());

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
  wm.setConfigPortalTimeout(240);
  setLedState(ERROR_LED, true);
}

void loop() {
  delay(10);

  if(millis() - lastdhtSampleMillis > 2000) {
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Fahrenheit (the default)
    float t = dht.readTemperature(true);

    lastdhtSampleMillis = millis();

    // Check if any reads failed and exit early (to try again).
    if (!isnan(h) || !isnan(t)) {
      lastHumidity = h;
      lastTemperature = t;
    }
    
  }

  if(telemetry_sent && (millis() - previousDataSendMillis) > 200) {
    setLedState(TELEMETRY_LED, false);
    telemetry_sent = false;
  }

  int currentDemandState = digitalRead(CONF_DEMAND_PIN);
  // if button pressed
  if(currentDemandState == LOW) 
  {
    // wait for 1 second
    if(holdCounter == 0) { 
      delay(3000);
    }

    setRGBLed(0, 0, 0);

    if(currentDemandState == LOW && (millis() - prevButtonHoldMillis) >= 1000) 
    {
      holdCounter += 1;
      if(holdCounter > 3) holdCounter = 1;

      int duration = 600 / (holdCounter + 1);
      for(int i = 0; i < holdCounter; i++) {
        setLedState(OPMODE_LED, true);
        delay(duration);
        setLedState(OPMODE_LED, false);
        delay(400);
      }
      prevButtonHoldMillis = millis();
    }
  }
  else if(currentDemandState == HIGH && holdCounter > 0) 
  {
      switch(holdCounter) {
        case 1:
          Serial.println("Halted for 1 second");
          forceConfig = true;
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
      
      prevButtonHoldMillis = 0;
      holdCounter = 0;
  }

  if(forceConfig) 
  {
    if (!wm.startConfigPortal((const char*)ap_ssid, ap_password))
    {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again
      ESP.restart();
      delay(5000);
    }

    // If we get here, we are connected to the WiFi
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("subnetMask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("gatewayIP: ");
    Serial.println(WiFi.gatewayIP());

    forceConfig = false;
    sendIntervalAttribute = true;
  }
  else {
    if(holdCounter == 0) {
      // other process here
      if(WiFi.status() == WL_CONNECTED) {

        if (!tb.connected()) {
          // Connect to the ThingsBoard
          Serial.print("Connecting to: ");
          Serial.print((const char*)&serverString);
          Serial.print(" with token ");
          Serial.println((const char*)&tokenString);

          if (!tb.connect((const char*)&serverString, (const char*)&tokenString, portNumber)) {
            Serial.println("Failed to connect");
            setLedState(ERROR_LED, true);
            return;
          }

          setLedState(ERROR_LED, false);
          Serial.print("Connected to: ");
          Serial.println((const char*)&serverString);
          // Sending a MAC address as an attribute
          tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
        }

        uint8_t alarmState = !digitalRead(ALARM_INPUT_PIN); // Inverted Value
        if(alarmState != lastAlarmState) {
          lastAlarmState = alarmState;
          tb.sendTelemetryData("alarmState", lastAlarmState);
          Serial.print("Alarm ");
          Serial.println((alarmState ? "Raised" : "Standby"));
        }

        // Sending telemetry every telemetrySendInterval time
        if (millis() - previousDataSendMillis > (telemetrySendInterval * 1000)) 
        {
          tb.sendTelemetryData("temperature", lastTemperature);
          tb.sendTelemetryData("humidity", lastHumidity);
          
          if(sendIntervalAttribute) {
            tb.sendAttributeData("interval", telemetrySendInterval);
            sendIntervalAttribute = false;
          }
          tb.sendAttributeData("rssi", WiFi.RSSI());
          tb.sendAttributeData("channel", WiFi.channel());
          tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
          tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
          tb.sendAttributeData("ssid", WiFi.SSID().c_str());
          
          setLedState(TELEMETRY_LED, true);
          telemetry_sent = true;

          previousDataSendMillis = millis();

          Serial.print("temperature: ");
          Serial.print(lastTemperature);
          Serial.print(" | humidity: ");
          Serial.print(lastHumidity);
          Serial.print(" | alarmState: ");
          Serial.print(lastAlarmState);

          Serial.print(" | interval: ");
          Serial.print(telemetrySendInterval);

          Serial.print(" | rssi: ");
          Serial.print(WiFi.RSSI());
          Serial.print(" | channel: ");
          Serial.print(WiFi.channel());
          Serial.print(" | bssid: ");
          Serial.print(WiFi.BSSIDstr().c_str());
          Serial.print(" | localIp: ");
          Serial.print(WiFi.localIP().toString().c_str());
          Serial.print(" | ssid: ");
          Serial.println(WiFi.SSID().c_str());
        }
        
      } else {

        setLedState(ERROR_LED, true);
        Serial.println("Reconnecting to WiFi...");
        wm.setEnableConfigPortal(false);
        wm.autoConnect((const char*)ap_ssid, ap_password);
        sendIntervalAttribute = true;
      }
    }
  }

  tb.loop();
  // 24 hours equals 86400 seconds
  if(millis() > (86400 * 1000)) {
    delay(3000);
    //reset module
    ESP.restart();
  }
}