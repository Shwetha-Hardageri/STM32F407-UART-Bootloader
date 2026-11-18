#include <SoftwareSerial.h>

SoftwareSerial stm(10, 11);   // RX = D10 (from STM32 PA2), TX = D11 (to divider -> PA3)

void setup() {
  Serial.begin(57600);        // PC <-> Uno
  stm.begin(57600);           // Uno <-> STM32
}

void loop() {
  while (Serial.available()) stm.write(Serial.read());
  while (stm.available())    Serial.write(stm.read());
}