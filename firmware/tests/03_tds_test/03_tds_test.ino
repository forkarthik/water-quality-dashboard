#define PIN_TDS_ADC 34

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TDS_ADC, INPUT);
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  Serial.println("TDS Sensor Raw ADC and Millivolt Monitor");
}

void loop() {
  uint32_t rawSum = 0;
  uint32_t mvSum = 0;
  const int samples = 30;

  for (int i = 0; i < samples; i++) {
    rawSum += analogRead(PIN_TDS_ADC);
    mvSum += analogReadMilliVolts(PIN_TDS_ADC);
    delayMicroseconds(200);
  }

  float avgRaw = (float)rawSum / samples;
  float avgMv  = (float)mvSum / samples;
  float voltage = avgMv / 1000.0f;

  // Approximate TDS calculation at 25 deg C
  float tdsPpm = (133.42f * pow(voltage, 3) - 255.86f * pow(voltage, 2) + 857.39f * voltage) * 0.5f;
  if (tdsPpm < 0) tdsPpm = 0;

  Serial.printf("Raw ADC: %4.0f | Voltage: %1.3f V | Est. TDS: %4.1f ppm\n", avgRaw, voltage, tdsPpm);
  delay(1000);
}
