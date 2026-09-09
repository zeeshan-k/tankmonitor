// ─────────────────────────────────────────────
// ESP32 Water Level Monitor - Pulse/Echo Version
// ─────────────────────────────────────────────

// ── Pin Configuration ─────────────────────────
const int ECHO_PIN = 16;   // Connect to sensor TX
const int TRIG_PIN = 17;   // Connect to sensor RX

// ── Tank Configuration ────────────────────────
const float TANK_DEPTH_CM   = 400.0;  // 4 metres 
const float MIN_DISTANCE_CM = 10.0;   // Gap at top when full

// ── Measurement Settings ──────────────────────
const int NUM_SAMPLES = 5;

// ─────────────────────────────────────────────
// Read response using Pulse/Echo
// ─────────────────────────────────────────────
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(20); 
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); 

  if (duration == 0) return -1.0; // Timeout/No echo

  // Calculate distance in cm (Speed of sound = 343 m/s)
  float distanceCM = (duration / 2.0) / 29.1;

  if (distanceCM < 20.0 || distanceCM > 600.0) return -1.0; // Out of bounds

  return distanceCM;
}

// ─────────────────────────────────────────────
// Take averaged median reading
// ─────────────────────────────────────────────
float measureDistance() {
  float readings[NUM_SAMPLES];
  int validCount = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    float r = readDistance();
    if (r > 0) readings[validCount++] = r;
    delay(100);
  }

  if (validCount == 0) return -1.0;

  // Sort for median
  for (int i = 0; i < validCount - 1; i++)
    for (int j = 0; j < validCount - i - 1; j++)
      if (readings[j] > readings[j + 1]) {
        float t = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = t;
      }

  return readings[validCount / 2];
}

// ─────────────────────────────────────────────
// Calculate percentage
// ─────────────────────────────────────────────
float calculatePercentage(float distanceCm) {
  float usableDepth = TANK_DEPTH_CM - MIN_DISTANCE_CM;
  float waterDepth  = TANK_DEPTH_CM - distanceCm;
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
  return              "EMPTY    ";
}

// ─────────────────────────────────────────────
// Draw bar [████████░░░░░░░░░░░░] 40%
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
// Print live updating display
// ─────────────────────────────────────────────
void printDisplay(float distance, float pct) {
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
  Serial.println();
  Serial.print  ("  ");              Serial.println(bar);
  Serial.print  ("  0%        ");
  Serial.println("              100%");
  Serial.println();
  Serial.println("  Tank config:");
  Serial.print  ("    Depth   : "); Serial.print(TANK_DEPTH_CM);   Serial.println(" cm");
  Serial.print  ("    Min gap : "); Serial.print(MIN_DISTANCE_CM); Serial.println(" cm");
  Serial.println("=========================================");
}

void printError() {
  Serial.print("\033[2J\033[H");
  Serial.println("=========================================");
  Serial.println("      ESP32 WATER LEVEL MONITOR         ");
  Serial.println("=========================================");
  Serial.println();
  Serial.println("  !! SENSOR ERROR !!");
  Serial.println("  No valid reading received.");
  Serial.println("  Check wiring:");
  Serial.println("    Sensor 5V  -> ESP32 VIN");
  Serial.println("    Sensor GND -> ESP32 GND");
  Serial.println("    Sensor TX  -> ESP32 GPIO 16 (Echo)");
  Serial.println("    Sensor RX  -> ESP32 GPIO 17 (Trigger)");
  Serial.println();
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
  delay(500);
  Serial.println("Starting...");
  delay(1000);
}

// ─────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────
void loop() {
  float distance = measureDistance();

  if (distance < 0) {
    printError();
  } else {
    float pct = calculatePercentage(distance);
    printDisplay(distance, pct);
  }

  delay(2000); 
}
