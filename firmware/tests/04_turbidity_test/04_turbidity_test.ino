#define PIN_TURBIDITY_ADC 35

// Divider factor: R1 = 10k, R2 = 22k -> V_adc = V_sensor * (22 / (10 + 22))
// Reconstruct V_sensor: V_sensor = V_adc * (32 / 22) = V_adc * 1.4545
const float TURB_DIVIDER_RATIO = (22.0f + 10.0f) / 22.0f;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TURBIDITY_ADC, INPUT);
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  Serial.println("Turbidity Sensor Raw ADC and Scaled Voltage Monitor");
}

void loop() {
  uint32_t mvSum = 0;
  const int samples = 30;

  for (int i = 0; i < samples; i++) {
    mvSum += analogReadMilliVolts(PIN_TURBIDITY_ADC);
    delayMicroseconds(200);
  }

  float adcV = (float)mvSum / (samples * 1000.0f);
  float reconstructedSensorV = adcV * TURB_DIVIDER_RATIO;

  // Approximate NTU
  float ntu = 0.0f;
  if (reconstructedSensorV >= 4.10f) {
    ntu = 0.0f;
  } else if (reconstructedSensorV <= 2.50f) {
    ntu = 3000.0f;
  } else {
    ntu = -1120.4f * pow(reconstructedSensorV, 2) + 5742.3f * reconstructedSensorV - 4352.9f;
  }
  if (ntu < 0) ntu = 0;

  Serial.printf("ADC Pin V: %1.3f V | Reconstructed Sensor V: %1.3f V | Est. Turbidity: %4.1f NTU\n", 
                adcV, reconstructedSensorV, ntu);
  delay(1000);
}
