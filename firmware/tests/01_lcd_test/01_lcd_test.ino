#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// Default I2C address is usually 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);
  Serial.println("Testing 16x2 I2C LCD...");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("LCD Test OK!");
  lcd.setCursor(0, 1);
  lcd.print("ESP32 GPIO 21/22");
}

void loop() {
  // Blink backlight to verify I2C communication
  delay(1500);
  lcd.noBacklight();
  delay(500);
  lcd.backlight();
}
