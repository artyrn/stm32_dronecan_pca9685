#ifndef I2C_H
#define I2C_H

#include <stdint.h>

void i2c_init(void);
void i2c_recover(void);

int i2c_write(uint8_t address,
              uint8_t reg,
              const uint8_t *data,
              uint16_t length);

int i2c_write_reg(uint8_t address,
                  uint8_t reg,
                  uint8_t value);

int i2c_probe(uint8_t address);

#endif
