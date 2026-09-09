// ─────────────────────────────────────────────
// ESP32 Water Level Monitor
// Sensor: JSN-SR04T UART version (5V RX TX GND)
// Platform: Blynk IoT
// ─────────────────────────────────────────────

#define BLYNK_TEMPLATE_ID     "TMPLxxxxxxxx"      // ← Replace with yours
#define BLYNK_TEMPLATE_NAME   "Water Tank"
#define BLYNK_AUTH_TOKEN      "your_auth_token"   // ← Replace with yours

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// ── WiFi Credentials ──────────────────────────
const char* WIFI_SSID     = "your_wifi_name";     // ← Replace
const char* WIFI_PASSWORD = "your_wifi_password"; // ← Replace

// ── UART Pin Configuration ────────────────────
// ESP32 UART2
const int RX_PIN = 16;   // Connect to sensor TX
const int TX_PIN = 17;   // Connect to sensor RX

// ── Tank Configuration ────────────────────────
// Measure these values once installed on your tank
float TANK_DEPTH_CM   = 400.0;  // Distance (cm) from sensor to tank bottom ← Adjust
float MIN_DISTANCE_CM = 10.0;   // Distance (cm) when tank is completely full ← Adjust

// ── Measurement Settings ──────────────────────
const int NUM_SAMPLES      = 5;     // Readings to average
const int MEASURE_INTERVAL = 5000;  // Milliseconds between measurements
const int SENSOR_BAUD      = 9600;  // JSN-SR04T UART baud rate

// ── Blynk Virtual Pins ────────────────────────
#define VPIN_DISTANCE   V0
#define VPIN_PERCENTAGE V1
#define VPIN_STATUS     V2

// ── UART Instance ─────────────────────────────
HardwareSerial sensorSerial(2);  // Use ESP32 UART2

BlynkTimer timer;

// ─────────────────────────────────────────────
// Send measurement request to sensor
// ─────────────────────────────────────────────
void requestMeasurement() {
  sensorSerial.write(0x01);  // Command byte to trigger measurement
}

// ─────────────────────────────────────────────
// Read and parse response from sensor
// Returns distance in CM, -1 if error
// ─────────────────────────────────────────────
float readDistance() {
  // Wait for 4 bytes response (max 100ms timeout)
  unsigned long timeout = millis() + 100;
  while (sensorSerial.available() < 4) {
    if (millis() > timeout) {
      Serial.println("TIMEOUT: No response from sensor");
      return -1.0;
    }
    delay(1);
  }

  // Read 4 bytes
  byte b1 = sensorSerial.read();  // Header - should be 0xFF
  byte b2 = sensorSerial.read();  // Distance high byte
  byte b3 = sensorSerial.read();  // Distance low byte
  byte b4 = sensorSerial.read();  // Checksum

  // Validate header
  if (b1 != 0xFF) {
    Serial.println("ERROR: Invalid header byte");
    sensorSerial.flush();
    return -1.0;
  }

  // Validate checksum
  byte checksum = (b1 + b2 + b3) & 0xFF;
  if (checksum != b4) {
    Serial.println("ERROR: Checksum mismatch");
    return -1.0;
  }

  // Calculate distance
  int distanceMM = (b2 << 8) + b3;
  float distanceCM = distanceMM / 10.0;

  // Sanity check - JSN-SR04T range is 20cm to 600cm
  if (distanceCM < 20.0 || distanceCM > 600.0) {
    Serial.print("ERROR: Out of range reading: ");
    Serial.println(distanceCM);
    return -1.0;
  }

  return distanceCM;
}

// ─────────────────────────────────────────────
// Take multiple readings and return average
// ─────────────────────────────────────────────
float measureDistance() {
  float readings[NUM_SAMPLES];
  int validCount = 0;

  // Clear any leftover data in buffer
  while (sensorSerial.available()) sensorSerial.read();

  for (int i = 0; i < NUM_SAMPLES; i++) {
    requestMeasurement();
    delay(200);  // Wait for sensor to respond

    float reading = readDistance();
    if (reading > 0) {
      readings[validCount++] = reading;
      Serial.print("  Sample ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(reading);
      Serial.println(" cm");
    }
    delay(100);
  }

  if (validCount == 0) {
    Serial.println("ERROR: No valid readings");
    return -1.0;
  }

  // Sort readings (bubble sort) for median
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = 0; j < validCount - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        float temp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = temp;
      }
    }
  }

  // Return median for better accuracy
  return readings[validCount / 2];
}

// ─────────────────────────────────────────────
// Calculate water level percentage
// ─────────────────────────────────────────────
float calculatePercentage(float distanceCm) {
  float usableDepth = TANK_DEPTH_CM - MIN_DISTANCE_CM;
  float waterDepth  = TANK_DEPTH_CM - distanceCm;
  float percentage  = (waterDepth / usableDepth) * 100.0;
  return constrain(percentage, 0.0, 100.0);
}

// ─────────────────────────────────────────────
// Get status string based on percentage
// ─────────────────────────────────────────────
String getStatus(float percentage) {
  if (percentage >= 90.0) return "FULL";
  if (percentage >= 60.0) return "HIGH";
  if (percentage >= 40.0) return "MEDIUM";
  if (percentage >= 20.0) return "LOW";
  if (percentage >= 5.0)  return "VERY LOW";
  return "EMPTY";
}

// ─────────────────────────────────────────────
// Main function - measure and send to Blynk
// ─────────────────────────────────────────────
void sendWaterLevel() {
  Serial.println("─────────────────────────────");
  Serial.println("Taking measurement...");

  float distance   = measureDistance();

  if (distance < 0) {
    Serial.println("Measurement failed - skipping");
    Blynk.virtualWrite(VPIN_STATUS, "SENSOR ERROR");
    return;
  }

  float percentage = calculatePercentage(distance);
  String status    = getStatus(percentage);

  // Print to serial
  Serial.print("Distance:    "); Serial.print(distance);   Serial.println(" cm");
  Serial.print("Water Level: "); Serial.print(percentage); Serial.println(" %");
  Serial.print("Status:      "); Serial.println(status);

  // Send to Blynk
  Blynk.virtualWrite(VPIN_DISTANCE,   distance);
  Blynk.virtualWrite(VPIN_PERCENTAGE, percentage);
  Blynk.virtualWrite(VPIN_STATUS,     status);
}

// ─────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────
void setup() {
  // Debug serial
  Serial.begin(115200);
  Serial.println("ESP32 Water Level Monitor Starting...");

  // Sensor serial
  sensorSerial.begin(SENSOR_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("Sensor UART initialized");

  // Connect to Blynk
  Serial.println("Connecting to Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Blynk connected!");

  // Timer
  timer.setInterval(MEASURE_INTERVAL, sendWaterLevel);

  Serial.println("Setup complete - first reading in 5 seconds");
}

// ─────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────
void loop() {
  Blynk.run();
  timer.run();
}
