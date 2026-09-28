/**
 * =========================================================================================
 * Project: IoT-Based Water Quality Monitoring & Early-Warning System (Rural Community Prototype)
 * Target MCU: ESP32 Development Board (NodeMCU / ESP32-WROOM-32)
 * Core Sensors: Analog TDS Sensor, Analog Turbidity Sensor
 * User Interface: 16x2 I2C LCD, Warning Red LED, NPN-Driven Alert Buzzer
 * Cloud Platform: ThingsBoard Community Edition (demo.thingsboard.io) via Wi-Fi HTTP POST
 * Future Expansion: SIM7600E-H 4G Cellular Telemetry, SMS Alerts
 * =========================================================================================
 * 
 * HARDWARE WIRING SUMMARY:
 * -----------------------------------------------------------------------------------------
 * 1. TDS Module:
 *    - VCC  --> ESP32 3V3 (Ensures signal output remains <= 2.3V, safely below ESP32 3.3V ADC limit)
 *    - GND  --> ESP32 GND
 *    - AOUT --> GPIO 34 (ESP32 ADC1_CH6)
 * 
 * 2. Turbidity Module:
 *    - VCC  --> ESP32 VIN (5V from USB; required for sufficient IR photodiode emission)
 *    - GND  --> ESP32 Common GND
 *    - AOUT --> Voltage Divider input (R1 = 10 kΩ, R2 = 22 kΩ to GND)
 *               Divider midpoint connects to GPIO 35 (ADC1_CH7). Max output ~3.09V <= 3.3V
 * 
 * 3. 16x2 I2C LCD:
 *    - VCC  --> ESP32 VIN (5V) or 3V3 (Depends on LCD module, 5V recommended for high contrast)
 *    - GND  --> ESP32 Common GND
 *    - SDA  --> GPIO 21
 *    - SCL  --> GPIO 22
 * 
 * 4. Alert Red LED:
 *    - GPIO 25 --> 220 Ω Resistor --> LED Anode (+)
 *    - LED Cathode (-) --> ESP32 GND
 * 
 * 5. Alert Buzzer (Driven by 2N2222 NPN Transistor):
 *    - GPIO 19 --> 1 kΩ Base Resistor --> 2N2222 Base (Pin 2)
 *    - 2N2222 Emitter (Pin 3) --> ESP32 Common GND
 *    - Buzzer (-) Pin --> 2N2222 Collector (Pin 1)
 *    - Buzzer (+) Pin --> ESP32 VIN (5V)
 * =========================================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =========================================================================================
// 1. PIN DEFINITIONS & HARDWARE CONSTANTS
// =========================================================================================
#define PIN_TDS_ADC        34      // ADC1_CH6 (Input-only pin, no WiFi conflict)
#define PIN_TURBIDITY_ADC  35      // ADC1_CH7 (Input-only pin, no WiFi conflict)
#define PIN_LED_WARN       25      // Digital output for Red Warning LED (via 220R)
#define PIN_BUZZER         19      // Digital output for Buzzer Driver (via 1k to 2N2222 base)

#define I2C_SDA_PIN        21      // Default ESP32 Hardware I2C Data
#define I2C_SCL_PIN        22      // Default ESP32 Hardware I2C Clock

// LCD Configuration: Address 0x27 (or 0x3F), 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =========================================================================================
// 2. WI-FI & THINGSBOARD CLOUD CONFIGURATION
// =========================================================================================
// Replace with your local Wi-Fi credentials (e.g. mobile hotspot or router)
const char* WIFI_SSID           = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD       = "YOUR_WIFI_PASSWORD";

// ThingsBoard Server & Device Access Token
const char* TB_SERVER           = "http://demo.thingsboard.io";
const char* TB_ACCESS_TOKEN     = "srod8p832i6y1eh2cgn2";

// =========================================================================================
// 3. OPERATIONAL & CALIBRATION CONSTANTS
// =========================================================================================
const int   SAMPLE_SAMPLES_COUNT   = 30;     // Multisampling window for noise filtering

// Voltage Divider Factor for Turbidity Sensor:
// Sensor Vout (up to 4.5V) divided by R1 = 10k, R2 = 22k -> V_adc = V_sensor * (22 / 32)
// Reconstruct true sensor voltage: V_sensor = V_adc * (32 / 22) = V_adc * 1.4545
const float TURBIDITY_DIVIDER_RATIO = (22.0f + 10.0f) / 22.0f;

// Temperature baseline (since no DS18B20 is fitted, assume standard lab baseline 25.0 C)
const float DEFAULT_TEMPERATURE    = 25.0f;

// Calibration Multipliers & Offsets (Standard gravity EC to TDS factor)
const float TDS_CALIBRATION_FACTOR = 0.5f;

// Water Quality Screening Thresholds (Derived from WHO & Indian Standard IS 10500:2012)
const float TDS_ACCEPTABLE_MAX     = 300.0f; // ppm (Desirable drinking limit)
const float TDS_PERMISSIBLE_MAX    = 500.0f; // ppm (Permissible upper limit)

const float TURB_ACCEPTABLE_MAX    = 1.0f;   // NTU (Desirable limit for clear water)
const float TURB_PERMISSIBLE_MAX   = 5.0f;   // NTU (Maximum permissible limit)

// System Timing Configuration (Non-blocking Millis Loop)
const unsigned long SENSOR_READ_INTERVAL_MS = 1500; // Sensor sampling: 1.5 seconds
const unsigned long LCD_PAGE_INTERVAL_MS    = 3000; // Alternating LCD screens: 3 seconds
const unsigned long TB_UPLOAD_INTERVAL_MS   = 5000; // Cloud telemetry upload: 5 seconds

unsigned long lastSensorReadTime            = 0;
unsigned long lastLcdSwitchTime             = 0;
unsigned long lastTbUploadTime              = 0;
int lcdDisplayMode                          = 0;

// =========================================================================================
// 4. ENUMERATIONS & SYSTEM STATE
// =========================================================================================
enum WaterQualityStatus {
  STATUS_SAFE,      // Within desirable baseline limits
  STATUS_CAUTION,   // Elevated parameters (approaching permissible thresholds)
  STATUS_ALERT      // Exceeds permissible guidelines (High particulate/salinity risk)
};

struct WaterMetrics {
  float tdsRawVoltage;
  float tdsPpm;
  float turbRawVoltage;
  float turbNtu;
  WaterQualityStatus status;
} currentMetrics;

// =========================================================================================
// 5. FUNCTION DECLARATIONS
// =========================================================================================
void initHardware();
void connectWiFi();
void readTDS(WaterMetrics &metrics);
void readTurbidity(WaterMetrics &metrics);
void checkThresholds(WaterMetrics &metrics);
void controlAlerts(const WaterMetrics &metrics);
void updateLCD(const WaterMetrics &metrics);
void printSerialTelemetry(const WaterMetrics &metrics);
void sendThingsBoardTelemetry(const WaterMetrics &metrics);
float readAveragedAdcMilliVolts(uint8_t pin, int samples);

// =========================================================================================
// 6. ARDUINO SETUP
// =========================================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n======================================================="));
  Serial.println(F(" RURAL WATER QUALITY MONITORING & EARLY WARNING SYSTEM "));
  Serial.println(F(" Core MCU: ESP32 | Target: Multi-parameter Screening   "));
  Serial.println(F("======================================================="));

  initHardware();

  // Initial Splash Screen on 16x2 LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Water Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi");

  // Attempt Wi-Fi Connection
  connectWiFi();

  delay(1200);
  lcd.clear();
}

// =========================================================================================
// 7. ARDUINO MAIN LOOP (Non-Blocking Cooperative Scheduling)
// =========================================================================================
void loop() {
  unsigned long currentMillis = millis();

  // Periodic Sensor Acquisition & Processing (Every 1.5s)
  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadTime = currentMillis;

    readTDS(currentMetrics);
    readTurbidity(currentMetrics);
    checkThresholds(currentMetrics);
    controlAlerts(currentMetrics);
    printSerialTelemetry(currentMetrics);
  }

  // Periodic LCD Display Multiplexing (Every 3s)
  if (currentMillis - lastLcdSwitchTime >= LCD_PAGE_INTERVAL_MS) {
    lastLcdSwitchTime = currentMillis;
    lcdDisplayMode = !lcdDisplayMode;
    updateLCD(currentMetrics);
  }

  // Periodic ThingsBoard Cloud Upload (Every 5s)
  if (currentMillis - lastTbUploadTime >= TB_UPLOAD_INTERVAL_MS) {
    lastTbUploadTime = currentMillis;
    sendThingsBoardTelemetry(currentMetrics);
  }
}

// =========================================================================================
// 8. MODULAR IMPLEMENTATIONS
// =========================================================================================

/**
 * @brief Initializes GPIO modes, ADC configuration, and I2C LCD.
 */
void initHardware() {
  pinMode(PIN_LED_WARN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_WARN, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_TDS_ADC, INPUT);
  pinMode(PIN_TURBIDITY_ADC, INPUT);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();

  // Hardware self-test chirp
  digitalWrite(PIN_LED_WARN, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(120);
  digitalWrite(PIN_LED_WARN, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.println(F("[SYS_INIT] Hardware initialization complete."));
}

/**
 * @brief Connects to Wi-Fi network with 15-second non-blocking timeout.
 */
void connectWiFi() {
  Serial.printf("[WIFI] Connecting to SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WIFI] Connected successfully!"));
    Serial.print(F("[WIFI] IP Address: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\n[WIFI] Connection timed out. Running in standalone local mode."));
  }
}

/**
 * @brief Reads averaged analog voltage using ESP32 calibrated API.
 */
float readAveragedAdcMilliVolts(uint8_t pin, int samples) {
  uint32_t totalMv = 0;
  for (int i = 0; i < samples; i++) {
    totalMv += analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  return (float)totalMv / (float)samples;
}

/**
 * @brief Acquires TDS sensor voltage and calculates ppm.
 */
void readTDS(WaterMetrics &metrics) {
  float voltageMv = readAveragedAdcMilliVolts(PIN_TDS_ADC, SAMPLE_SAMPLES_COUNT);
  float voltage = voltageMv / 1000.0f;
  metrics.tdsRawVoltage = voltage;

  float compensationCoefficient = 1.0f + 0.02f * (DEFAULT_TEMPERATURE - 25.0f);
  float compensationVoltage = voltage / compensationCoefficient;

  float calculatedTds = (133.42f * pow(compensationVoltage, 3) 
                       - 255.86f * pow(compensationVoltage, 2) 
                       + 857.39f * compensationVoltage) * TDS_CALIBRATION_FACTOR;

  if (calculatedTds < 0.0f) calculatedTds = 0.0f;
  metrics.tdsPpm = calculatedTds;
}

/**
 * @brief Acquires Turbidity sensor voltage and computes NTU.
 */
void readTurbidity(WaterMetrics &metrics) {
  float adcMv = readAveragedAdcMilliVolts(PIN_TURBIDITY_ADC, SAMPLE_SAMPLES_COUNT);
  float adcV = adcMv / 1000.0f;

  float sensorVoltage = adcV * TURBIDITY_DIVIDER_RATIO;
  metrics.turbRawVoltage = sensorVoltage;

  float ntu = 0.0f;
  if (sensorVoltage >= 4.10f) {
    ntu = 0.0f;
  } else if (sensorVoltage <= 2.50f) {
    ntu = 3000.0f;
  } else {
    ntu = -1120.4f * pow(sensorVoltage, 2) + 5742.3f * sensorVoltage - 4352.9f;
  }

  if (ntu < 0.0f) ntu = 0.0f;
  metrics.turbNtu = ntu;
}

/**
 * @brief Assesses sensor values against WHO & IS 10500 standards to classify risk.
 */
void checkThresholds(WaterMetrics &metrics) {
  if (metrics.tdsPpm > TDS_PERMISSIBLE_MAX || metrics.turbNtu > TURB_PERMISSIBLE_MAX) {
    metrics.status = STATUS_ALERT;
  } else if (metrics.tdsPpm > TDS_ACCEPTABLE_MAX || metrics.turbNtu > TURB_ACCEPTABLE_MAX) {
    metrics.status = STATUS_CAUTION;
  } else {
    metrics.status = STATUS_SAFE;
  }
}

/**
 * @brief Actuates Red LED and Buzzer based on evaluated status.
 */
void controlAlerts(const WaterMetrics &metrics) {
  switch (metrics.status) {
    case STATUS_ALERT:
      digitalWrite(PIN_LED_WARN, HIGH);
      digitalWrite(PIN_BUZZER, HIGH);
      delay(80);
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case STATUS_CAUTION:
      digitalWrite(PIN_LED_WARN, !digitalRead(PIN_LED_WARN));
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case STATUS_SAFE:
    default:
      digitalWrite(PIN_LED_WARN, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      break;
  }
}

/**
 * @brief Updates 16x2 I2C LCD with a 2-page rotating dashboard.
 */
void updateLCD(const WaterMetrics &metrics) {
  lcd.clear();

  if (lcdDisplayMode == 0) {
    // PAGE 1: Quantitative Readings
    lcd.setCursor(0, 0);
    lcd.print("TDS: ");
    lcd.print((int)metrics.tdsPpm);
    lcd.print(" ppm");

    lcd.setCursor(0, 1);
    lcd.print("Turb: ");
    lcd.print(metrics.turbNtu, 1);
    lcd.print(" NTU");
  } else {
    // PAGE 2: Status & Wi-Fi indicator
    lcd.setCursor(0, 0);
    lcd.print("Status: ");
    if (metrics.status == STATUS_SAFE)        lcd.print("SAFE");
    else if (metrics.status == STATUS_CAUTION) lcd.print("CAUTION");
    else                                      lcd.print("ALERT!");

    lcd.setCursor(0, 1);
    if (WiFi.status() == WL_CONNECTED) {
      lcd.print("Cloud: OK (TB)");
    } else {
      lcd.print("Cloud: No WiFi");
    }
  }
}

/**
 * @brief Prints clean, structured telemetry to the Serial Monitor.
 */
void printSerialTelemetry(const WaterMetrics &metrics) {
  Serial.print(F("[TIME: "));
  Serial.print(millis() / 1000);
  Serial.print(F("s] | TDS: "));
  Serial.print(metrics.tdsPpm, 1);
  Serial.print(F(" ppm | Turb: "));
  Serial.print(metrics.turbNtu, 1);
  Serial.print(F(" NTU | Status: "));

  switch (metrics.status) {
    case STATUS_SAFE:    Serial.println(F("SAFE")); break;
    case STATUS_CAUTION: Serial.println(F("CAUTION")); break;
    case STATUS_ALERT:   Serial.println(F("CRITICAL ALERT")); break;
  }
}

/**
 * @brief Streams telemetry directly to ThingsBoard using HTTP POST.
 * Endpoint: http://demo.thingsboard.io/api/v1/{ACCESS_TOKEN}/telemetry
 */
void sendThingsBoardTelemetry(const WaterMetrics &metrics) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[TB_UPLOAD] Wi-Fi not connected. Skipping cloud upload."));
    return;
  }

  HTTPClient http;
  String url = String(TB_SERVER) + "/api/v1/" + String(TB_ACCESS_TOKEN) + "/telemetry";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Determine status string
  String statusStr = "NORMAL";
  if (metrics.status == STATUS_ALERT)   statusStr = "ALERT";
  if (metrics.status == STATUS_CAUTION) statusStr = "WARNING";

  // Construct JSON payload matching dashboard telemetry keys
  String jsonPayload = "{";
  jsonPayload += "\"tds\":" + String(metrics.tdsPpm, 1) + ",";
  jsonPayload += "\"turbidity\":" + String(metrics.turbNtu, 1) + ",";
  jsonPayload += "\"status\":\"" + statusStr + "\"";
  jsonPayload += "}";

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode == 200) {
    Serial.print(F("[TB_UPLOAD] Success! Data sent: "));
    Serial.println(jsonPayload);
  } else {
    Serial.print(F("[TB_UPLOAD] Failed, HTTP error code: "));
    Serial.println(httpResponseCode);
  }

  http.end();
}
