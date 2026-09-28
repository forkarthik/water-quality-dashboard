/**
 * =========================================================================================
 * Project: IoT-Based Water Quality Monitoring & Early-Warning System (Refined & Bulletproof)
 * Target MCU: ESP32 (NodeMCU-32S / ESP32-WROOM-32)
 * Core Sensors: Analog TDS (GPIO 34), Analog Turbidity (GPIO 35 via 10k/22k divider)
 * Local Output: 16x2 I2C LCD (Auto-detects 0x27/0x3F), Red Alert LED (GPIO 25), Buzzer (GPIO 19 via 2N2222)
 * Cloud: ThingsBoard Community (demo.thingsboard.io)
 * Device ID: 81383a60-bb11-11f1-9681-6110e8f55c0f
 * Access Token: srod8p832i6y1eh2cgn2
 * =========================================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =========================================================================================
// 1. HARDWARE PIN DEFINITIONS
// =========================================================================================
#define PIN_TDS_ADC        34      // ADC1_CH6 (TDS sensor analog output)
#define PIN_TURBIDITY_ADC  35      // ADC1_CH7 (Turbidity analog output via 10k/22k divider)
#define PIN_LED_WARN       25      // Red Warning LED (via 220R)
#define PIN_BUZZER         19      // Buzzer Driver (via 1k to 2N2222 base)

#define I2C_SDA_PIN        21      // Hardware I2C SDA
#define I2C_SCL_PIN        22      // Hardware I2C SCL

// Dynamic LCD pointer (allows auto-detection of 0x27 or 0x3F)
LiquidCrystal_I2C* lcd = nullptr;
uint8_t lcdI2CAddress = 0x27;       // Default, will be auto-scanned in setup

// =========================================================================================
// 2. WI-FI & THINGSBOARD CLOUD CONFIGURATION
// =========================================================================================
// IMPORTANT: ESP32 only connects to 2.4 GHz Wi-Fi (NOT 5 GHz).
// If using phone hotspot, ensure "Maximize Compatibility" (2.4 GHz) is turned ON.
const char* WIFI_SSID           = "YOUR_WIFI_SSID";       // <-- REPLACE WITH YOUR WI-FI / HOTSPOT NAME
const char* WIFI_PASSWORD       = "YOUR_WIFI_PASSWORD";   // <-- REPLACE WITH YOUR WI-FI PASSWORD

// Pre-configured with your exact ThingsBoard details:
const char* TB_SERVER           = "http://demo.thingsboard.io";
const char* TB_ACCESS_TOKEN     = "srod8p832i6y1eh2cgn2";

// =========================================================================================
// 3. OPERATIONAL & CALIBRATION CONSTANTS
// =========================================================================================
const int   SAMPLE_COUNT            = 30;     // Multisampling noise filter
const float TURBIDITY_DIVIDER_RATIO = (22.0f + 10.0f) / 22.0f; // 10k/22k divider reconstruction ~1.4545
const float DEFAULT_TEMPERATURE     = 25.0f;  // Standard reference baseline
const float TDS_CALIBRATION_FACTOR  = 0.5f;   // Standard EC to TDS conversion

// Screening Thresholds (IS 10500:2012 / WHO)
const float TDS_ACCEPTABLE_MAX      = 300.0f; // ppm
const float TDS_PERMISSIBLE_MAX     = 500.0f; // ppm
const float TURB_ACCEPTABLE_MAX     = 1.0f;   // NTU
const float TURB_PERMISSIBLE_MAX    = 5.0f;   // NTU

// Non-blocking Timing Loops
const unsigned long SENSOR_INTERVAL_MS = 1000; // Refresh LCD & sensors every 1 second
const unsigned long CLOUD_INTERVAL_MS  = 5000; // Upload to ThingsBoard every 5 seconds
const unsigned long WIFI_RETRY_MS      = 10000;// Retry Wi-Fi reconnect every 10 seconds

unsigned long lastSensorTime = 0;
unsigned long lastCloudTime  = 0;
unsigned long lastWifiRetry  = 0;

// =========================================================================================
// 4. DATA STRUCTURES & SYSTEM STATE
// =========================================================================================
enum WaterQualityStatus {
  STATUS_SAFE,
  STATUS_CAUTION,
  STATUS_ALERT
};

struct WaterMetrics {
  float tdsVoltage;
  float tdsPpm;
  float turbVoltage;
  float turbNtu;
  WaterQualityStatus status;
} metrics;

// =========================================================================================
// 5. FUNCTION DECLARATIONS
// =========================================================================================
void initHardware();
uint8_t scanI2CAddress();
void initLCDAuto();
void maintainWiFiConnection();
void readTDS();
void readTurbidity();
void evaluateThresholds();
void handleAlertOutputs();
void updateLCDDisplay();
void printDiagnostics();
void uploadToThingsBoard();
float sampleADCmilliVolts(uint8_t pin, int samples);

// =========================================================================================
// 6. SETUP
// =========================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n======================================================="));
  Serial.println(F(" SMART WATER QUALITY MONITORING NODE (ESP32)           "));
  Serial.println(F(" Cloud: demo.thingsboard.io                            "));
  Serial.println(F(" Token: srod8p832i6y1eh2cgn2                           "));
  Serial.println(F("======================================================="));

  initHardware();

  // Non-blocking Wi-Fi connect attempt (max 5s in setup, won't freeze system if no Wi-Fi)
  Serial.print(F("[WIFI] Connecting to: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 6000) {
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WIFI] Connected! Local IP: "));
    Serial.println(WiFi.localIP());
    if (lcd) {
      lcd->clear();
      lcd->setCursor(0, 0);
      lcd->print("WiFi Connected!");
      lcd->setCursor(0, 1);
      lcd->print(WiFi.localIP());
      delay(1500);
    }
  } else {
    Serial.println(F("\n[WIFI] Not connected. Running in LOCAL SENSOR MODE."));
    if (lcd) {
      lcd->clear();
      lcd->setCursor(0, 0);
      lcd->print("No WiFi Found");
      lcd->setCursor(0, 1);
      lcd->print("Running Local...");
      delay(1500);
    }
  }

  if (lcd) lcd->clear();
}

// =========================================================================================
// 7. MAIN LOOP (Completely Non-Blocking)
// =========================================================================================
void loop() {
  unsigned long now = millis();

  // 1. Maintain Wi-Fi in background without blocking sensors
  maintainWiFiConnection();

  // 2. Read Sensors, Refresh LCD, and Trigger Local Alerts (Every 1 Second)
  if (now - lastSensorTime >= SENSOR_INTERVAL_MS) {
    lastSensorTime = now;

    readTDS();
    readTurbidity();
    evaluateThresholds();
    handleAlertOutputs();
    updateLCDDisplay();
    printDiagnostics();
  }

  // 3. Upload Telemetry to ThingsBoard (Every 5 Seconds)
  if (now - lastCloudTime >= CLOUD_INTERVAL_MS) {
    lastCloudTime = now;
    uploadToThingsBoard();
  }
}

// =========================================================================================
// 8. I2C AUTO-SCAN & LCD INITIALIZATION
// =========================================================================================

/**
 * @brief Scans I2C bus to find whether the LCD is at 0x27, 0x3F, or another address.
 */
uint8_t scanI2CAddress() {
  Serial.println(F("[I2C] Scanning bus on SDA=21, SCL=22..."));
  uint8_t detectedAddr = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("[I2C] Device found at address 0x%02X\n", addr);
      if (addr == 0x27 || addr == 0x3F) {
        detectedAddr = addr;
      }
    }
  }

  if (detectedAddr == 0) {
    Serial.println(F("[I2C] WARNING: No standard LCD address (0x27 or 0x3F) found. Defaulting to 0x27."));
    detectedAddr = 0x27;
  }
  return detectedAddr;
}

void initLCDAuto() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  lcdI2CAddress = scanI2CAddress();
  Serial.printf("[LCD] Initializing LiquidCrystal_I2C at 0x%02X...\n", lcdI2CAddress);

  lcd = new LiquidCrystal_I2C(lcdI2CAddress, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->clear();

  lcd->setCursor(0, 0);
  lcd->print("Water Monitor");
  lcd->setCursor(0, 1);
  lcd->print("System Boot OK");
  delay(1200);
}

void initHardware() {
  pinMode(PIN_LED_WARN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED_WARN, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_TDS_ADC, INPUT);
  pinMode(PIN_TURBIDITY_ADC, INPUT);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  // Initialize LCD with auto-detection
  initLCDAuto();

  // Self-test chirp
  digitalWrite(PIN_LED_WARN, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(120);
  digitalWrite(PIN_LED_WARN, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.println(F("[SYS_INIT] Hardware initialization complete."));
}

// =========================================================================================
// 9. WI-FI BACKGROUND MAINTENANCE
// =========================================================================================
void maintainWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiRetry >= WIFI_RETRY_MS) {
      lastWifiRetry = now;
      Serial.println(F("[WIFI] Attempting background reconnect..."));
      WiFi.reconnect();
    }
  }
}

// =========================================================================================
// 10. SENSOR ACQUISITION & CALCULATIONS
// =========================================================================================

float sampleADCmilliVolts(uint8_t pin, int samples) {
  uint32_t totalMv = 0;
  for (int i = 0; i < samples; i++) {
    totalMv += analogReadMilliVolts(pin);
    delayMicroseconds(150);
  }
  return (float)totalMv / (float)samples;
}

void readTDS() {
  float mv = sampleADCmilliVolts(PIN_TDS_ADC, SAMPLE_COUNT);
  float voltage = mv / 1000.0f;
  metrics.tdsVoltage = voltage;

  // Temperature compensation to 25 C
  float compCoeff = 1.0f + 0.02f * (DEFAULT_TEMPERATURE - 25.0f);
  float compVolt = voltage / compCoeff;

  // Gravity standard conversion curve
  float calculated = (133.42f * pow(compVolt, 3) 
                    - 255.86f * pow(compVolt, 2) 
                    + 857.39f * compVolt) * TDS_CALIBRATION_FACTOR;

  if (calculated < 0.0f) calculated = 0.0f;
  metrics.tdsPpm = calculated;
}

void readTurbidity() {
  float mv = sampleADCmilliVolts(PIN_TURBIDITY_ADC, SAMPLE_COUNT);
  float adcV = mv / 1000.0f;

  // Reconstruct true voltage across 10k/22k divider:
  float sensorV = adcV * TURBIDITY_DIVIDER_RATIO;
  metrics.turbVoltage = sensorV;

  float ntu = 0.0f;
  if (sensorV >= 4.10f) {
    ntu = 0.0f;
  } else if (sensorV <= 2.50f) {
    ntu = 3000.0f;
  } else {
    ntu = -1120.4f * pow(sensorV, 2) + 5742.3f * sensorV - 4352.9f;
  }

  if (ntu < 0.0f) ntu = 0.0f;
  metrics.turbNtu = ntu;
}

void evaluateThresholds() {
  if (metrics.tdsPpm > TDS_PERMISSIBLE_MAX || metrics.turbNtu > TURB_PERMISSIBLE_MAX) {
    metrics.status = STATUS_ALERT;
  } else if (metrics.tdsPpm > TDS_ACCEPTABLE_MAX || metrics.turbNtu > TURB_ACCEPTABLE_MAX) {
    metrics.status = STATUS_CAUTION;
  } else {
    metrics.status = STATUS_SAFE;
  }
}

void handleAlertOutputs() {
  switch (metrics.status) {
    case STATUS_ALERT:
      digitalWrite(PIN_LED_WARN, HIGH);
      digitalWrite(PIN_BUZZER, HIGH);
      delay(60);
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case STATUS_CAUTION:
      digitalWrite(PIN_LED_WARN, !digitalRead(PIN_LED_WARN)); // Slow blink
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case STATUS_SAFE:
    default:
      digitalWrite(PIN_LED_WARN, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      break;
  }
}

// =========================================================================================
// 11. 16x2 I2C LCD DISPLAY LOGIC
// =========================================================================================
void updateLCDDisplay() {
  if (!lcd) return;

  char row0[17];
  char row1[17];

  const char* cloudTag = (WiFi.status() == WL_CONNECTED) ? "TB" : "NC";
  const char* statTag  = "SAFE";
  if (metrics.status == STATUS_ALERT)   statTag = "ALRT";
  if (metrics.status == STATUS_CAUTION) statTag = "WARN";

  // Row 0: "TDS: 285ppm  [TB]" (Exactly 16 characters)
  snprintf(row0, sizeof(row0), "TDS:%4dppm [%2s]", (int)metrics.tdsPpm, cloudTag);

  // Row 1: "NTU: 1.4  [SAFE]" (Exactly 16 characters)
  snprintf(row1, sizeof(row1), "NTU:%4.1f  [%4s]", metrics.turbNtu, statTag);

  lcd->setCursor(0, 0);
  lcd->print(row0);

  lcd->setCursor(0, 1);
  lcd->print(row1);
}

// =========================================================================================
// 12. SERIAL MONITOR TELEMETRY
// =========================================================================================
void printDiagnostics() {
  Serial.printf("[SEC: %4lu] | TDS: %4.1f ppm (V=%1.2f) | Turb: %3.1f NTU (V=%1.2f) | WiFi: %s | Status: ",
                millis() / 1000,
                metrics.tdsPpm, metrics.tdsVoltage,
                metrics.turbNtu, metrics.turbVoltage,
                (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "OFFLINE");

  switch (metrics.status) {
    case STATUS_SAFE:    Serial.println(F("SAFE")); break;
    case STATUS_CAUTION: Serial.println(F("CAUTION")); break;
    case STATUS_ALERT:   Serial.println(F("CRITICAL ALERT")); break;
  }
}

// =========================================================================================
// 13. CLOUD TELEMETRY UPLOAD TO THINGSBOARD
// =========================================================================================
void uploadToThingsBoard() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[TB_UPLOAD] Wi-Fi not connected. Skipping cloud upload."));
    return;
  }

  HTTPClient http;
  String url = String(TB_SERVER) + "/api/v1/" + String(TB_ACCESS_TOKEN) + "/telemetry";

  http.begin(url);
  http.setTimeout(3500); // 3.5s timeout prevents hanging the system
  http.addHeader("Content-Type", "application/json");

  String statusStr = "NORMAL";
  if (metrics.status == STATUS_ALERT)   statusStr = "ALERT";
  if (metrics.status == STATUS_CAUTION) statusStr = "WARNING";

  // Construct JSON payload
  String json = "{";
  json += "\"tds\":" + String(metrics.tdsPpm, 1) + ",";
  json += "\"turbidity\":" + String(metrics.turbNtu, 1) + ",";
  json += "\"status\":\"" + statusStr + "\"";
  json += "}";

  Serial.print(F("[TB_UPLOAD] Uploading: "));
  Serial.println(json);

  int httpCode = http.POST(json);

  if (httpCode == 200) {
    Serial.println(F("[TB_UPLOAD] SUCCESS! HTTP 200 OK received from ThingsBoard."));
  } else {
    Serial.printf("[TB_UPLOAD] FAILED! HTTP Error Code: %d\n", httpCode);
  }

  http.end();
}
