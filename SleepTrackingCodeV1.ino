#include "Seeed_Arduino_mmWave.h"
#include <HardwareSerial.h>

HardwareSerial mmWaveSerial(1);
SEEED_MR60BHA2 mmWave;

#define RX_PIN 16
#define TX_PIN 17

// ── Warmup ───────────────────────────────────────────────────────────
#define WARMUP_MS 15000UL
bool warmedUp = false;

// ── Timing ───────────────────────────────────────────────────────────
unsigned long lastPrint         = 0;
unsigned long trackingStartTime = 0;

// ── Presence ─────────────────────────────────────────────────────────
#define PRESENCE_TIMEOUT_MS 10000UL
unsigned long lastDetectionTime = 0;
int  presenceStableCount = 0;
int  absenceCount        = 0;
bool trackingActive      = false;

// ── Averaging accumulators ───────────────────────────────────────────
float breathSum = 0;
int   breathCount = 0;
float heartSum = 0;
int   heartCount = 0;

// ── Spike counters ───────────────────────────────────────────────────
int breathSpikeCount = 0;
int heartSpikeCount  = 0;

// ── Variation tracking ───────────────────────────────────────────────
float breathMin = 1000;
float breathMax = 0;
float heartMin  = 1000;
float heartMax  = 0;

// ── Spike anchors ────────────────────────────────────────────────────
float lastValidBreath = 0;
float lastValidHeart  = 0;

// ── Sleep state ───────────────────────────────────────────────────────
int sleepWindowCount = 0;

// ─────────────────────────────────────────────────────────────────────
void resetWindowAccumulators() {
  breathSum        = 0;
  breathCount      = 0;
  breathSpikeCount = 0;
  heartSum         = 0;
  heartCount       = 0;
  heartSpikeCount  = 0;
  breathMin = 1000;
  breathMax = 0;
  heartMin  = 1000;
  heartMax  = 0;
}

void resetSessionState() {
  resetWindowAccumulators();
  lastDetectionTime = 0;
  sleepWindowCount  = 0;
  lastValidBreath   = 0;
  lastValidHeart    = 0;
}

// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  mmWaveSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  mmWave.begin(&mmWaveSerial);

  resetSessionState();

  Serial.println("time_ms,person_present,state,breath_bpm,heart_bpm,breath_range,heart_range,breath_samples,heart_samples,breath_spikes,heart_spikes");
}

// ─────────────────────────────────────────────────────────────────────
void loop() {

  // ── Warmup guard ─────────────────────────────────────────────────
  if (!warmedUp) {
    if (millis() < WARMUP_MS) {
      mmWave.update(100);
      return;
    }
    warmedUp = true;
    resetSessionState();
    lastPrint = millis();
    Serial.println("# Warmup complete - sensor ready");
  }

  // ── Poll sensor ───────────────────────────────────────────────────
  if (mmWave.update(100)) {
    float breath_rate = 0;
    float heart_rate  = 0;
    bool  detected    = false;

    // ── BREATH ────────────────────────────────────────────────────
    if (mmWave.getBreathRate(breath_rate)) {
      if (breath_rate > 5 && breath_rate < 30) {
        bool isSpike = (lastValidBreath > 0) &&
                       (abs(breath_rate - lastValidBreath) / lastValidBreath >= 0.40f);

        breathSum += breath_rate;
        breathCount++;
        if (breath_rate < breathMin) breathMin = breath_rate;
        if (breath_rate > breathMax) breathMax = breath_rate;
        detected = true;

        if (isSpike) {
          breathSpikeCount++;
          // Hold anchor at last stable value — do not update on spike
        } else {
          lastValidBreath = breath_rate;
        }
      }
    }

    // ── HEART ─────────────────────────────────────────────────────
    if (mmWave.getHeartRate(heart_rate)) {
      if (heart_rate > 40 && heart_rate < 120) {
        bool isSpike = (lastValidHeart > 0) &&
                       (abs(heart_rate - lastValidHeart) / lastValidHeart >= 0.30f);

        heartSum += heart_rate;
        heartCount++;
        if (heart_rate < heartMin) heartMin = heart_rate;
        if (heart_rate > heartMax) heartMax = heart_rate;
        detected = true;

        if (isSpike) {
          heartSpikeCount++;
          // Hold anchor at last stable value — do not update on spike
        } else {
          lastValidHeart = heart_rate;
        }
      }
    }

    if (detected) lastDetectionTime = millis();
  }

  // ── 30-second window ─────────────────────────────────────────────
  if (millis() - lastPrint >= 30000) {
    lastPrint = millis();

    bool personPresent = (lastDetectionTime > 0 &&
                          millis() - lastDetectionTime < PRESENCE_TIMEOUT_MS);

    float avgBreath   = (breathCount > 0) ? breathSum / breathCount : 0;
    float avgHeart    = (heartCount  > 0) ? heartSum  / heartCount  : 0;
    float breathRange = (breathCount > 0) ? breathMax - breathMin   : 0;
    float heartRange  = (heartCount  > 0) ? heartMax  - heartMin    : 0;

    // ── Session start ────────────────────────────────────────────
    if (personPresent && breathCount > 10) {
      presenceStableCount++;
    } else {
      presenceStableCount = 0;
    }

    // Debug line — '#' prefix is ignored by the Python logger
    Serial.print("# DBG present:");
    Serial.print(personPresent);
    Serial.print(" breath_n:");
    Serial.print(breathCount);
    Serial.print(" stable:");
    Serial.print(presenceStableCount);
    Serial.print(" absence:");
    Serial.println(absenceCount);

    if (!trackingActive && presenceStableCount >= 2) {
      trackingActive    = true;
      trackingStartTime = millis();
      Serial.println("# TRACKING_STARTED");
      resetSessionState();
      resetWindowAccumulators();
    }

    // ── Session stop ─────────────────────────────────────────────
    if (!personPresent) {
      absenceCount++;
    } else {
      absenceCount = 0;
    }

    if (trackingActive && absenceCount >= 2) {
      trackingActive = false;
      Serial.println("# TRACKING_STOPPED");
    }

    // ── Sleep state ───────────────────────────────────────────────
    int cleanBreath = breathCount - breathSpikeCount;
    int cleanHeart  = heartCount  - heartSpikeCount;

    String state = "AWAKE";

    if (!personPresent) {
      state = "NOT_PRESENT";
      sleepWindowCount = 0;
    } else {
      bool windowQualifies = (cleanBreath >= 20)
                          && (cleanHeart  >= 20)
                          && (avgBreath   >= 8.0 && avgBreath <= 20.0)
                          && (breathRange <  3.0)
                          && (heartRange  <  8.0);

      if (windowQualifies) {
        sleepWindowCount++;
      } else {
        sleepWindowCount = 0;
      }

      if (sleepWindowCount >= 10) {
        state = "LIKELY_ASLEEP";
      } else {
        state = "AWAKE";
      }
    }

    // ── Emit CSV row (only when tracking active) ──────────────────
    if (trackingActive) {
      Serial.print(millis() - trackingStartTime);
      Serial.print(",");
      Serial.print(personPresent ? 1 : 0);
      Serial.print(",");
      Serial.print(state);
      Serial.print(",");
      Serial.print(avgBreath, 2);
      Serial.print(",");
      Serial.print(avgHeart, 2);
      Serial.print(",");
      Serial.print(breathRange, 2);
      Serial.print(",");
      Serial.print(heartRange, 2);
      Serial.print(",");
      Serial.print(breathCount);
      Serial.print(",");
      Serial.print(heartCount);
      Serial.print(",");
      Serial.print(breathSpikeCount);
      Serial.print(",");
      Serial.println(heartSpikeCount);
    }

    // ── Reset window accumulators only ───────────────────────────
    // sleepWindowCount and lastValid* persist across windows intentionally
    resetWindowAccumulators();
  }
}
