#include <Arduino.h>
void setup() {
  Serial.begin(9600);
  delay(1000); // Allow Serial to initialize
  Serial.println("ESP32-C3 Serial Test");
}

void loop() {
  Serial.println("Loop running");
  delay(1000);
}