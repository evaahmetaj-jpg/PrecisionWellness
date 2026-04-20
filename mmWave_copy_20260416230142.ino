#include "Seeed_Arduino_mmWave.h"
#include <HardwareSerial.h>

HardwareSerial mmWaveSerial(1);
SEEED_MR60BHA2 mmWave;

#define RX_PIN 16
#define TX_PIN 17

// Timing
unsigned long lastPrint = 0;

// Averaging
float breathSum = 0;
int breathCount = 0;

float heartSum = 0;
int heartCount = 0;

// Variation tracking
float breathMin = 1000;
float breathMax = 0;

float heartMin = 1000;
float heartMax = 0;

// Presence detection
bool personPresent = false;

void setup() {
  Serial.begin(115200);

  mmWaveSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  mmWave.begin(&mmWaveSerial);

  Serial.println("mmWave ready");
}

void loop() {
  if (mmWave.update(100)) {

    float breath_rate = 0;
    float heart_rate = 0;

    bool detectedThisCycle = false;

    // ---- BREATH ----
    if (mmWave.getBreathRate(breath_rate)) {
      if (breath_rate > 5 && breath_rate < 30) {

        breathSum += breath_rate;
        breathCount++;

        // Track variation
        if (breath_rate < breathMin) breathMin = breath_rate;
        if (breath_rate > breathMax) breathMax = breath_rate;

        detectedThisCycle = true;
      }
    }

    // ---- HEART ----
    if (mmWave.getHeartRate(heart_rate)) {
      if (heart_rate > 40 && heart_rate < 120) {

        heartSum += heart_rate;
        heartCount++;

        // Track variation
        if (heart_rate < heartMin) heartMin = heart_rate;
        if (heart_rate > heartMax) heartMax = heart_rate;

        detectedThisCycle = true;
      }
    }

    // ⚡ FAST PRESENCE DETECTION
    if (detectedThisCycle) {
      personPresent = true;
    }
  }

  // 🧘 SLOW REPORTING (every 30 seconds)
  if (millis() - lastPrint >= 30000) {
    lastPrint = millis();

    Serial.println("----- 30s REPORT -----");

    // ---- Averages ----
    float avgBreath = 0;
    float avgHeart = 0;

    if (breathCount > 0) {
      avgBreath = breathSum / breathCount;
      Serial.print("Avg Breath: ");
      Serial.println(avgBreath);
    } else {
      Serial.println("No valid breath data");
    }

    if (heartCount > 0) {
      avgHeart = heartSum / heartCount;
      Serial.print("Avg Heart: ");
      Serial.println(avgHeart);
    } else {
      Serial.println("No valid heart data");
    }

    // ---- Variation ----
    float breathRange = (breathCount > 0) ? (breathMax - breathMin) : 0;
    float heartRange = (heartCount > 0) ? (heartMax - heartMin) : 0;

    Serial.print("Breath Range: ");
    Serial.println(breathRange);

    Serial.print("Heart Range: ");
    Serial.println(heartRange);

    // ---- Presence ----
    if (personPresent) {
      Serial.println("Person: PRESENT");
    } else {
      Serial.println("Person: NOT PRESENT");
    }

    // ---- Sleep Detection ----
    bool asleep = false;

    if (personPresent && breathCount > 0 && heartCount > 0) {
      if (breathRange < 3 && heartRange < 8) {
        asleep = true;
      }
    }

    if (asleep) {
      Serial.println("State: LIKELY ASLEEP");
    } else {
      Serial.println("State: AWAKE / UNSTABLE");
    }

    Serial.println("----------------------");

    // ---- Reset for next window ----
    breathSum = 0;
    breathCount = 0;

    heartSum = 0;
    heartCount = 0;

    breathMin = 1000;
    breathMax = 0;

    heartMin = 1000;
    heartMax = 0;

    personPresent = false;
  }
}