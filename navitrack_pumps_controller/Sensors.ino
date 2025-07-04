

/*
  ------------------------------------------------------------------------------
  setupSensors() – Sensor Initialization and Task Startup
  ------------------------------------------------------------------------------

  This function initializes:
  - The ESP32 watchdog timer to ensure the system resets on hangs
  - The serial interface for Modbus communication (Sensor2)
  - DS18B20 temperature sensor
  - A FreeRTOS task to continuously read sensor data (`SensorReading`)

  ------------------------------------------------------------------------------
  Watchdog Timer (WDT):
  - Uses `esp_task_wdt_init(WDT_TIMEOUT, true)` to initialize the WDT
    - `WDT_TIMEOUT` should be defined globally (in seconds)
    - Second argument enables panic and automatic restart on timeout
  - Adds the current task to the WDT with `esp_task_wdt_add(NULL)`

  ------------------------------------------------------------------------------
  Sensors:
  - `Sensor2` (Modbus sensor):
    - Communicates over `TempHumid` software serial at 9600 baud
    - Initialized with `SLAVE_ID_SENSOR2`
  - `ds18b20` (DallasTemperature object):
    - One-wire digital temperature sensor (e.g., DS18B20)

  ------------------------------------------------------------------------------
  Task Creation:
  - Creates and pins the `SensorReading` task to core 0
  - Stack size: 30,000 words
  - Priority: 0 (lowest; can be adjusted depending on importance)
  - Task handle: `Task1`

  ------------------------------------------------------------------------------
*/


void setupSensors() {
  esp_task_wdt_init(WDT_TIMEOUT, true);  // Enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);


  TempHumid.begin(9600, SWSERIAL_8N1);

  //Sensor1.begin(SLAVE_ID_SENSOR1, TempHumid);
  Sensor2.begin(SLAVE_ID_SENSOR2, TempHumid);
  ds18b20.begin();

  xTaskCreatePinnedToCore(
    SensorReading, /* Function to implement the task */
    "Task1",       /* Name of the task */
    30000,         /* Stack size in words */
    NULL,          /* Task input parameter */
    0,             /* Priority of the task */
    &Task1,        /* Task handle. */
    0);
}



/*
  ------------------------------------------------------------------------------
  SensorReading Task – ESP32 Environmental Telemetry
  ------------------------------------------------------------------------------

  This FreeRTOS task reads environmental data from two sensors:
  1. A Modbus temperature & humidity sensor (Sensor2)
  2. A DS18B20 digital temperature sensor

  It constructs a JSON telemetry payload and stores it in a global variable
  `telemetryPayload` to be used by the publishing task (e.g., MQTT or HTTP).

  ------------------------------------------------------------------------------
  Dependencies:
  - ModbusMaster library (Sensor2)
  - OneWire & DallasTemperature libraries (DS18B20)
  - ArduinoJson (JSON formatting)
  ------------------------------------------------------------------------------

  JSON Telemetry Format (only includes available/valid sensor data):

  {
    "temp1": "23.4",      // Temperature from Modbus sensor
    "humidity1": "56.7",  // Humidity from Modbus sensor
    "ds_temp": "22.9"     // Temperature from DS18B20
  }

  ------------------------------------------------------------------------------
  Fallback Behavior:
  - If any sensor fails, its value is set to -100 and excluded from JSON payload.
  - Reading interval is controlled by: deviceSettings.telemetryInterval (in sec)
  ------------------------------------------------------------------------------

  Task Creation Example:
  xTaskCreate(SensorReading, "Sensor Reading", 4096, NULL, 1, NULL);
*/


void SensorReading(void *pvParameters) {
  for (;;) {
 sensor1_ok = false;
 ds_ok = false;

    // --- Read Sensor2 (Modbus humidity & temperature)
    uint8_t Reg = Sensor2.readHoldingRegisters(0, 2);

    if (Reg == Sensor2.ku8MBSuccess) {
      for (int i = 0; i < 2; i++) {
        dataSensor = Sensor2.getResponseBuffer(i);
        dataArray[i] = dataSensor;
      }
      Temperature1 = dataArray[1] * 0.1;
      Humidity1 = dataArray[0] * 0.1;
      sensor1_ok = true;
    } else {
      Temperature1 = -100;
      Humidity1 = -100;
    }

    // --- Read DS18B20
    ds18b20.requestTemperatures();
    DS_Temperature = ds18b20.getTempCByIndex(0);
    if (DS_Temperature != DEVICE_DISCONNECTED_C) {
      ds_ok = true;
    } else {
      DS_Temperature = -100;
    }

    StaticJsonDocument<128> doc;

    if (sensor1_ok) {
      doc["temp1"] = String(Temperature1, 1);  // formatted to 1 decimal
      doc["humidity1"] = String(Humidity1, 1);
    }

    if (ds_ok) {
      doc["ds_temp"] = String(DS_Temperature, 1);
    }


    serializeJson(doc, telemetryPayload); 
    Serial.println("📤 Telemetry updated: " + telemetryPayload);

    vTaskDelay(pdMS_TO_TICKS(deviceSettings.telemetryInterval * 100));
  }
}
