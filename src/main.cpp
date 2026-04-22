#include <Arduino.h>
#include "Seeed_Arduino_mmWave.h"

#ifdef ESP32
#include <HardwareSerial.h>
HardwareSerial mmWaveSerial(0);
#else
#define mmWaveSerial Serial1
#endif

SEEED_MR60BHA2 mmWave;

void setup() {
  Serial.begin(115200);
  mmWave.begin(&mmWaveSerial);

  // CSV header (important)
  Serial.println("time,breath,heart,distance");
}

void loop() {
  if (mmWave.update(100)) {

    float breath_rate;
    float heart_rate;
    float distance;

    if (mmWave.getBreathRate(breath_rate) &&
        mmWave.getHeartRate(heart_rate) &&
        mmWave.getDistance(distance)) {

      // ✅ CSV format
      Serial.printf("%lu,%.2f,%.2f,%.2f\n",
                    millis(),
                    breath_rate,
                    heart_rate,
                    distance);
    }
  }
}