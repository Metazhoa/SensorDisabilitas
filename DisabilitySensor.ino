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

// JSON configuration file
#define JSON_CONFIG_FILE "/node_config.json"

#define CONF_DEMAND_PIN 14
#define CONF_RESET_PIN 12

// Flag for saving data
bool shouldSaveConfig = false;

// MQTT Variables
char tokenString[50] = "YOUR_DEVICE_ACCESS_TOKEN";
char serverString[30] = "demo.thingsboard.io";
int portNumber = 1883;

// Change to true when testing to force configuration every time we run
bool forceConfig = false;
WiFiManager wm;

void saveConfigFile() // Save Config in JSON format
{
  Serial.println(F("Saving configuration..."));
  
  // Create a JSON document
  StaticJsonDocument<512> json;
  
  json["token"] = tokenString;
  json["server"] = serverString;
  json["port"] = portNumber;

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
  if (SPIFFS.begin(false) || SPIFFS.begin(true))
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

const bool connected() {
  // Check to ensure we aren't connected yet
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    return true;
  }

  return false;
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

  // Setup Serial monitor
  Serial.begin(115200);
  pinMode(CONF_DEMAND_PIN, INPUT_PULLUP);
  pinMode(CONF_RESET_PIN, INPUT_PULLUP);
  delay(10);

  // Reset settings (only for development)
  // wm.resetSettings();

  // Set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);

  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback(configModeCallback);
}

void loop() {
  delay(10);
  // put your main code here, to run repeatedly:
  int currentTriggerState = digitalRead(CONF_DEMAND_PIN);
  if(!connected() || forceConfig || currentTriggerState == LOW) {

    // Token TextBox
    WiFiManagerParameter token_textbox("token", "Thingsboard Token", tokenString, 50);
    // Server TextBox
    WiFiManagerParameter server_textbox("server", "Thingsboard Server", serverString, 30);
    
    // Need to convert numerical input to string to display the default value.
    char numericBuffer[6];
    sprintf(numericBuffer, "%d", portNumber); 
    
    // Port TextBox (Number) - 6 + 1 spare characters maximum
    WiFiManagerParameter port_textbox_num("port_num", "Thingsboard Server port number", numericBuffer, 7);

    wm.addParameter(&token_textbox);
    wm.addParameter(&server_textbox);
    wm.addParameter(&port_textbox_num);

    wm.setShowStaticFields(true);
    wm.setShowDnsFields(true);

    if(!connected() && currentTriggerState != LOW) {
      if (!wm.autoConnect("NEWTEST_AP", "password"))
      {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again
        ESP.restart();
        delay(5000);
      }
    } else if(forceConfig || currentTriggerState == LOW) {
      if (!wm.startConfigPortal("NEWTEST_AP", "password"))
      {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again
        ESP.restart();
        delay(5000);
      }
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

    // Lets deal with the user config values
    // Copy the string value
    strncpy(tokenString, token_textbox.getValue(), sizeof(tokenString));
    Serial.print("tokenString: ");
    Serial.println(tokenString);

    strncpy(serverString, server_textbox.getValue(), sizeof(serverString));
    Serial.print("serverString: ");
    Serial.println(serverString);

    //Convert the number value
    portNumber = atoi(port_textbox_num.getValue());
    Serial.print("portNumber Str: ");
    Serial.println(port_textbox_num.getValue());
    Serial.print("portNumber: ");
    Serial.println(portNumber);
    
    // Save the custom parameters to FS
    if (shouldSaveConfig) {
      saveConfigFile();
    }
  }

  int currentResetState = digitalRead(CONF_RESET_PIN);
  if(currentResetState == LOW) {
    wm.resetSettings();
    SPIFFS.remove(JSON_CONFIG_FILE);
    ESP.restart();
    delay(5000);
  }
}