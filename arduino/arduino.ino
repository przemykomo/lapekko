#include "../common.h"

void setup() {
  Serial.begin(9600);
}

void loop() {
  if (Serial.available() > 0) {
    char command = Serial.read();
    BATTERY_DATA_T data = 70;

    switch (command) {
      case '1':
        Serial.write(data);
      break;
      case '2':
        Serial.write(data);
      break;
    }
    Serial.flush();
  }
}
