#define PIN_LED_WARN 25  // Through 220R resistor to LED Anode
#define PIN_BUZZER   19  // Through 1k resistor to 2N2222 Base

void setup() {
  Serial.begin(115200);
  Serial.println("Testing LED and 2N2222 Buzzer Driver...");

  pinMode(PIN_LED_WARN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
}

void loop() {
  Serial.println("Activating Alert (LED ON, Buzzer Beep)...");
  digitalWrite(PIN_LED_WARN, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(150); // Short chirp

  digitalWrite(PIN_BUZZER, LOW);
  delay(850); // Keep LED on for remaining duration

  Serial.println("Deactivating Alert (LED OFF, Buzzer OFF)...");
  digitalWrite(PIN_LED_WARN, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  delay(2000);
}
