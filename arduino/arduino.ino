#include "../common.h"

BATTERY_DATA_T charge_now = 0; // TODO: update it and store periodically on EEPROM
BATTERY_DATA_T voltage_now = 15000000;
BATTERY_DATA_T current_now = 3000000;

void setup() {
    Serial.begin(9600);
}

void writeData(BATTERY_DATA_T data) {
    byte buf[sizeof(BATTERY_DATA_T)];
    
    for (int i = 0; i < sizeof(buf); i++) {
        buf[i] = (data >> (i * 8)) & 255;
    }

    Serial.write(buf, sizeof(buf));
}

void loop() {
    // arduino nano A5 & A7 ports

    // leonardo
    //voltage_now = (analogRead(A5) * (5.0 / 1023.0) * 4.058) * 1000000;

    // nano needs different scale. I've spent too much time trying to find the best scale...
    voltage_now = (analogRead(A5) * (5.0 / 1023.0) * 3.7) * 1000000;
    current_now = ((analogRead(A7) * (5.0 / 1023.0) - 2.5) / 185.0 / 4.8 * 5.0 * 1000000000.0);
    current_now = abs(current_now);

    while (Serial.available() > 0) {
        char command = Serial.read();

        switch (command) {
        case BATTERY_CMD_CHARGE_NOW:
            writeData(charge_now);
            break;
        case BATTERY_CMD_CHARGE_COUNTER: //TODO: remove I think
            writeData(charge_now);
            break;
        case BATTERY_CMD_VOLTAGE_NOW:
            writeData(voltage_now);
            break;
        case BATTERY_CMD_CURRENT_NOW:
            writeData(current_now);
            break;
        }
        Serial.flush();
    }
}
