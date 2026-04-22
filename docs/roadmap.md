# Project Roadmap: Precision Wellness

## 1. Overview & Goal

This system uses:
mmWave sensor → breathing, heart rate, movement
environmental sensor → temperature, humidity, pressure

Goal: Convert raw sensor data into meaningful sleep insights.

## 2. Data Format

Current output (ESP32 → CSV):
time_ms, breath_rate, heart_rate, distance

Future output:
time_ms, breath_rate, heart_rate, distance, temperature, humidity, pressure

## 3. Core Pipeline

Stage 1 — Data Collection (Embedded)
Input: mmWave sensor data
Output: CSV stream over serial
Status: ✅ Implemented

Stage 2 — Data Logging
Goal: Save overnight data to file

Tasks:
Stream serial data → CSV file
Ensure no dropped lines
Handle disconnections

Stage 3 - Data Cleaning
Functions to implement (Python):

clean_data(data):
remove invalid values (NaN, zeros)
handle missing timestamps
normalize sampling rate
smooth_signal(signal):
apply moving average or low-pass filter

Stage 4 – Feature Extraction
Functions:
compute_breathing_features(data):
    average breathing rate
    variability
    detect irregular breathing
compute_heart_features(data):
    resting heart rate
    HR trends
    HR variability (if possible)
compute_motion_features(data):
    detect movement spikes
    quantify restlessness

Stage 5 – Sleep Detection
Goal: Classify sleep into states

States:
    AWAKE
    LIGHT
    DEEP

Function:
classify_sleep(data):
    use thresholds + heuristics:
    high movement → AWAKE
    low movement + stable breathing → DEEP
    intermediate → LIGHT

Stage 6 – Scoring System
Functions:
compute_sleep_score(data):
combine:
    total sleep time
    restlessness
    heart rate stability
compute_readiness_score(data):
    compares current night to baseline
    detect deviations

Stage 7 – Insight Generation (Sleep + Environment)
Goal: Generate meaningful insights by combining physiological data and environmental conditions.
Inputs
    Sleep data:
        sleep_score
        time_in_deep_sleep
        restlessness
        heart rate
    Environmental data:
        temperature
        humidity
        pressure

Core Functions
aggregate_nightly_data(data):
    reduce raw time-series into nightly summaries:
        avg_temperature
        avg_humidity
        sleep_score
        total_sleep_time

analyze_environmental_correlation(data):
    compute relationships between:
        temperature vs sleep_score
        humidity vs sleep quality

find_optimal_conditions(data):
    group by environmental ranges (binning)
    compute average sleep_score per range
    identify optimal values
Example output:
    "Optimal sleep temperature: 68–72°F"

detect_patterns(data):
    detect trends such as:
        high humidity → worse sleep
        temperature swings → more awakenings

generate_insights(data):
    convert patterns into human-readable outputs
Examples:
    "You sleep best at ~70°F"
    "Higher humidity is associated with lighter sleep"
    "Your sleep quality drops when room temperature exceeds 75°F"

Output
{
"sleep_score": 82,
"readiness_score": 76,
"optimal_temperature": 70,
"optimal_range": [68, 72],
"insights": [
"You sleep best at ~70°F",
"High humidity reduces deep sleep"
]
}

Constraints
Requires multi-night data
Must handle noisy signals
Avoid false correlations with small datasets