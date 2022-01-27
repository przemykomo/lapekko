#ifndef BATTERY_LAPEKKO_COMMON_H
#define BATTERY_LAPEKKO_COMMON_H

#define BATTERY_DATA_T int32_t
#define BATTERY_MAX_CHARGE 6000000

// charge µAh
// energy µWh
// capacity 0-100 (percents)
// voltage µV
// current µA

// Charge now is the same as charge counter, but as an absolute value, I guess?
#define BATTERY_CMD_CHARGE_NOW '1'
#define BATTERY_CMD_VOLTAGE_NOW '2'
#define BATTERY_CMD_CURRENT_NOW '3'

#endif // BATTERY_LAPEKKO_COMMON_H
