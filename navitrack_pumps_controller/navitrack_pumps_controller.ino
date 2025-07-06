#include "definitions.h"

void initWatchdog(int timeoutSeconds = 600) {
  // Initialize the Task Watchdog Timer
  esp_task_wdt_init(timeoutSeconds, true);  // timeout, panic on trigger
  Serial.println("🛡️ Watchdog initialized");

  // Add the tasks you want to monitor
  esp_task_wdt_add(NULL);  // Add current task (usually loop or setup task)
}


void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Debug Init");
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  initWatchdog();
  lcdBegin();
  initialiseOutputs();
  
  displayTask();

  initSettings();

#ifdef USE_WIFI
  while (!connectWiFi())
    ;
  mqttConnect();
#endif

#ifdef USE_GSM
  maintainGSMConnectivity();
#endif
  hourlyResetSetup();
 
}

void loop() {
  telemetryLoop();
  esp_task_wdt_reset();  // Feed the watchdog
}
