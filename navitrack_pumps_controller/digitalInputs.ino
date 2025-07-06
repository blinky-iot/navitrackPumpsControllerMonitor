

// === Function to Read PCF8574 Inputs ===
uint8_t readPCF8574Inputs(uint8_t i2cAddress) {
  Wire.requestFrom(i2cAddress, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  } else {
    Serial.println("⚠️ Failed to read PCF8574");
    return 0xFF;
  }
}

// === Function to Send Digital Input Telemetry ===
void sendDigitalInputTelemetry(uint8_t inputState) {
  String payload = "{";
  for (uint8_t i = 0; i < 8; i++) {
    payload += "\"DI" + String(i) + "\":" + String(bitRead(inputState, i));
    if (i < 7) payload += ",";
  }
  payload += "}";

  if (mqttClient.publish("v1/devices/me/telemetry", payload.c_str())) {
    Serial.print("📤 DI Telemetry Sent: ");
    Serial.println(payload);
  } else {
    Serial.println("❌ Failed to send digital input telemetry");
  }
}

// === Function to Call Periodically in loop() or Task ===
void monitorDigitalInputs() {
  uint8_t currentState = readPCF8574Inputs(PCF8574_ADDR);
  unsigned long now = millis();

  bool changed = (currentState != digitalInputBuffer);
  bool intervalElapsed = (now - lastDigitalTelemetryTime > digitalInputsTelemetryInterval);

  if (changed || intervalElapsed) {
    digitalInputBuffer = currentState;
    sendDigitalInputTelemetry(currentState);
    lastDigitalTelemetryTime = now;
  }
}
