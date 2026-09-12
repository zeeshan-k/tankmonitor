// ═════════════════════════════════════════════════════════════════
//        ESP32 WATER LEVEL MONITOR - JSN-SR04T (Pulse/Echo)
//        With noise filtering, Wi-Fi Stats, & Blynk Rate Limiting
// ═════════════════════════════════════════════════════════════════

// ── BLYNK CONFIGURATION ───────────────────────────────────────────
#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Water Level"
#define BLYNK_AUTH_TOKEN "************************"

// ── LIBRARIES ─────────────────────────────────────────────────────
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

// ── WI-FI CONFIGURATION ───────────────────────────────────────────
char ssid[] = "**************************";
char pass[] = "**************************";

// ── PIN CONFIGURATION ─────────────────────────────────────────────
const int TRIG_PIN = 17;        
const int ECHO_PIN = 16;        

// ── TANK CONFIGURATION ────────────────────────────────────────────
const float TANK_EMPTY_CM     = 200.0;
const float TANK_FULL_CM      = 10.0;

// ── DISPLAY / TIMING ──────────────────────────────────────────────
const int REFRESH_INTERVAL_MS = 5000;           // Serial monitor update (5 sec)
const unsigned long BLYNK_UPDATE_MS = 60000;    // Blynk push interval (60 sec)

// ── MEASUREMENT QUALITY ───────────────────────────────────────────
const int NUM_SAMPLES         = 9;      
const int SAMPLE_DELAY_MS     = 150;    
const int ECHO_TIMEOUT_US     = 40000;  

// ── OUTLIER FILTERING ─────────────────────────────────────────────
const int OUTLIER_TRIM        = 2;

// ── SPIKE REJECTION ───────────────────────────────────────────────
const float MAX_CHANGE_CM     = 30.0;

// ── ROLLING AVERAGE ───────────────────────────────────────────────
const int ROLLING_WINDOW      = 4;

// ── ERROR HANDLING ────────────────────────────────────────────────
const int ERROR_THRESHOLD     = 3;      

// ═════════════════════════════════════════════════════════════════
//   VARIABLES & TIMERS
// ═════════════════════════════════════════════════════════════════

float lastValidDistance       = -1.0;
int   consecutiveErrors       = 0;
float rollingBuffer[ROLLING_WINDOW];
int   rollingIndex            = 0;
int   rollingCount            = 0;
unsigned long lastBlynkPush   = 0;

BlynkTimer timer; 

// ─────────────────────────────────────────────
// Single pulse/echo reading
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
// Trimmed mean
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

  for (int i = 0; i < validCount - 1; i++)
    for (int j = 0; j < validCount - i - 1; j++)
      if (readings[j] > readings[j + 1]) {
        float t        = readings[j];
        readings[j]    = readings[j + 1];
        readings[j+1]  = t;
      }

  int trimStart = OUTLIER_TRIM;
  int trimEnd   = validCount - OUTLIER_TRIM;

  if (trimEnd <= trimStart) {
    return readings[validCount / 2];
  }

  float sum   = 0;
  int   count = 0;
  for (int i = trimStart; i < trimEnd; i++) {
    sum += readings[i];
    count++;
  }

  return sum / count;
}

// ─────────────────────────────────────────────
// Spike rejection
// ─────────────────────────────────────────────
bool isSpike(float newReading) {
  if (lastValidDistance < 0) return false; 
  return abs(newReading - lastValidDistance) > MAX_CHANGE_CM;
}

// ─────────────────────────────────────────────
// Rolling average
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
  return                  "EMPTY    ";
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
// Main Serial Display
// ─────────────────────────────────────────────
void printDisplay(float rawDistance, float smoothedDistance, float pct, bool isStale) {
  String status = getStatus(pct);
  String bar    = drawBar(pct);
  float filledLevel = TANK_EMPTY_CM - smoothedDistance;
  String wifiStat = (WiFi.status() == WL_CONNECTED) ? "Connected (" + String(WiFi.RSSI()) + " dBm)" : "Disconnected";

  Serial.print("\033[2J\033[H");
  Serial.println("=========================================");
  Serial.println("      ESP32 WATER LEVEL MONITOR         ");
  Serial.println("=========================================");
  Serial.println();
  Serial.print  ("  Raw reading  : "); Serial.print(rawDistance, 1);      Serial.println(" cm");
  Serial.print  ("  Filled Level : "); Serial.print(filledLevel, 1);      Serial.println(" cm");
  Serial.print  ("  Water volume : "); Serial.print(pct, 1);              Serial.println(" %");
  Serial.print  ("  Status       : "); Serial.println(status);
  Serial.print  ("  Wi-Fi        : "); Serial.println(wifiStat);
  
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
  Serial.println("=========================================");
}

// ─────────────────────────────────────────────
// Push Data to Blynk Cloud (Rate Limited)
// ─────────────────────────────────────────────
void pushToBlynk(float pct, float distance, String statusStr) {
  if (Blynk.connected() && (millis() - lastBlynkPush >= BLYNK_UPDATE_MS || lastBlynkPush == 0)) {
    float filledLevel = TANK_EMPTY_CM - distance;
    String wifiStat = (WiFi.status() == WL_CONNECTED) ? "Connected (" + String(WiFi.RSSI()) + " dBm)" : "Offline";

    Blynk.virtualWrite(V0, pct);         
    Blynk.virtualWrite(V1, filledLevel); 
    Blynk.virtualWrite(V2, statusStr);   
    Blynk.virtualWrite(V3, wifiStat);    
    
    lastBlynkPush = millis();
  }
}

// ─────────────────────────────────────────────
// Error Handling
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
  Serial.print  ("  Consecutive errors : "); Serial.println(consecutiveErrors);
  Serial.println("  Retrying...");
  Serial.println("=========================================");
  
  // Rate limited error push
  if (Blynk.connected() && (millis() - lastBlynkPush >= BLYNK_UPDATE_MS || lastBlynkPush == 0)) {
    Blynk.virtualWrite(V2, "SENSOR ERROR");
    lastBlynkPush = millis();
  }
}

// ─────────────────────────────────────────────
// Main Measurement Logic (Called by Timer)
// ─────────────────────────────────────────────
void processWaterLevel() {
  float rawDistance = measureDistance();  

  if (rawDistance < 0) {
    consecutiveErrors++;

    if (consecutiveErrors >= ERROR_THRESHOLD) {
      printError();
    } else if (lastValidDistance > 0) {
      float smoothed = getRollingAverage();
      float pct      = calculatePercentage(smoothed);
      printDisplay(lastValidDistance, smoothed, pct, true);
      pushToBlynk(pct, smoothed, getStatus(pct));
    }

  } else if (isSpike(rawDistance)) {
    Serial.print("Spike rejected: ");
    Serial.print(rawDistance);
    Serial.println(" cm");

    if (lastValidDistance > 0) {
      float smoothed = getRollingAverage();
      float pct      = calculatePercentage(smoothed);
      printDisplay(lastValidDistance, smoothed, pct, true);
      pushToBlynk(pct, smoothed, getStatus(pct));
    }

  } else {
    consecutiveErrors = 0;
    lastValidDistance = rawDistance;

    addToRollingBuffer(rawDistance);
    float smoothed = getRollingAverage();
    float pct      = calculatePercentage(smoothed);
    
    printDisplay(rawDistance, smoothed, pct, false);
    pushToBlynk(pct, smoothed, getStatus(pct));
  }
}

// ─────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  for (int i = 0; i < ROLLING_WINDOW; i++)
    rollingBuffer[i] = 0.0;

  Serial.println("\n--- BOOTING ---");
  Serial.print("Connecting to Wi-Fi (");
  Serial.print(ssid);
  Serial.print(") ");
  
  WiFi.begin(ssid, pass);
  int attempts = 0;
  
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi Connected Successfully!");
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("Wi-Fi Connection Failed!");
    Serial.println("System will continue attempting to connect in the background.");
  }

  Serial.println("Starting Blynk connection...");
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  timer.setInterval(REFRESH_INTERVAL_MS, processWaterLevel);
  Serial.println("ESP32 Water Level Monitor starting in 2 seconds...");
  delay(2000);
}

// ─────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────
void loop() {
  Blynk.run();   
  timer.run();   
}
