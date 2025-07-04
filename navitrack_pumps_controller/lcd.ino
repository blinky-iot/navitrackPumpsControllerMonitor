// Last known good timestamps
unsigned long sensor1_last_good = 0;
unsigned long ds_last_good = 0;

// Debounced status
bool sensor1_stable_ok = false;
bool ds_stable_ok = false;
int hysteresis = 1;
// Constants
const unsigned long debouncePeriod = 10000;  // 30 seconds

void LCDTask(void *pvParameters) {
  static float lastGoodTemp1 = -100.0;
  static float lastGoodHumidity1 = -100.0;
  static float lastGoodDSTemp = -100.0;

  static unsigned long lastUpdate = 0;

  const int updateInterval = 1000;

  static String lastLine0 = "";
  static String lastLine1 = "";

  bool highAlert = false;
  bool lowAlert = false;

  static bool showNetworkStatus = true;
  static unsigned long lastToggleTime = 0;
  const unsigned long toggleInterval = 3000;  // 3 seconds

  for (;;) {
    float maxTemp = max(Temperature1, DS_Temperature);
    // Serial.print("max temperature: ");
    // Serial.println(maxTemp);

    // Evaluate High Alert first (takes precedence)
    if (maxTemp > high_critical_value) {
      highAlert = true;
      lowAlert = false;  // clear lowAlert when in highAlert
      Serial.println("High Alert");
    } else {
      highAlert = false;
    }

    // Only check for lowAlert if highAlert is NOT active
    if (!highAlert) {
      if (maxTemp > low_critical_value) {
        Serial.print("low_critical_value ");
        Serial.println(low_critical_value);
        Serial.println("LOW Alert");
        lowAlert = true;
      } else {
        lowAlert = false;
      }
    }

    if (highAlert || lowAlert) {
      toggleBacklight();
    } else lcd.backlight();


    unsigned long now = millis();

    // --- Update debounced sensor flags
    if (sensor1_ok) sensor1_last_good = now;
    if (ds_ok) ds_last_good = now;

    sensor1_stable_ok = (now - sensor1_last_good <= debouncePeriod);
    ds_stable_ok = (now - ds_last_good <= debouncePeriod);

    // --- Cache valid readings
    if (sensor1_ok) {
      lastGoodTemp1 = Temperature1;
      if (Humidity1 >= 0 && Humidity1 <= 100) {
        lastGoodHumidity1 = Humidity1;
      }
    }
    if (ds_ok) {
      lastGoodDSTemp = DS_Temperature;
    }

    // --- Determine which temperature to evaluate
    float activeTemp = ds_stable_ok ? lastGoodDSTemp : lastGoodTemp1;

    // --- Apply hysteresis logic
    if (activeTemp >= high_critical_value && !highAlert) {
      highAlert = true;
    } else if (activeTemp <= (high_critical_value - hysteresis) && highAlert) {
      highAlert = false;
    }

    if (activeTemp <= low_critical_value && !lowAlert) {
      lowAlert = true;
    } else if (activeTemp >= (low_critical_value + hysteresis) && lowAlert) {
      lowAlert = false;
    }

    // --- Every update interval
    if (now - lastUpdate >= updateInterval) {
      lastUpdate = now;




     // Compose Line 0
      String line0;
      if (!backlightState) {
        line0 = "H:" + String(low_critical_value, 1);
        line0 += " HH:" + String(high_critical_value, 1);

      } else {
        line0 = "NetW:";
        line0 += networkConnected ? "OK" : "ERR";
        line0 += " DATA:";
        line0 += mqttConnected ? "YES" : "ERR";
      }

      // Compose Line 1: Sensor data
      String line1;
      if (sensor1_stable_ok && ds_stable_ok) {
        line1 = "T:" + String(lastGoodDSTemp, 1) + (char)223 + "C";
        line1 += " H:" + String(lastGoodHumidity1, 1);
      } else if (sensor1_stable_ok) {
        line1 = "T:" + String(lastGoodTemp1, 1) + (char)223 + "C";
        line1 += " H:" + String(lastGoodHumidity1, 1);
      } else if (ds_stable_ok) {
        line1 = "T:" + String(lastGoodDSTemp, 1) + (char)223 + "C";
      } else {
        line1 = "No sensor data   ";  // padded to clear leftovers
      }

      // Update LCD only if content has changed
      if (line0 != lastLine0 || line1 != lastLine1) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(line0);
        lcd.setCursor(0, 1);
        lcd.print(line1);
        lastLine0 = line0;
        lastLine1 = line1;
      }
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}



void displayTask() {
  // Create LCD Task on Core 0
  xTaskCreatePinnedToCore(
    LCDTask,
    "LCD Display Task",
    4096,
    NULL,
    1,
    &DisplayTaskHandle,
    0  // Core 0
  );
}


void lcdBegin() {
  lcd.begin();      // Initialize LCD
  lcd.backlight();  // Turn on backlight
  lcd.clear();      // Optional: Clear display
  lcd.setCursor(0, 0);
  lcd.print("System Starting");
}





void toggleBacklight() {
  unsigned long now = millis();
  const unsigned long interval = 2000;  // 2 seconds

  if (now - lastBlinkTime >= interval) {
    lastBlinkTime = now;
    backlightState = !backlightState;

    if (backlightState) {
      lcd.backlight();
    } else {
      lcd.noBacklight();
    }
  }
}
