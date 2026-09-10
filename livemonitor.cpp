// ═════════════════════════════════════════════════════════════════
//        ESP32 WATER LEVEL MONITOR - JSN-SR04T (Pulse/Echo)
// ═════════════════════════════════════════════════════════════════

// ── PIN CONFIGURATION ─────────────────────────────────────────────
const int TRIG_PIN = 17;        // Trigger pin → connect to sensor TRIG
const int ECHO_PIN = 16;        // Echo pin    → connect to sensor ECHO

// ── TANK CONFIGURATION ────────────────────────────────────────────
// Measure these values physically before deploying

// Distance (cm) from sensor down to tank floor when tank is EMPTY
// Example: sensor is at top, tank floor is 400cm below
const float TANK_EMPTY_CM = 400.0;

// Distance (cm) from sensor down to water surface when tank is FULL
// Water does not rise all the way to sensor - measure this gap
// Example: even when full, water surface is 150cm below sensor
const float TANK_FULL_CM  = 150.0;

// ── DISPLAY / TIMING ──────────────────────────────────────────────
const int REFRESH_INTERVAL_MS = 5000;   // Screen refresh in milliseconds
                                        // 5000 = every 5 seconds

// ── MEASUREMENT QUALITY ───────────────────────────────────────────
const int NUM_SAMPLES      = 5;         // Number of readings to take per cycle
                                        // More = more accurate but slower
                                        // Recommended: 3 to 7

const int SAMPLE_DELAY_MS  = 150;       // Delay between samples in milliseconds
                                        // Allows sound echo to dissipate
                                        // Do not go below 100ms

const int ECHO_TIMEOUT_US  = 40000;     // Echo timeout in microseconds
                                        // 40000us = ~6.8 metres max range
                                        // Increase if getting false timeouts

// ── ERROR HANDLING ────────────────────────────────────────────────
const int ERROR_THRESHOLD  = 3;         // How many consecutive failures before
                                        // showing SENSOR ERROR screen
                                        // Until then, last good reading is shown

// ═════════════════════════════════════════════════════════════════
//   DO NOT MODIFY BELOW UNLESS YOU KNOW WHAT YOU ARE DOING
// ═════════════════════════════════════════════════════════════════

float lastValidDistance  = -1.0;
int   consecutiveErrors  = 0;

// ─────────────────────────────────────────────
// Single pulse/echo reading
// Returns distance in CM or -1 on error
// ─────────────────────────────────────────────
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(20);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);

  if (duration == 0) return -1.0;

  float distanceCM = (duration * 0.0343) / 2.0;

  // JSN-SR04T valid range is 20cm to 600cm
  if (distanceCM < 20.0 || distanceCM > 600.0) return -1.0;

  return distanceCM;
}

// ─────────────────────────────────────────────
// Take multiple samples, return median
// ─────────────────────────────────────────────
float measureDistance() {
  float readings[NUM_SAMPLES];
  int validCount = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    float r = readDistance();
    if (r > 0) readings[validCount++] = r;
    delay(SAMPLE_DELAY_MS);
  }

  if (validCount == 0) return -1.0;

  // Bubble sort for median
  for (int i = 0; i < validCount - 1; i++)
    for (int j = 0; j < validCount - i - 1; j++)
      if (readings[j] > readings[j + 1]) {
        float t      = readings[j];
        readings[j]  = readings[j + 1];
        readings[j + 1] = t;
      }

  return readings[validCount / 2];
}

// ─────────────────────────────────────────────
// Calculate water level percentage
// 0%   = tank empty (sensor reads TANK_EMPTY_CM)
// 100% = tank full  (sensor reads TANK_FULL_CM)
// ─────────────────────────────────────────────
float calculatePercentage(float distanceCm) {
  float usableDepth = TANK_EMPTY_CM - TANK_FULL_CM;
  float waterDepth  = TANK_EMPTY_CM - distanceCm;
  return constrain((waterDepth / usableDepth) * 100.0, 0.0, 100.0);
}

// ─────────────────────────────────────────────
// Status label based on percentage
// ─────────────────────────────────────────────
String getStatus(float pct) {
  if (pct >= 90.0) return "FULL     ";
  if (pct >= 60.0) return "HIGH     ";
  if (pct >= 40.0) return "MEDIUM   ";
  if (pct >= 20.0) return "LOW      ";
  if (pct >= 5.0)  return "VERY LOW ";
  return                   "EMPTY    ";
}

// ─────────────────────────────────────────────
// ASCII progress bar  [########------------]
// ─────────────────────────────────────────────
String drawBar(float pct) {
  const int BAR_WIDTH = 20;
  int filled = (int)((pct / 100.0) * BAR_WIDTH);
  String bar = "[";
  for (int i = 0; i < BAR_WIDTH; i++)
    bar += (i < filled) ? "#" : "-";
  bar += "]";
  return bar;
}

// ─────────────────────────────────────────────
// Main display - clears screen and redraws
// ─────────────────────────────────────────────
void printDisplay(float distance, float pct, bool isStale) {
  String status = getStatus(pct);
  String bar    = drawBar(pct);

  Serial.print("\033[2J\033[H");
  Serial.println("=========================================");
  Serial.println("      ESP32 WATER LEVEL MONITOR         ");
  Serial.println("=========================================");
  Serial.println();
  Serial.print  ("  Distance  : "); Serial.print(distance, 1); Serial.println(" cm");
  Serial.print  ("  Water     : "); Serial.print(pct, 1);      Serial.println(" %");
  Serial.print  ("  Status    : "); Serial.println(status);
  if (isStale)
  Serial.println("  (last good reading - sensor retrying)");
  Serial.println();
  Serial.print  ("  ");              Serial.println(bar);
  Serial.println("  0%                              100%");
  Serial.println();
  Serial.println("─────────────────────────────────────────");
  Serial.println("  Tank config:");
  Serial.print  ("    Empty at : "); Serial.print(TANK_EMPTY_CM);      Serial.println(" cm");
  Serial.print  ("    Full at  : "); Serial.print(TANK_FULL_CM);       Serial.println(" cm");
  Serial.print  ("    Usable   : "); Serial.print(TANK_EMPTY_CM - TANK_FULL_CM); Serial.println(" cm");
  Serial.print  ("    Refresh  : "); Serial.print(REFRESH_INTERVAL_MS / 1000);  Serial.println(" sec");
  Serial.println("=========================================");
}

// ─────────────────────────────────────────────
// Error screen
// ─────────────────────────────────────────────
void printError() {
  Serial.print("\033[2J\033[H");
  Serial.println("=========================================");
  Serial.println("      ESP32 WATER LEVEL MONITOR         ");
  Serial.println("=========================================");
  Serial.println();
  Serial.println("  !! SENSOR ERROR !!");
  Serial.println("  No valid reading received.");
  Serial.println();
  Serial.println("  Check wiring:");
  Serial.println("    Sensor 5V   → ESP32 VIN");
  Serial.println("    Sensor GND  → ESP32 GND");
  Serial.println("    Sensor TRIG → ESP32 GPIO 17");
  Serial.println("    Sensor ECHO → ESP32 GPIO 16");
  Serial.println();
  Serial.print  ("  Consecutive errors: "); Serial.println(consecutiveErrors);
  Serial.println("  Retrying...");
  Serial.println("=========================================");
}

// ─────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delay(500);
  Serial.println("ESP32 Water Level Monitor starting...");
  delay(1000);
}

// ─────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────
void loop() {
  float distance = measureDistance();

  if (distance < 0) {
    consecutiveErrors++;

    if (consecutiveErrors >= ERROR_THRESHOLD) {
      printError();
    } else if (lastValidDistance > 0) {
      // Temporary glitch - show last good reading with stale flag
      float pct = calculatePercentage(lastValidDistance);
      printDisplay(lastValidDistance, pct, true);
    }

  } else {
    consecutiveErrors = 0;
    lastValidDistance = distance;
    float pct = calculatePercentage(distance);
    printDisplay(distance, pct, false);
  }

  delay(REFRESH_INTERVAL_MS);
}
