void setup() {
  Serial.begin(9600);
}

void loop() {
  if (Serial.available() > 0) {
    char command = Serial.read();
//    if (command != '\r' && command != '\n') {
//      Serial.print(command, DEC);
//    }

    char data = 54;

    switch (command) {
      case 49: // '1'
        Serial.write('E');
      break;
      case 50: // '2'
        Serial.write(data);
      break;
    }
    Serial.flush();
  }
}
