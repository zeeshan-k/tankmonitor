# ESP32 Water Level Monitor (Blynk IoT + JSN-SR04T)

A robust, Wi-Fi-enabled IoT water level monitoring system built with an ESP32 and a JSN-SR04T waterproof ultrasonic sensor. 

This project is designed for real-world water tanks. It features advanced acoustic noise filtering, spike rejection for turbulent water, rolling averages for stable UI displays, and seamless integration with the Blynk IoT platform for remote mobile monitoring.

## Features
* **Pulse/Echo Operation:** Fully compatible with factory-default JSN-SR04T sensors.
* **Advanced Signal Filtering:** Handles sloshing water, acoustic echoes, and physical interference.
* **Blynk IoT Integration:** Live mobile dashboard with percentage, filled capacity, and Wi-Fi health.
* **Smart Rate Limiting:** Serial monitor updates every 5 seconds, while Blynk cloud updates every 60 seconds to protect free-tier message quotas.
* **Graceful Error Handling:** System survives temporary sensor glitches without flashing immediate errors.

---

## Hardware Requirements
* **Microcontroller:** ESP32 Development Board (e.g., DevKit V1).
* **Sensor:** JSN-SR04T Waterproof Ultrasonic Distance Sensor.
* **Power Supply:** 5V / 2A USB wall charger (Computer USB ports may not provide enough current for the sensor).

## Wiring Diagram
*Note: The JSN-SR04T must be powered from the ESP32's `VIN` pin (5V), NOT the 3.3V pin or the `VN` data pin.*

| JSN-SR04T Pin | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **5V** | **VIN** | 5V Power Supply |
| **GND** | **GND** | Ground |
| **RX** | **GPIO 17 (TX2)** | Trigger (Receives start pulse) |
| **TX** | **GPIO 16 (RX2)** | Echo (Sends distance pulse) |

---

## How the Script Works (Core Logic)

Water tanks are hostile environments for ultrasonic sensors. Condensation, sloshing water, splashing from inlet pipes, and sound waves bouncing off tank walls create highly erratic raw data. This script uses a 4-stage processing pipeline every 5 seconds to guarantee stable, accurate readings.

### 1. Raw Batch Sampling
Instead of taking one reading, the ESP32 fires **9 consecutive ultrasonic pulses** spaced 150ms apart. The 150ms delay acts as "acoustic padding," allowing stray sound waves to dissipate before the next ping.

### 2. Outlier Filtering (Trimmed Mean)
The script takes those 9 readings, sorts them from lowest to highest distance, and **trims (deletes) the top 2 and bottom 2 values**. 
* Deleting the lowest values removes false "short" echoes (caused by mist or condensation).
* Deleting the highest values removes false "deep" echoes (where the sound bounced off a wall instead of the water).
The remaining 5 middle values are averaged to create a single clean reading.

### 3. Spike Rejection Logic
Even with a trimmed mean, a physical disturbance (like a frog crossing the beam, or a massive splash) can ruin a reading. 
The script compares the new reading to the `lastValidDistance`. If the water level suddenly jumped by more than **30 cm in 5 seconds**, it is physically impossible for the tank to drain or fill that fast. The system categorizes this as a "Spike", rejects the reading, and holds the display steady using the last known good value.

### 4. Rolling Average Smoothing
To prevent the UI gauge from flickering back and forth by 1-2 percent due to tiny surface ripples, the script stores the last 4 valid, processed readings in a buffer. It outputs the average of this buffer, resulting in a perfectly smooth, slow-moving data readout.

---

# Configuration Parameters & Variables

This document explains the core parameters and state variables used in the ESP32 Water Level Monitor script. These values control the physical dimensions of the tank, the timing of network updates, and the mathematical filters used to eliminate noise and water turbulence.

## 1. Tank Configuration
Defines the physical boundaries of your water tank to accurately calculate percentages and volumes.

*   `TANK_EMPTY_CM` (200.0): The exact physical distance (in centimeters) from the face of the ultrasonic sensor to the very bottom of the tank. This represents 0% capacity.
*   `TANK_FULL_CM` (10.0): The distance (in centimeters) from the sensor to the highest allowed water level. This acts as a dead-zone or gap at the top to prevent the sensor from getting submerged. This represents 100% capacity.
*   *Note: The usable water depth is calculated automatically (Empty - Full).*

## 2. Display & Timing
Controls how frequently the system measures the water and transmits data.

*   `REFRESH_INTERVAL_MS` (5000): The time in milliseconds (5 seconds) between active measurement cycles. The Serial Monitor UI updates at this rate.
*   `BLYNK_UPDATE_MS` (60000): The time in milliseconds (60 seconds) between data pushes to the Blynk Cloud. This slower rate prevents the ESP32 from hitting Blynk's message rate limits while keeping the local Serial Monitor fast.

## 3. Measurement Quality
Controls the physical acoustic behavior of the ultrasonic sensor.

*   `NUM_SAMPLES` (9): The number of individual acoustic pings the sensor fires per measurement cycle. Taking multiple samples allows the system to filter out bad echoes.
*   `SAMPLE_DELAY_MS` (150): The pause (in milliseconds) between each of the 9 pings. This acts as "acoustic padding," giving scattered sound waves from the previous ping time to dissipate before firing the next one.
*   `ECHO_TIMEOUT_US` (40000): The maximum time (in microseconds) the sensor will wait for an echo before giving up. 40,000µs allows a maximum reading distance of roughly 6.8 meters.

## 4. Signal Filtering & Smoothing
These parameters configure the mathematical filters that clean the raw sensor data, ignoring sloshing water, splashing, and false echoes.

*   `OUTLIER_TRIM` (2): Used in the "Trimmed Mean" filter. After taking the 9 samples and sorting them by distance, the system discards the top 2 and bottom 2 values. This completely eliminates random high spikes (splashes) and low spikes (sensor glitches) before averaging the remaining 5 reliable middle values.
*   `MAX_CHANGE_CM` (30.0): The Spike Rejection threshold. If a new reading differs from the last valid reading by more than 30cm in just 5 seconds, the system assumes it is an anomaly (like an object moving past the sensor) and rejects it.
*   `ROLLING_WINDOW` (4): The number of consecutive measurement cycles used to calculate the rolling average. A window of 4 means the displayed water level is the average of the last 4 valid cycles, resulting in a smooth, steady UI that doesn't flicker when the water surface ripples.

## 5. Error Handling
*   `ERROR_THRESHOLD` (3): The system acts as a shock absorber for errors. It will quietly ignore up to 2 failed measurement cycles in a row (displaying the last known good reading). If it fails 3 times consecutively, it officially triggers a "SENSOR ERROR" state and pushes the error to Blynk.

---

## Internal State Variables
These variables are used internally by the script to track data across loops. **Do not modify these.**

*   `lastValidDistance`: Stores the last successful reading in case the next reading is a rejected spike or a temporary error.
*   `consecutiveErrors`: A counter tracking how many times the sensor has failed in a row.
*   `rollingBuffer[ROLLING_WINDOW]`: An array storing the history of recent measurements for the rolling average math.
*   `rollingIndex` & `rollingCount`: Trackers managing the placement of new data into the rolling buffer.
*   `lastBlynkPush`: Stores the exact timestamp (via `millis()`) of the last successful Blynk upload to manage the 60-second rate limit.

---

## Blynk Web Dashboard Setup (Free Tier)

Set up your template Datastreams as follows:

| Name | Virtual Pin | Data Type | Min / Max | Units | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Water Percentage** | `V0` | Integer | 0 to 100 | `%` | Bind to Gauge/Level widgets |
| **Filled Level** | `V1` | Double | 0 to (Empty - Full) | `cm` | Bind to Labeled Value widget |
| **Status** | `V2` | String | - | - | Shows FULL, LOW, ERROR |
| **Wi-Fi Status** | `V3` | String | - | - | Shows connection and RSSI strength |

*Ensure your ESP32 is connected to a 2.4GHz Wi-Fi network, as 5GHz is not supported by the hardware.*
