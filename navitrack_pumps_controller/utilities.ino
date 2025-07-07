void HourlyResetTask(void* pvParameters) {
  while (true) {
    vTaskDelay(3600000 / portTICK_PERIOD_MS);  // 1 hour delay
    //vTaskDelay(60000 / portTICK_PERIOD_MS);  // 1 minute for testing
    Serial.println("🔁 Resetting ESP32 (hourly task)...");
    delay(100);
    ESP.restart();
  }
}


void hourlyResetSetup() {
  // In setup:
  xTaskCreatePinnedToCore(
    HourlyResetTask, "HourlyReset", 2048, NULL, 1, NULL, 1  // core 1 is typical for background
  );
}



// --- Save Config to LittleFS
bool saveConfig(const char* filename) {
  StaticJsonDocument<256> doc;
  doc["accessToken"] = deviceSettings.TOKEN;
  doc["server"] = deviceSettings.SERVER;
  doc["port"] = deviceSettings.port;
  doc["telemetryInterval"] = deviceSettings.telemetryInterval;

  String jsonStr;
  serializeJson(doc, jsonStr);
  writeFile(filename, jsonStr);
  return true;
}



// --- Read Config from LittleFS
bool readConfig(const char* filename) {
  Serial.println("📂 Reading config file: " + String(filename));

  String content = readFile(filename);
  if (content == "") {
    Serial.println("❌ Config file empty or missing");
    return false;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(error.c_str());
    return false;
  }

  // --- accessToken
  String tok = doc["accessToken"] | "default_token";
  tok.toCharArray(deviceSettings.TOKEN, sizeof(deviceSettings.TOKEN));
  Serial.println("🔐 accessToken: " + String(deviceSettings.TOKEN));

  // --- server
  String srv = doc["server"] | "telemetry.blinkelectrics.co.ke";
  srv.toCharArray(deviceSettings.SERVER, sizeof(deviceSettings.SERVER));
  Serial.println("🌐 server: " + String(deviceSettings.SERVER));

  // --- port
  JsonVariant portVal = doc["port"];
  if (portVal.is<int>()) {
    deviceSettings.port = portVal.as<int>();
  } else if (portVal.is<const char*>()) {
    deviceSettings.port = String(portVal.as<const char*>()).toInt();
  } else {
    deviceSettings.port = 1883;
  }
  Serial.println("🔌 port: " + String(deviceSettings.port));

  // --- telemetryInterval
  JsonVariant intervalVal = doc["telemetryInterval"];
  if (intervalVal.is<int>()) {
    deviceSettings.telemetryInterval = intervalVal.as<int>();
  } else if (intervalVal.is<const char*>()) {
    deviceSettings.telemetryInterval = String(intervalVal.as<const char*>()).toInt();
  } else {
    deviceSettings.telemetryInterval = 60;
  }
  Serial.println("📊 telemetryInterval: " + String(deviceSettings.telemetryInterval));

  Serial.println("✅ Config loaded successfully.");
  return true;
}




// --- Write File
void writeFile(const char* filename, String message) {
  File file = LittleFS.open(filename, "w");
  if (!file) return;
  file.print(message);
  file.close();
}

// --- Read File
String readFile(const char* filename) {
  File file = LittleFS.open(filename, "r");
  if (!file) return "";
  String content = file.readString();
  file.close();
  return content;
}


void setOutputState(uint8_t pin, bool value) {
  if (pin >= 16) return;  // protect against out-of-range pins
  bitWrite(outputStates[pin / 8], pin % 8, value);
  sr.setAll(outputStates);  // apply updated state
}

void outputUpdateTask(void* parameter) {
  static uint8_t previousState = 0;

  while (true) {
    if (outputBuffer != previousState) {
      sr.setAll(&outputBuffer);  // Push the updated output state to the shift register
      previousState = outputBuffer;

      // Serial.print("🔄 Output buffer updated: ");
      // Serial.println(outputBuffer, BIN);  // Print binary for visibility
      char binStr[9];  // 8 bits + null terminator
      for (int i = 7; i >= 0; i--) {
        binStr[7 - i] = bitRead(outputBuffer, i) ? '1' : '0';
      }
      binStr[8] = '\0';  // null-terminate the string

      Serial.print("🔄 Output buffer updated: ");
      Serial.println(binStr);
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
  }
}

void initialiseOutputs() {
  sr.setAllLow();  // Start with all outputs LOW

  // Start the task on Core 1
  xTaskCreatePinnedToCore(
    outputUpdateTask,
    "OutputUpdateTask",
    2048,
    NULL,
    1,
    NULL,
    1);
}
