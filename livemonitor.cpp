// ═════════════════════════════════════════════════════════════════
//        ESP32 WATER LEVEL MONITOR - JSN-SR04T (Pulse/Echo)
//        With noise/turbulence filtering
// ═════════════════════════════════════════════════════════════════

// ── PIN CONFIGURATION ─────────────────────────────────────────────
const int TRIG_PIN = 17;        // Trigger pin → connect to sensor TRIG
const int ECHO_PIN = 16;        // Echo pin    → connect to sensor ECHO

// ── TANK CONFIGURATION ────────────────────────────────────────────
// Distance (cm) from sensor to tank floor when tank is EMPTY
const float TANK_EMPTY_CM     = 400.0;

// Distance (cm) from sensor to water surface when tank is FULL
const float TANK_FULL_CM      = 150.0;

// ── DISPLAY / TIMING ──────────────────────────────────────────────
const int REFRESH_INTERVAL_MS = 5000;   // Screen refresh in milliseconds

// ── MEASUREMENT QUALITY ───────────────────────────────────────────
const int NUM_SAMPLES         = 9;      // Samples per cycle
                                        // Higher = better filtering
                                        // Recommended: 7 to 11 for turbulent water

const int SAMPLE_DELAY_MS     = 150;    // Delay between samples (ms)
                                        // Do not go below 100ms

const int ECHO_TIMEOUT_US     = 40000;  // Echo timeout in microseconds

// ── OUTLIER FILTERING ─────────────────────────────────────────────
// After sorting samples, discard this many from each end
// Example: with 9 samples and OUTLIER_TRIM=2, middle 5 are used
// Higher = more aggressive outlier removal
// Recommended: 1 to 3 (must be less than NUM_SAMPLES/2)
const int OUTLIER_TRIM        = 2;

// ── SPIKE REJECTION ───────────────────────────────────────────────
// If new reading differs from last valid reading by more than this (cm),
// it is treated as a spike and rejected
// Set high enough to allow real water level changes
// Set low enough to reject splashing spikes
// Recommended: 20 to 40 cm for a turbulent tank
const float MAX_CHANGE_CM     = 30.0;

// ── ROLLING AVERAGE (SMOOTHING OVER TIME) ─────────────────────────
// Number of cycles to smooth over
// Higher = smoother display but slower to react to real level changes
// Recommended: 3 to 6
const int ROLLING_WINDOW      = 4;

// ── ERROR HANDLING ────────────────────────────────────────────────
const int ERROR_THRESHOLD     = 3;      // Consecutive failures before error screen

// ═════════════════════════════════════════════════════════════════
//   DO NOT MODIFY BELOW UNLESS YOU KNOW WHAT YOU ARE DOING
// ═════════════════════════════════════════════════════════════════

float lastValidDistance               = -1.0;
int   consecutiveErrors               = 0;
float rollingBuffer[ROLLING_WINDOW];
int   rollingIndex                    = 0;
int   rollingCount                    = 0;

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

  if (distanceCM < 20.0 || distanceCM > 600.0) return -1.0;

  return distanceCM;
}

// ─────────────────────────────────────────────
// Trimmed mean:
//   1. Collect NUM_SAMPLES readings
//   2. Sort them
//   3. Discard OUTLIER_TRIM from each end
//   4. Average the middle ones
//
// This removes both high spikes (splashing)
// and low spikes (mist/foam false short readings)
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

  // Sort ascending
  for (int i = 0; i < validCount - 1; i++)
    for (int j = 0; j < validCount - i - 1; j++)
      if (readings[j] > readings[j + 1]) {
        float t        = readings[j];
        readings[j]    = readings[j + 1];
        readings[j+1]  = t;
      }

  // Trim outliers from both ends
  int trimStart = OUTLIER_TRIM;
  int trimEnd   = validCount - OUTLIER_TRIM;

  // Safety: make sure we still have samples left after trimming
  if (trimEnd <= trimStart) {
    // Not enough samples to trim - fall back to median
    return readings[validCount / 2];
  }

  // Average the middle values
  float sum   = 0;
  int   count = 0;
  for (int i = trimStart; i < trimEnd; i++) {
    sum += readings[i];
    count++;
  }

  return sum / count;
}

// ─────────────────────────────────────────────
// Spike rejection:
//   If reading jumps more than MAX_CHANGE_CM
//   from last known good value, reject it
//   Real water level changes slowly
//   Splashes change it suddenly
// ─────────────────────────────────────────────
bool isSpike(float newReading) {
  if (lastValidDistance < 0) return false; // No reference yet, accept all
  return abs(newReading - lastValidDistance) > MAX_CHANGE_CM;
}

// ─────────────────────────────────────────────
// Rolling average:
//   Keeps last ROLLING_WINDOW valid readings
//   Returns their average
//   Smooths out cycle-to-cycle jitter
// ─────────────────────────────────────────────
void addToRollingBuffer(float value) {
  rollingBuffer[rollingIndex] = value;
  rollingIndex = (rollingIndex + 1) % ROLLING_WINDOW;
  if (rollingCount < ROLLING_WINDOW) rollingCount++;
}

float getRollingAverage() {
  if (rollingCount == 0) return -1.0;
  float sum = 0;
  for (int i = 0; i < rollingCount; i++)
    sum += rollingBuffer[i];
  return sum / rollingCount;
}

// ─────────────────────────────────────────────
// Calculate water level percentage
// ─────────────────────────────────────────────
float calculatePercentage(float distanceCm) {
  float usableDepth = TANK_EMPTY_CM - TANK_FULL_CM;
  float waterDepth  = TANK_EMPTY_CM - distanceCm;
  return constrain((waterDepth / usableDepth) * 100.0, 0.0, 100.0);
}

// ─────────────────────────────────────────────
// Status label
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
// ASCII bar
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
// Main display
// ─────────────────────────────────────────────
void printDisplay(float rawDistance, float smoothedDistance, float pct, bool isStale) {
  String status = getStatus(pct);
  String bar    = drawBar(pct);

  Serial.print("\033[2J\033[H");
  Serial.println("=========================================");
  Serial.println("      ESP32 WATER LEVEL MONITOR         ");
  Serial.println("=========================================");
  Serial.println();
  Serial.print  ("  Raw reading  : "); Serial.print(rawDistance, 1);      Serial.println(" cm");
  Serial.print  ("  Smoothed     : "); Serial.print(smoothedDistance, 1); Serial.println(" cm");
  Serial.print  ("  Water level  : "); Serial.print(pct, 1);              Serial.println(" %");
  Serial.print  ("  Status       : "); Serial.println(status);
  if (isStale)
  Serial.println("  (last good reading - sensor retrying)");
  Serial.println();
  Serial.print  ("  ");                Serial.println(bar);
  Serial.println("  0%                              100%");
  Serial.println();
  Serial.println("─────────────────────────────────────────");
  Serial.println("  Tank config:");
  Serial.print  ("    Empty at   : "); Serial.print(TANK_EMPTY_CM);                Serial.println(" cm");
  Serial.print  ("    Full at    : "); Serial.print(TANK_FULL_CM);                 Serial.println(" cm");
  Serial.print  ("    Usable     : "); Serial.print(TANK_EMPTY_CM - TANK_FULL_CM); Serial.println(" cm");
  Serial.println("─────────────────────────────────────────");
  Serial.println("  Filter config:");
  Serial.print  ("    Samples    : "); Serial.println(NUM_SAMPLES);
  Serial.print  ("    Trim each  : "); Serial.println(OUTLIER_TRIM);
  Serial.print  ("    Max change : "); Serial.print(MAX_CHANGE_CM);  Serial.println(" cm");
  Serial.print  ("    Smoothing  : "); Serial.print(ROLLING_WINDOW); Serial.println(" cycles");
  Serial.print  ("    Refresh    : "); Serial.print(REFRESH_INTERVAL_MS / 1000); Serial.println(" sec");
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
  Serial.print  ("  Consecutive errors : "); Serial.println(consecutiveErrors);
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

  // Initialise rolling buffer to zero
  for (int i = 0; i < ROLLING_WINDOW; i++)
    rollingBuffer[i] = 0.0;

  delay(500);
  Serial.println("ESP32 Water Level Monitor starting...");
  delay(1000);
}

// ─────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────
void loop() {
  float rawDistance = measureDistance();  // trimmed mean of samples

  if (rawDistance < 0) {
    consecutiveErrors++;

    if (consecutiveErrors >= ERROR_THRESHOLD) {
      printError();
    } else if (lastValidDistance > 0) {
      float smoothed = getRollingAverage();
      float pct      = calculatePercentage(smoothed);
      printDisplay(lastValidDistance, smoothed, pct, true);
    }

  } else if (isSpike(rawDistance)) {
    // Sudden jump - likely splash or disturbance
    // Ignore this reading, keep last smoothed value
    Serial.print("Spike rejected: ");
    Serial.print(rawDistance);
    Serial.print(" cm (last valid: ");
    Serial.print(lastValidDistance);
    Serial.println(" cm)");

    if (lastValidDistance > 0) {
      float smoothed = getRollingAverage();
      float pct      = calculatePercentage(smoothed);
      printDisplay(lastValidDistance, smoothed, pct, true);
    }

  } else {
    // Good reading
    consecutiveErrors = 0;
    lastValidDistance = rawDistance;

    addToRollingBuffer(rawDistance);
    float smoothed = getRollingAverage();
    float pct      = calculatePercentage(smoothed);
    printDisplay(rawDistance, smoothed, pct, false);
  }

  delay(REFRESH_INTERVAL_MS);
}
