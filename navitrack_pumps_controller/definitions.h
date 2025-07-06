#include <ModbusMaster.h>
#include <esp_task_wdt.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_task_wdt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>


#define TINY_GSM_MODEM_SIM800  // ✅ Define your modem model here
#include <TinyGsmClient.h>
#include <StreamDebugger.h>

#include <Wire.h>

bool onStartup = false;


#define USE_GSM
//#define USE_WIFI



#define blinkBoard
//#define powWaterBoard

SoftwareSerial TempHumid(25, 26);

//#define SLAVE_ID_SENSOR1 1
#define SLAVE_ID_SENSOR2 2
//ModbusMaster Sensor1;
ModbusMaster Sensor2;

#define ONE_WIRE_BUS 27  // 13 for blinkBoard
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
float DS_Temperature = 0;
TaskHandle_t Task1;

#define WDT_TIMEOUT 3600
float Temperature1, Temperature2, Humidity1, Humidity2;
float dataArray[20];
unsigned long dataSensor;

bool sensor1_ok = false;
bool ds_ok = false;

float high_critical_value = 50.0;
float low_critical_value = 40.0;


String telemetryPayload = "{}";  // Default JSON string


// --- WiFi Settings
const char* ssid = "GardenWhispers";
const char* password = "blink2023?";

// --- MQTT & Config Settings
WiFiClient espClient;
#ifdef USE_WIFI
PubSubClient mqttClient(espClient);
#endif

// --- Global Struct for Config
struct DeviceSettings {
  char TOKEN[64] = "pumpControllerDemo";
  char SERVER[64] = "telemetry.blinkelectrics.co.ke";
  int port = 1883;
  int telemetryInterval = 60;
};

DeviceSettings deviceSettings;
bool attributesUpdated = false;
const char* config_filename = "/config.json";

// --- Forward Declarations
bool saveConfig(const char* filename);
bool readConfig(const char* filename);
String readFile(const char* filename);
void writeFile(const char* filename, String data);
bool connectWiFi();
bool mqttConnect();
void requestSharedAttributes();
void callback(char* topic, byte* payload, unsigned int length);





#define MODEM_RX 16
#define MODEM_TX 17
#define MODEM_RST 18
#define MODEM_BAUD 115200

#define SerialMon Serial  // For debug output to PC
#define SerialAT Serial2  // Hardware serial for SIM800



// StreamDebugger debugger(SerialAT, SerialMon);
// TinyGsm modem(debugger);  // 👈 Use debugger instead of SerialAT
// TinyGsmClient gsmClient(modem);
// #ifdef USE_GSM
// PubSubClient mqttClient(gsmClient);
// #endif

//#define USE_DEBUGGER

#ifdef USE_DEBUGGER
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);  // 👈 Use debugger
#else
TinyGsm modem(SerialAT);  // 👈 No debugger
#endif

TinyGsmClient gsmClient(modem);

#ifdef USE_GSM
PubSubClient mqttClient(gsmClient);
#endif

const char apn[] = "iot.safaricom.com";  // Safaricom IoT APN
const char gprsUser[] = "";
const char gprsPass[] = "";

volatile bool attributesReceived = false;


void initSettings() {
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS mount failed");
  } else {
    Serial.println("✅ LittleFS mounted");
    if (!readConfig(config_filename)) {
      Serial.println("⚠️ No config found, saving defaults");
      saveConfig(config_filename);
    } else readConfig(config_filename);  // 🔁 Reload from file into deviceSettings
  }
}


#include <LCD_I2C.h>

LCD_I2C lcd(0x27, 16, 2);  // Default address of most PCF8574 modules, change according

// Global Flags and Data
bool networkConnected = false;
bool mqttConnected = false;

TaskHandle_t DisplayTaskHandle;


// Declare these globally or as static inside a function/task
static unsigned long lastBlinkTime = 0;
static bool backlightState = true;

#include <ShiftRegister74HC595.h>

// Example: 2 shift registers = 8 outputs
ShiftRegister74HC595<2> sr(23, 18, 5);  // (dataPin, clockPin, latchPin)
uint8_t outputStates[2] = { 0x00 };     // Persistent buffer: 8 outputs (1 bytes)

uint8_t outputBuffer = 0;                              // Holds output state (bits 0–7)
uint8_t lastTelemetryBuffer = 0;                       // Last sent state
unsigned long lastTelemetrySent = 0;                   // Timestamp of last send
const unsigned long digitalTelemetryInterval = 30000;  // in milliseconds (e.g., 30s)
uint8_t lastDigitalTelemetryBuffer = 0;
unsigned long lastDigitalTelemetrySent = 0;



// === DigitalInputs ===
uint8_t digitalInputBuffer = 0xFF;         // Holds current state of PCF8574 inputs
uint8_t lastDigitalInputBuffer = 0xFF;     // Previous state for change detection
unsigned long lastDigitalTelemetryTime = 0;
const unsigned long digitalInputsTelemetryInterval = 30000;  // in ms
#define PCF8574_ADDR 0x3E

