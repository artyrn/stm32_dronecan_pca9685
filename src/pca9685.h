#ifndef PCA9685_H
#define PCA9685_H

#include <stdint.h>

int pca9685_init(uint8_t device);
int pca9685_recover(uint8_t device);

int pca9685_set_pwm_us(uint8_t device,
                       uint8_t channel,
                       uint16_t pulse_us);

uint8_t pca9685_get_address(uint8_t device);

#endif
