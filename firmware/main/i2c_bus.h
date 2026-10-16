// the one I2C bus of a board: the round one's IO expander and touch chip, the 3.5" C's touch
#pragma once
#include "board.h"
#ifdef PIN_I2C_SDA
#include "driver/i2c_master.h"

// a device on the bus at 400 kHz, the bus comes up on the first call
i2c_master_dev_handle_t i2c_dev(uint8_t addr);
// 1 if something acks at addr
int i2c_probe(uint8_t addr);
// take the bus down and give the pins back (only when no device was added)
void i2c_bus_free(void);
#endif
