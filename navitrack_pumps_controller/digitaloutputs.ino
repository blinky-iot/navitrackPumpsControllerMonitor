// bool requestDigitalOutputAttributes() {
//   bool intervalElapsed = (now - lastDigitalTelemetryTime > deviceSettings.telemetryInterval * 1000);
//   attributesReceived = false;
//   if (intervalElapsed) {
//     // Request DO0–DO7 shared attributes from ThingsBoard
//     String payload = "{\"sharedKeys\":\"DO0,DO1,DO2,DO3,DO4,DO5,DO6,DO7\"}";
//     Serial.println("📡 Requested DO0–DO7 attributes");

//     bool success = mqttClient.publish("v1/devices/me/attributes/request/1", payload.c_str());
//     if (success) {
//       Serial.println("✅ Digital Output Attribute Request Successful");
//     } else {
//       Serial.println("❌ Digital Output Attribute Request Failed");
//       return false;
//     }

//     // Allow time for callback to process the response
//     unsigned long start = millis();
//     while (!attributesReceived && millis() - start < 10000) {
//       mqttClient.loop();  // critical for receiving the attributes
//       delay(10);          // prevent watchdog timeout
//     }

//     return true;
//   }
// }

bool requestDigitalOutputAttributes() {
  static bool onstartup = true;
  unsigned long now = millis();
  bool intervalElapsed = (now - lastAttributesRequestTime > deviceSettings.telemetryInterval * 1000 || onstartup);

  if (!intervalElapsed) {
    return false;  // No need to request yet
  }

  attributesReceived = false;

  // Construct request payload
  String payload = "{\"sharedKeys\":\"DO0,DO1,DO2,DO3,DO4,DO5,DO6,DO7\"}";
  Serial.println("📡 Requesting DO0–DO7 attributes...");

  bool success = mqttClient.publish("v1/devices/me/attributes/request/1", payload.c_str());
  if (!success) {
    Serial.println("❌ Digital Output Attribute Request Failed");
    return false;
  }

  Serial.println("✅ Attribute request published successfully");

  // Wait up to 10 seconds for response
  unsigned long start = millis();
  while (!attributesReceived && millis() - start < 10000) {
    mqttClient.loop();
    delay(10);
  }

  if (attributesReceived) {
    Serial.println("📥 Attributes received successfully");
    lastAttributesRequestTime = now;
    onstartup = false;
    return true;
  } else {
    Serial.println("⚠️ Timed out waiting for attribute response");
    return false;
  }
}



void handleDigitalOutputAttributesPayload(const JsonObject& obj) {
  JsonObject root;

  // If the payload is nested under "shared", unwrap it
  if (obj.containsKey("shared") && obj["shared"].is<JsonObject>()) {
    root = obj["shared"].as<JsonObject>();
  } else {
    root = obj;
  }

  for (uint8_t i = 0; i < 8; i++) {
    String key = "DO" + String(i);
    if (root.containsKey(key)) {
      bool value = root[key];
      bitWrite(outputBuffer, i, value);
      Serial.printf("📥 DO%d attribute received, set buffer bit to %d\n", i, value);
    }
  }

  Serial.println("✔️ Output buffer updated from attribute payload.");
}




void sendDigitalOutputsTelemetry() {
  static uint8_t lastDigitalTelemetryBuffer = 0;
  static unsigned long lastDigitalTelemetrySent = 0;

  if (!mqttClient.connected()) {
    Serial.println("MQTT Client Not Connected for telemetry sending");
    return;  // Skip telemetry if MQTT not connected
  }

  bool changed = (outputBuffer != lastDigitalTelemetryBuffer);
  bool intervalElapsed = (millis() - lastDigitalTelemetrySent > deviceSettings.telemetryInterval * 1000);

  if (changed || intervalElapsed) {
    StaticJsonDocument<128> doc;

    for (uint8_t i = 0; i < 8; i++) {
      doc["DO" + String(i)] = bitRead(outputBuffer, i);
    }

    char payload[128];
    serializeJson(doc, payload, sizeof(payload));

    bool success = mqttClient.publish("v1/devices/me/telemetry", payload);
    if (success) {
      Serial.print("📤 DO Telemetry sent: ");
      Serial.println(payload);
      lastDigitalTelemetrySent = millis();
      lastDigitalTelemetryBuffer = outputBuffer;
    } else {
      Serial.print("📤 DO Telemetry NOT sent: ");
      Serial.println(payload);
      Serial.println("❌ Failed to send digital output telemetry");
    }
  }
}