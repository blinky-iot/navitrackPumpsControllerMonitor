// --- WiFi Connection with 30s Timeout
bool connectWiFi() {
  WiFi.begin(ssid, password);
  networkConnected = false;
  Serial.print("Connecting to WiFi");

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 30000;  // 30 seconds

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);

    if (millis() - startAttemptTime > timeout) {
      Serial.println("\n❌ WiFi connection timed out");
      return false;
    }
  }

  networkConnected = true;
  Serial.println("\n✅ WiFi connected");
  return true;
}


/*
  ------------------------------------------------------------------------------
  mqttConnect() – MQTT Broker Connection Handler
  ------------------------------------------------------------------------------

  Description:
  This function manages the connection to an MQTT broker using credentials
  provided via the global `deviceSettings` structure.

  It attempts to establish a connection with the broker and sets the MQTT
  callback function for handling incoming messages.

  ------------------------------------------------------------------------------
  Function Behavior:
  - Initializes connection parameters (host, port, token)
  - Repeatedly attempts to connect to the broker (inside a `while` loop)
  - If connection is successful:
      - Logs success message to Serial
      - Sets `mqttConnected = true`
      - Triggers `requestSharedAttributes()` to fetch initial shared config
      - Returns `true`
  - If connection fails:
      - Logs the failure state using `mqttClient.state()`
      - Sets `mqttConnected = false`
      - Returns `false` immediately (no retry delay active)

  ------------------------------------------------------------------------------
  Dependencies:
  - `PubSubClient` for MQTT connection management
  - `deviceSettings` object must contain:
      - `SERVER` (String/IP of MQTT broker)
      - `port` (int)
      - `TOKEN` (auth token for ThingsBoard or similar)
  - `callback` function for message handling must be defined elsewhere
  - `requestSharedAttributes()` should be implemented to fetch shared attributes

  ------------------------------------------------------------------------------
  Global Variables Modified:
  - `mqttConnected` – Boolean flag representing connection status

  ------------------------------------------------------------------------------
  Suggested Improvements:
  - Add exponential backoff or retry delay logic for persistent retry
  - Improve security by using SSL/TLS if supported by the broker
  - Parameterize client ID or generate dynamically

  ------------------------------------------------------------------------------
*/



// --- MQTT Connect
bool mqttConnect() {
  mqttConnected = false;
  mqttClient.setServer(deviceSettings.SERVER, deviceSettings.port);
  mqttClient.setCallback(callback);

  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT broker at ");
    Serial.print(deviceSettings.SERVER);
    Serial.print(":");
    Serial.println(deviceSettings.port);

    if (mqttClient.connect("Client", deviceSettings.TOKEN, NULL)) {
      Serial.println("✅ MQTT connected");
      mqttConnected = true;
      requestSharedAttributes();
      requestDigitalOutputAttributes();
      return true;
    } else {
      Serial.print("❌ MQTT connect failed. State: ");
      Serial.println(mqttClient.state());
      mqttConnected = false;
      return false;
      delay(2000);
    }
  }
}

// --- Request Shared Attributes

/*
  ------------------------------------------------------------------------------
  requestSharedAttributes() – Fetch Shared Attributes via MQTT
  ------------------------------------------------------------------------------

  Description:
  This function sends a request to the ThingsBoard platform (or compatible MQTT
  broker) to retrieve shared device attributes. These attributes may include
  configuration values such as:
    - Access token
    - Critical thresholds
    - Server address and port
    - Telemetry interval

  ------------------------------------------------------------------------------
  Behavior:
  1. Subscribes to two MQTT topics to receive attribute responses:
      - `v1/devices/me/attributes/response/+` for specific responses
      - `v1/devices/me/attributes` for general updates
  2. Constructs a JSON payload specifying the shared keys to request.
  3. Publishes the request to the topic:
      - `v1/devices/me/attributes/request/1`
  4. Waits up to 10 seconds (non-blocking loop) for the attributes response
      by continuously calling `mqttClient.loop()`.

  ------------------------------------------------------------------------------
  MQTT Topics:
  - **Subscribe**:
      - `v1/devices/me/attributes/response/+`
      - `v1/devices/me/attributes`
  - **Publish**:
      - `v1/devices/me/attributes/request/1` with payload like:
        `{"sharedKeys":"accessToken,High_Critical_Value,Low_Critical_Value,port,server,telemetryInterval"}`

  ------------------------------------------------------------------------------
  Output:
  - Logs success or failure of the publish operation to the serial console
  - Prints a message indicating that attribute request was sent

  ------------------------------------------------------------------------------
  Dependencies:
  - Requires an active MQTT connection (`mqttClient.connected() == true`)
  - `mqttClient` must be an instance of `PubSubClient` or compatible library
  - Shared attributes must be configured on the ThingsBoard device

  ------------------------------------------------------------------------------
  Suggestions:
  - Consider adding a timeout callback or flag to indicate success/failure
    in receiving attributes
  - Abstract the list of requested keys into a configurable variable or list

  ------------------------------------------------------------------------------
*/

void requestSharedAttributes() {
  attributesReceived = false;
  if (mqttClient.subscribe("v1/devices/me/attributes/response/+")) Serial.println("Device Configuration Attributes Subscription Success");
  if (mqttClient.subscribe("v1/devices/me/attributes")) Serial.println("Device Attributes Changes Subscription Success");
  String payload = "{\"sharedKeys\":\"accessToken,High_Critical_Value,Low_Critical_Value,port,server,telemetryInterval\"}";
  if (mqttClient.publish("v1/devices/me/attributes/request/1", payload.c_str())) Serial.println("Attribute Request Successful");
  else Serial.println("Attribute Request Failed");
  Serial.println("📡 Requested shared attributes");
  unsigned int now = millis();
  while (!attributesReceived && millis() - now < 10000) {
    mqttClient.loop();
  }
}






/*
  ------------------------------------------------------------------------------
  callback() – MQTT Attribute Response Handler
  ------------------------------------------------------------------------------

  Description:
  This function is triggered when an MQTT message is received on subscribed
  attribute topics. It is designed to process shared attribute updates sent
  from the ThingsBoard platform (or similar MQTT server) and apply them to the
  device’s runtime configuration.

  ------------------------------------------------------------------------------
  Topics Handled:
  - `v1/devices/me/attributes/response/+` (direct response to attribute request)
  - `v1/devices/me/attributes` (general shared attribute updates)

  ------------------------------------------------------------------------------
  Behavior:
  1. Converts the received payload (byte array) to a JSON string.
  2. Deserializes the JSON and extracts shared attributes, either flat or nested.
  3. Compares each attribute with the currently saved value.
  4. If differences are found:
     - Updates in-memory config values in `deviceSettings`
     - Flags `updated = true`
     - Flags `mqttNeedsRestart = true` for access token, server, or port changes
  5. If any config was updated:
     - Calls `saveConfig()` to persist changes to file
     - Calls `ESP.restart()` if a critical MQTT setting changed

  ------------------------------------------------------------------------------
  Attributes Handled:
  - `accessToken` → Updates `deviceSettings.TOKEN`
  - `server`      → Updates `deviceSettings.SERVER`
  - `port`        → Updates `deviceSettings.port`
  - `telemetryInterval` → Updates telemetry frequency in seconds
  - `High_Critical_Value` → Updates threshold for high alert logic
  - `Low_Critical_Value`  → Updates threshold for low alert logic

  ------------------------------------------------------------------------------
  Dependencies:
  - Global variables:
    - `deviceSettings` (configuration structure)
    - `high_critical_value`, `low_critical_value` (thresholds)
    - `config_filename` (used in saveConfig)
  - External functions:
    - `saveConfig(filename)` → saves updated config to persistent storage
  - Libraries:
    - ArduinoJson
    - PubSubClient (MQTT library)
    - ESP.restart()

  ------------------------------------------------------------------------------
  Notes:
  - The function handles both flat and nested JSON (e.g., wrapped under "shared")
  - Handles both numeric and string representations of numbers
  - Prints out all actions to Serial for debugging and traceability

  ------------------------------------------------------------------------------
  Suggestions:
  - Add fallback or retry logic for `saveConfig()` failure
  - Use a non-blocking approach for restart (e.g., scheduling via flag)
  - Add validation (e.g., value ranges) before applying attribute updates

  ------------------------------------------------------------------------------
*/

// void callback(char* topic, byte* payload, unsigned int length) {
//   Serial.println("📥 MQTT Callback Triggered");
//   Serial.print("📨 Topic: ");
//   Serial.println(topic);
//   String topicStr = String(topic);

//   // Convert payload to JSON string
//   String json;
//   for (unsigned int i = 0; i < length; i++) {
//     json += (char)payload[i];
//   }

//   Serial.print("📨 Payload: ");
//   Serial.println(json);

//   // Parse JSON
//   StaticJsonDocument<512> doc;
//   DeserializationError error = deserializeJson(doc, json);
//   if (error) {
//     Serial.print("❌ JSON parse error: ");
//     Serial.println(error.c_str());
//     return;
//   }

//   // Handle both wrapped and flat payloads
//   JsonObject root = doc.as<JsonObject>();
//   JsonObject shared = root.containsKey("shared") ? root["shared"].as<JsonObject>() : root;

//   if (String(topic).startsWith("v1/devices/me/attributes/response")) {
//     StaticJsonDocument<512> doc;
//     attributesReceived = true;  // ✅ Set flag
//     DeserializationError error = deserializeJson(doc, payload, length);

//     if (!error) {
//       JsonObject shared = doc["shared"];
//       if (!shared.isNull()) {
//         handleDigitalOutputAttributesPayload(shared);
//       }
//     } else {
//       Serial.print("❌ JSON Parse error: ");
//       Serial.println(error.c_str());
//     }
//   }


//   bool updated = false;
//   bool mqttNeedsRestart = false;

//   // --- Access Token
//   if (shared.containsKey("accessToken")) {
//     String val = shared["accessToken"].as<String>();
//     if (val != String(deviceSettings.TOKEN)) {
//       val.toCharArray(deviceSettings.TOKEN, sizeof(deviceSettings.TOKEN));
//       updated = true;
//       mqttNeedsRestart = true;
//       Serial.println("🔐 Updated accessToken");
//     }
//   }

//   // --- Server
//   if (shared.containsKey("server")) {
//     String val = shared["server"].as<String>();
//     if (val != String(deviceSettings.SERVER)) {
//       val.toCharArray(deviceSettings.SERVER, sizeof(deviceSettings.SERVER));
//       updated = true;
//       mqttNeedsRestart = true;
//       Serial.println("🌐 Updated server");
//     }
//   }

//   // --- Port
//   if (shared.containsKey("port")) {
//     JsonVariant p = shared["port"];
//     int newPort = p.is<int>() ? p.as<int>() : String(p.as<const char*>()).toInt();
//     if (newPort != deviceSettings.port && newPort != 0) {
//       deviceSettings.port = newPort;
//       updated = true;
//       mqttNeedsRestart = true;
//       Serial.println("🔌 Updated port");
//     }
//   }

//   // --- telemetryInterval
//   if (shared.containsKey("telemetryInterval")) {
//     JsonVariant t = shared["telemetryInterval"];
//     int newInterval = t.is<int>() ? t.as<int>() : String(t.as<const char*>()).toInt();
//     if (newInterval != deviceSettings.telemetryInterval && newInterval > 0) {
//       deviceSettings.telemetryInterval = newInterval;
//       updated = true;
//       Serial.println("📊 Updated telemetryInterval");
//     }
//   }

//   // --- High Critical Value
//   if (shared.containsKey("High_Critical_Value")) {
//     JsonVariant hcv = shared["High_Critical_Value"];
//     float newHigh = hcv.is<float>() ? hcv.as<float>() : String(hcv.as<const char*>()).toFloat();
//     if (newHigh != high_critical_value && newHigh != 0) {
//       high_critical_value = newHigh;
//       Serial.println("📈 Updated High_Critical_Value to: " + String(high_critical_value, 1));
//     }
//   }

//   // --- Low Critical Value
//   if (shared.containsKey("Low_Critical_Value")) {
//     JsonVariant lcv = shared["Low_Critical_Value"];
//     float newLow = lcv.is<float>() ? lcv.as<float>() : String(lcv.as<const char*>()).toFloat();
//     if (newLow != low_critical_value) {
//       low_critical_value = newLow;
//       Serial.println("📉 Updated Low_Critical_Value to: " + String(low_critical_value, 1));
//     }
//   }

//   // --- Save & Act
//   if (updated) {
//     Serial.println("💾 Attributes updated, saving config...");
//     saveConfig(config_filename);

//     if (mqttNeedsRestart) {
//       Serial.println("🔄 Critical config changed. Restarting Device!");
//       ESP.restart();
//     }
//   } else {
//     Serial.println("✔️ Parameters Updated. Restart not required");
//   }
// }

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("📥 MQTT Callback Triggered");
  Serial.print("📨 Topic: ");
  Serial.println(topic);

  // Convert payload to String
  String json;
  for (unsigned int i = 0; i < length; i++) {
    json += (char)payload[i];
  }

  Serial.print("📨 Payload: ");
  Serial.println(json);

  // Parse JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  JsonObject shared = root.containsKey("shared") ? root["shared"].as<JsonObject>() : root;

  // Handle digital output updates for both wrapped and flat formats
  handleDigitalOutputAttributesPayload(shared);

  // Handle general parameters only for response topic
  String topicStr = String(topic);
  if (topicStr.startsWith("v1/devices/me/attributes/response")) {
    attributesReceived = true;
  }

  // -------- GENERAL PARAMETER UPDATES --------
  bool updated = false;
  bool mqttNeedsRestart = false;

  // --- Access Token
  if (shared.containsKey("accessToken")) {
    String val = shared["accessToken"].as<String>();
    if (val != String(deviceSettings.TOKEN)) {
      val.toCharArray(deviceSettings.TOKEN, sizeof(deviceSettings.TOKEN));
      updated = true;
      mqttNeedsRestart = true;
      Serial.println("🔐 Updated accessToken");
    }
  }

  // --- Server
  if (shared.containsKey("server")) {
    String val = shared["server"].as<String>();
    if (val != String(deviceSettings.SERVER)) {
      val.toCharArray(deviceSettings.SERVER, sizeof(deviceSettings.SERVER));
      updated = true;
      mqttNeedsRestart = true;
      Serial.println("🌐 Updated server");
    }
  }

  // --- Port
  if (shared.containsKey("port")) {
    JsonVariant p = shared["port"];
    int newPort = p.is<int>() ? p.as<int>() : String(p.as<const char*>()).toInt();
    if (newPort != deviceSettings.port && newPort != 0) {
      deviceSettings.port = newPort;
      updated = true;
      mqttNeedsRestart = true;
      Serial.println("🔌 Updated port");
    }
  }

  // --- telemetryInterval
  if (shared.containsKey("telemetryInterval")) {
    JsonVariant t = shared["telemetryInterval"];
    int newInterval = t.is<int>() ? t.as<int>() : String(t.as<const char*>()).toInt();
    if (newInterval != deviceSettings.telemetryInterval && newInterval > 0) {
      deviceSettings.telemetryInterval = newInterval;
      updated = true;
      Serial.println("📊 Updated telemetryInterval");
    }
  }

  // --- High Critical Value
  if (shared.containsKey("High_Critical_Value")) {
    JsonVariant hcv = shared["High_Critical_Value"];
    float newHigh = hcv.is<float>() ? hcv.as<float>() : String(hcv.as<const char*>()).toFloat();
    if (newHigh != high_critical_value && newHigh != 0) {
      high_critical_value = newHigh;
      Serial.println("📈 Updated High_Critical_Value to: " + String(high_critical_value, 1));
    }
  }

  // --- Low Critical Value
  if (shared.containsKey("Low_Critical_Value")) {
    JsonVariant lcv = shared["Low_Critical_Value"];
    float newLow = lcv.is<float>() ? lcv.as<float>() : String(lcv.as<const char*>()).toFloat();
    if (newLow != low_critical_value) {
      low_critical_value = newLow;
      Serial.println("📉 Updated Low_Critical_Value to: " + String(low_critical_value, 1));
    }
  }

  // --- Save & Act
  if (updated) {
    Serial.println("💾 Attributes updated, saving config...");
    saveConfig(config_filename);

    if (mqttNeedsRestart) {
      Serial.println("🔄 Critical config changed. Restarting Device!");
      ESP.restart();
    }
  } else {
    Serial.println("✔️ Parameters Updated.");
  }
}


void maintainWiFiConnection() {
  static unsigned long lastCheck = 0;
  const unsigned long checkInterval = 10000;  // 10 seconds

  unsigned long now = millis();
  if (now - lastCheck < checkInterval && onStartup) return;
  onStartup = true;  //allow for wifi configuration on startup
  lastCheck = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected. Reconnecting...");
    connectWiFi();
  }
}



// --- Loop
/*
  ------------------------------------------------------------------------------
  telemetryLoop() – Periodic MQTT Telemetry Publisher
  ------------------------------------------------------------------------------

  Description:
  This function is designed to be called repeatedly from the main loop.
  It handles maintaining network connectivity (Wi-Fi or GSM) and ensures
  that telemetry is sent at a defined interval via MQTT.

  ------------------------------------------------------------------------------
  Dependencies:
  - Global:
    - `deviceSettings.telemetryInterval` – defines telemetry interval in seconds
    - `telemetryPayload` – preformatted JSON string (from sensor reading task)
    - `mqttClient` – an instance of PubSubClient
  - Functions:
    - `maintainWiFiConnection()` – ensures Wi-Fi remains connected (if enabled)
    - `maintainGSMConnectivity()` – ensures GSM remains connected (if enabled)
    - `mqttConnect()` – attempts to (re)connect to the MQTT broker

  ------------------------------------------------------------------------------
  Behavior:
  1. Checks connectivity based on the active connection mode:
     - If `USE_WIFI` is defined, calls `maintainWiFiConnection()`
     - If `USE_GSM` is defined, calls `maintainGSMConnectivity()`
  2. If the time since `lastSent` exceeds the configured telemetry interval:
     - Checks if the MQTT client is connected
       - If yes and the telemetry payload is non-empty (`{}` or larger), publishes to:
         `v1/devices/me/telemetry`
       - If not connected, attempts to reconnect using `mqttConnect()`
     - Updates `lastSent` with the current timestamp (from `millis()`)

  3. Calls `mqttClient.loop()` every cycle to ensure MQTT events are processed

  ------------------------------------------------------------------------------
  Timing:
  - Uses `millis()` and `lastSent` to determine when to send telemetry
  - Telemetry is sent every `deviceSettings.telemetryInterval * 1000` ms

  ------------------------------------------------------------------------------
  Global Variables:
  - `unsigned long lastSent` – timestamp (in ms) of the last successful telemetry push

  ------------------------------------------------------------------------------
  Notes:
  - Avoids sending empty JSON payloads (`{}`) by checking payload length
  - Must be called inside the `loop()` function for continuous operation
  - Designed for use in systems that publish telemetry via MQTT to platforms
    like ThingsBoard, Home Assistant, or custom brokers

  ------------------------------------------------------------------------------
*/

unsigned long lastSent = 0;
void telemetryLoop() {

#ifdef USE_WIFI
  maintainWiFiConnection();
#endif

#ifdef USE_GSM
  maintainGSMConnectivity();
#endif
  if (millis() - lastSent > deviceSettings.telemetryInterval * 1000) {
    if (mqttClient.connected()) {
      //requestDigitalOutputAttributes();
      //sendDigitalOutputsTelemetry();
      //monitorDigitalInputs();
    }
  }
  sendDigitalOutputsTelemetry();
  monitorDigitalInputs();
  requestDigitalOutputAttributes();
  mqttClient.loop();
}


void powerOnModem() {
#ifdef blinkBoard
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, LOW);
  delay(100);
  digitalWrite(MODEM_RST, HIGH);
  delay(5000);  // Allow modem to boot up
#endif

#ifdef powWaterBoard
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);
  delay(100);
  digitalWrite(MODEM_RST, LOW);
  delay(5000);  // Allow modem to boot up
#endif
}



// --- GSM MQTT Maintenance Function
void maintainGSMConnectivity() {
  SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);  // Give time for modem to wake
  static unsigned long lastCheck = 0;
  const unsigned long checkInterval = 10000;
  static bool modemInitialized = false;

  if (millis() - lastCheck < checkInterval && onStartup) return;
  else onStartup = true;  // allow for GSM config on startup
  lastCheck = millis();

  // (Re)initialize modem if needed
  if (!modemInitialized || !modem.isNetworkConnected()) {
    SerialMon.println("⚠️ GSM not connected. Restarting modem...");
    powerOnModem();
    modem.restart();
    modemInitialized = true;

    if (!modem.waitForNetwork(30000L)) {
      SerialMon.println("❌ Network not found");
      modemInitialized = false;
      return;
    }
    SerialMon.println("✅ GSM Network Connected");
  }

  // GPRS connection
  if (!modem.isGprsConnected()) {
    SerialMon.println("🌐 GPRS not connected. Attempting reconnect...");
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      SerialMon.println("❌ GPRS failed");
      networkConnected = false;
      return;
    }
    SerialMon.println("✅ GPRS Connected");
    networkConnected = true;
  }

  // MQTT connection
  if (!mqttClient.connected()) {
    mqttConnect();
  }

  mqttClient.loop();  // Always call loop to maintain MQTT connection
}
