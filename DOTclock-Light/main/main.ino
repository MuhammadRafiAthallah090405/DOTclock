// main.ino
#include <Arduino.h>
#include "rtc_eeprom.h"   // deklarasi extern rtc, fungsi EEPROM/RTC
#include "webpage.h"      // PROGMEM web assets (tidak wajib di main but ok)
#include <EEPROM.h>       // kami gunakan EEPROM.begin jika perlu

// === Pin definitions (opsi B: #define) ===
#define DHTPIN        2
#define ALARM_OUT_PIN D5

// Global sensor state (definisi nyata)
float g_lastTemp = NAN;
float g_lastHum = NAN;
unsigned long g_lastSensorMillis     = 0;
const unsigned long SENSOR_INTERVAL  = 5UL * 1000UL; // 30s
unsigned long lastDisplay            = 0;
const unsigned long DISPLAY_INTERVAL = 2000; // 2 detik

// Forward declarations (implemented in other tabs)
void startAP();
void initServer();
void handleClientLoop();
void readSensorOnce();
void checkAlarmsLoop();
void setupOTA();
void handleOTA();
void initRTCAndLoadAlarms();

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(ALARM_OUT_PIN, OUTPUT);
  digitalWrite(ALARM_OUT_PIN, LOW);

  // Start AP
  startAP();

  // init sensor once
  readSensorOnce();

  // init RTC + EEPROM (in rtc_eeprom.ino)
  initRTCAndLoadAlarms();

  // setup OTA (after WiFi started)
  setupOTA();

  // start webserver
  initServer();

  Serial.println("Setup complete.");
}

void loop() {
  // sensor update non-blocking
  unsigned long nowMs = millis();
  if (nowMs - g_lastSensorMillis >= SENSOR_INTERVAL) {
    g_lastSensorMillis = nowMs;
    readSensorOnce();
  }

  if (millis() - lastDisplay >= 2000) {
    lastDisplay = millis();
    printDateTime();
  }

  // check alarms
  checkAlarmsLoop();

  // handle http clients
  handleClientLoop();

  // handle OTA
  handleOTA();

  delay(10);
}
