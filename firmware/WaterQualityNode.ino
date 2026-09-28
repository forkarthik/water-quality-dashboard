/**
 * =========================================================================================
 * Project: IoT-Based Water Quality Monitoring & Early-Warning System (Rural Community Prototype)
 * Target MCU: ESP32 Development Board (NodeMCU / ESP32-WROOM-32)
 * Core Sensors: Analog TDS Sensor, Analog Turbidity Sensor
 * User Interface: 16x2 I2C LCD, Warning Red LED, NPN-Driven Alert Buzzer
 * Future Expansion: SIM7600E-H 4G Cellular Telemetry, Cloud MQTT/HTTP, SMS Alerts
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
// 2. OPERATIONAL & CALIBRATION CONSTANTS
// =========================================================================================
// Electrical & ADC Constants
const float ADC_REF_VOLTAGE        = 3.30f;  // Nominal ESP32 VDD in Volts
const int   ADC_RESOLUTION         = 4095;   // 12-bit ADC max counts
const int   SAMPLE_SAMPLES_COUNT   = 30;     // Multisampling window for noise filtering

// Voltage Divider Factor for Turbidity Sensor:
// Sensor Vout (up to 4.5V) is divided by R1 = 10k, R2 = 22k -> V_adc = V_sensor * (22 / (10 + 22)) = 0.6875
// To reconstruct true sensor voltage: V_sensor = V_adc / (22.0 / (10.0 + 22.0)) = V_adc * 1.4545
const float TURBIDITY_DIVIDER_RATIO = (22.0f + 10.0f) / 22.0f; // Multiplier ~ 1.4545

// Temperature baseline (since no DS18B20 is fitted, assume standard lab baseline 25.0 C)
const float DEFAULT_TEMPERATURE    = 25.0f;

// Calibration Multipliers & Offsets (Update these using standard buffer testing)
const float TDS_CALIBRATION_FACTOR = 0.5f;   // Standard conversion ratio: TDS (ppm) ≈ EC (uS/cm) * 0.5

// Water Quality Screening Thresholds (Derived from WHO & Indian Standard IS 10500:2012 guidelines)
// Note: These are physical-chemical screening thresholds, not microbiological assurances.
const float TDS_ACCEPTABLE_MAX     = 300.0f; // ppm (Desirable drinking limit)
const float TDS_PERMISSIBLE_MAX    = 500.0f; // ppm (Permissible upper threshold in absence of alternate source)

const float TURB_ACCEPTABLE_MAX    = 1.0f;   // NTU (Desirable limit for clear water)
const float TURB_PERMISSIBLE_MAX   = 5.0f;   // NTU (Maximum permissible limit)

// System Timing Configuration (Non-blocking Millis Loop)
const unsigned long SENSOR_READ_INTERVAL_MS = 1500; // Sensor sampling rate: 1.5 seconds
const unsigned long LCD_PAGE_INTERVAL_MS    = 3000; // Alternate between parameter views
unsigned long lastSensorReadTime            = 0;
unsigned long lastLcdSwitchTime             = 0;
int lcdDisplayMode                          = 0;    // 0: Readings view, 1: Diagnostic/Status view

// =========================================================================================
// 3. ENUMERATIONS & SYSTEM STATE
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
// 4. FUNCTION DECLARATIONS (MODULAR ARCHITECTURE)
// =========================================================================================
void initHardware();
void readTDS(WaterMetrics &metrics);
void readTurbidity(WaterMetrics &metrics);
void checkThresholds(WaterMetrics &metrics);
void controlAlerts(const WaterMetrics &metrics);
void updateLCD(const WaterMetrics &metrics);
void printSerialTelemetry(const WaterMetrics &metrics);
float readAveragedAdcMilliVolts(uint8_t pin, int samples);

// Future Expansion Hooks:
void transmitTelemetry4G(const WaterMetrics &metrics); // Prepared for SIM7600E-H UART

// =========================================================================================
// 5. ARDUINO SETUP
// =========================================================================================
void setup() {
  // Initialize Serial Interface for Telemetry & Diagnostics
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
  lcd.print("System Init...");
  delay(1800);
  lcd.clear();
}

// =========================================================================================
// 6. ARDUINO MAIN LOOP (Non-Blocking Cooperative Scheduling)
// =========================================================================================
void loop() {
  unsigned long currentMillis = millis();

  // Periodic Sensor Acquisition & Processing
  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadTime = currentMillis;

    // Step A: Acquire physical parameters
    readTDS(currentMetrics);
    readTurbidity(currentMetrics);

    // Step B: Evaluate risk matrix
    checkThresholds(currentMetrics);

    // Step C: Trigger visual & acoustic safety alerts
    controlAlerts(currentMetrics);

    // Step D: Output formatted stream to serial console
    printSerialTelemetry(currentMetrics);

    // Step E: Expansion hook (future SIM7600E-H cloud transmission)
    // transmitTelemetry4G(currentMetrics);
  }

  // Periodic LCD Display Multiplexing
  if (currentMillis - lastLcdSwitchTime >= LCD_PAGE_INTERVAL_MS) {
    lastLcdSwitchTime = currentMillis;
    lcdDisplayMode = !lcdDisplayMode; // Toggle display view
    updateLCD(currentMetrics);
  }
}

// =========================================================================================
// 7. MODULAR IMPLEMENTATIONS
// =========================================================================================

/**
 * @brief Initializes GPIO modes, ADC configuration, and I2C LCD.
 */
void initHardware() {
  // GPIO Actuator Setup
  pinMode(PIN_LED_WARN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_WARN, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // ADC Pin Setup (ESP32 ADC pins 34 and 35 are input-only)
  pinMode(PIN_TDS_ADC, INPUT);
  pinMode(PIN_TURBIDITY_ADC, INPUT);

  // Configure ADC attenuation (11dB provides full-scale range ~0 to 3.1V - 3.3V)
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  // Initialize I2C Bus & LCD
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
 * @brief Reads averaged analog voltage using ESP32 calibrated API where available,
 *        applying a software multisampling low-pass filter to dampen electrical ripple.
 */
float readAveragedAdcMilliVolts(uint8_t pin, int samples) {
  uint32_t totalMv = 0;
  for (int i = 0; i < samples; i++) {
    // analogReadMilliVolts() utilizes factory eFuse calibration curves on ESP32
    totalMv += analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  return (float)totalMv / (float)samples;
}

/**
 * @brief Acquires TDS sensor voltage and calculates approximate ppm.
 * Formula models standard electrical conductivity to dissolved solids conversion.
 */
void readTDS(WaterMetrics &metrics) {
  // Sample analog millivolts and convert to Volts
  float voltageMv = readAveragedAdcMilliVolts(PIN_TDS_ADC, SAMPLE_SAMPLES_COUNT);
  float voltage = voltageMv / 1000.0f;
  metrics.tdsRawVoltage = voltage;

  // Temperature compensation formula (baseline 25°C):
  // Compensation Coefficient = 1.0 + 0.02 * (T - 25.0)
  float compensationCoefficient = 1.0f + 0.02f * (DEFAULT_TEMPERATURE - 25.0f);
  float compensationVoltage = voltage / compensationCoefficient;

  // Polynomial curve for standard gravity analog TDS meter:
  // TDS = (133.42 * V^3 - 255.86 * V^2 + 857.39 * V) * 0.5
  float calculatedTds = (133.42f * pow(compensationVoltage, 3) 
                       - 255.86f * pow(compensationVoltage, 2) 
                       + 857.39f * compensationVoltage) * TDS_CALIBRATION_FACTOR;

  // Clamp non-physical negative values or open-circuit floating states
  if (calculatedTds < 0.0f) calculatedTds = 0.0f;
  metrics.tdsPpm = calculatedTds;
}

/**
 * @brief Acquires Turbidity sensor voltage and computes approximate NTU.
 * Handles the external 10k/22k voltage divider reconstruction.
 */
void readTurbidity(WaterMetrics &metrics) {
  // Sample ADC input pin
  float adcMv = readAveragedAdcMilliVolts(PIN_TURBIDITY_ADC, SAMPLE_SAMPLES_COUNT);
  float adcV = adcMv / 1000.0f;

  // Reconstruct true sensor output voltage prior to the 10k/22k divider:
  float sensorVoltage = adcV * TURBIDITY_DIVIDER_RATIO;
  metrics.turbRawVoltage = sensorVoltage;

  // Standard Optical Turbidity curve approximation:
  // Most commercial modules (e.g., TS-300B) output ~4.1V - 4.5V in clear water (0 NTU),
  // with voltage dropping as particulate scatter increases.
  // Standard polynomial: NTU = -1120.4 * V^2 + 5742.3 * V - 4352.9
  float ntu = 0.0f;
  if (sensorVoltage >= 4.10f) {
    ntu = 0.0f; // Clear baseline
  } else if (sensorVoltage <= 2.50f) {
    ntu = 3000.0f; // Very high turbidity / heavy mud
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
  // Critical Condition: If either parameter exceeds maximum permissible levels
  if (metrics.tdsPpm > TDS_PERMISSIBLE_MAX || metrics.turbNtu > TURB_PERMISSIBLE_MAX) {
    metrics.status = STATUS_ALERT;
  }
  // Caution Condition: If parameters exceed ideal baseline but stay within permissible limits
  else if (metrics.tdsPpm > TDS_ACCEPTABLE_MAX || metrics.turbNtu > TURB_ACCEPTABLE_MAX) {
    metrics.status = STATUS_CAUTION;
  }
  // Normal Condition: Optimal physical parameters
  else {
    metrics.status = STATUS_SAFE;
  }
}

/**
 * @brief Actuates Red LED and Buzzer based on evaluated status.
 */
void controlAlerts(const WaterMetrics &metrics) {
  switch (metrics.status) {
    case STATUS_ALERT:
      // High alert: Solid Red LED + Rapid Beeping via 2N2222 Driver
      digitalWrite(PIN_LED_WARN, HIGH);
      
      // Audible pulse (non-blocking simulation or short burst)
      digitalWrite(PIN_BUZZER, HIGH);
      delay(80);
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case STATUS_CAUTION:
      // Caution: Slow blink LED, no audible alarm to prevent fatigue
      digitalWrite(PIN_LED_WARN, !digitalRead(PIN_LED_WARN));
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case STATUS_SAFE:
    default:
      // Safe: Indicators inactive
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
    // PAGE 2: Health Screening Status & Voltages
    lcd.setCursor(0, 0);
    lcd.print("Status: ");
    if (metrics.status == STATUS_SAFE) {
      lcd.print("SAFE");
    } else if (metrics.status == STATUS_CAUTION) {
      lcd.print("CAUTION");
    } else {
      lcd.print("ALERT!");
    }

    lcd.setCursor(0, 1);
    lcd.print("V:");
    lcd.print(metrics.tdsRawVoltage, 2);
    lcd.print("V ");
    lcd.print(metrics.turbRawVoltage, 2);
    lcd.print("V");
  }
}

/**
 * @brief Prints clean, structured telemetry to the Serial Monitor for analysis.
 */
void printSerialTelemetry(const WaterMetrics &metrics) {
  Serial.print(F("[TIME: "));
  Serial.print(millis() / 1000);
  Serial.print(F("s] | TDS: "));
  Serial.print(metrics.tdsPpm, 1);
  Serial.print(F(" ppm (V="));
  Serial.print(metrics.tdsRawVoltage, 3);
  Serial.print(F("V) | Turb: "));
  Serial.print(metrics.turbNtu, 1);
  Serial.print(F(" NTU (V="));
  Serial.print(metrics.turbRawVoltage, 3);
  Serial.print(F("V) | Status: "));

  switch (metrics.status) {
    case STATUS_SAFE:    Serial.println(F("SAFE (Normal)")); break;
    case STATUS_CAUTION: Serial.println(F("CAUTION (Elevated)")); break;
    case STATUS_ALERT:   Serial.println(F("CRITICAL ALERT (Threshold Exceeded)")); break;
  }
}

// =========================================================================================
// 8. FUTURE EXPANSION STUB: SIM7600E-H 4G / CLOUD TELEMETRY
// =========================================================================================
/**
 * @brief Placeholder stub for future SIM7600E-H AT-command or MQTT publishing.
 * When the 4G module is integrated:
 * 1. Initialize HardwareSerial(1) or (2) with RX/TX pins.
 * 2. Send AT commands or use TinyGSM client for HTTP POST / MQTT publish to cloud dashboard.
 * 3. Send SMS if metrics.status == STATUS_ALERT.
 */
void transmitTelemetry4G(const WaterMetrics &metrics) {
  // Future implementation:
  // e.g., SerialAT.printf("AT+CMGS=\"+91XXXXXXXXXX\"\r\n");
  // e.g., mqttClient.publish("rural_water/station_01", payloadJson);
}
