#include "../common.h"

void setup() {
    Serial.begin(9600);
}

BATTERY_DATA_T charge_now = 3000000;
BATTERY_DATA_T charge_counter = 1000000;
BATTERY_DATA_T voltage_now = 15000000;
BATTERY_DATA_T current_now = 3000000;

void writeData(BATTERY_DATA_T data) {
    byte buf[sizeof(BATTERY_DATA_T)];
    
    for (int i = 0; i < sizeof(buf); i++) {
        buf[i] = (data >> (i * 8)) & 255;
    }

    Serial.write(buf, sizeof(buf));
}

void loop() {
    while (Serial.available() > 0) {
        char command = Serial.read();

        switch (command) {
        case BATTERY_CMD_CHARGE_NOW:
            writeData(charge_now);
            break;
        case BATTERY_CMD_CHARGE_COUNTER:
            writeData(charge_counter);
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
