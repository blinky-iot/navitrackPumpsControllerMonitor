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
  else onStartup = true; // allow for GSM config on startup
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
