#include <stdint.h>

#include "board.h"
#include "pca9685.h"
#include "i2c.h"
#include <unistd.h>

#define PCA9685_MODE1           0x00U
#define PCA9685_MODE2           0x01U
#define PCA9685_LED0_ON_L       0x06U
#define PCA9685_PRESCALE        0xFEU

#define PCA9685_MODE1_SLEEP     0x10U
#define PCA9685_MODE1_AI        0x20U
#define PCA9685_MODE1_RESTART   0x80U

#define PCA9685_MODE2_OUTDRV    0x04U

#define PCA9685_PRESCALE_50HZ   121U



/*
 * Convert logical PCA number to I2C address.
 *
 * device 0 -> 0x40
 * device 1 -> 0x41
 */
uint8_t pca9685_get_address(uint8_t device)
{
    if (device == 0U) {
        return PCA9685_0_ADDRESS;
    }

    if (device == 1U) {
        return PCA9685_1_ADDRESS;
    }

    return 0U;
}


int pca9685_set_pwm_us(uint8_t device,
                       uint8_t channel,
                       uint16_t pulse_us)
{
    if (device >= PCA9685_DEVICE_COUNT) {
        return -1;
    }

    if (channel >= PCA9685_CHANNELS_PER_DEVICE) {
        return -2;
    }

    const uint8_t address =
        pca9685_get_address(device);

    if (address == 0U) {
        return -3;
    }

    /*
     * Servo/PWM safety limits.
     */
    if (pulse_us < 500U) {
        pulse_us = 500U;
    }

    if (pulse_us > 2500U) {
        pulse_us = 2500U;
    }

    /*
     * PCA9685 runs at 50 Hz.
     *
     * Period = 20,000 us.
     * One period = 4096 counts.
     */
    const uint32_t counts =
        ((uint32_t)pulse_us * 4096U) /
        20000U;

    const uint8_t base =
        (uint8_t)(
            PCA9685_LED0_ON_L +
            (4U * channel));

    /*
    * PCA9685 auto-increment is enabled during init,
    * therefore all four channel registers can be
    * updated in one I2C transaction:
    *
    * LEDn_ON_L
    * LEDn_ON_H
    * LEDn_OFF_L
    * LEDn_OFF_H
    */
    const uint8_t data[4] = {
        0U,
        0U,
        (uint8_t)(counts & 0xFFU),
        (uint8_t)((counts >> 8) & 0x0FU)
    };

    return i2c1_write(
        address,
        base,
        data,
        sizeof(data));

}


int pca9685_init(uint8_t device)
{
    if (device >= PCA9685_DEVICE_COUNT) {
        return -1;
    }

    const uint8_t address =
        pca9685_get_address(device);

    if (address == 0U) {
        return -2;
    }

    int result;

    /*
     * Sleep before changing PRESCALE.
     */
    result = i2c1_write_reg(
        address,
        PCA9685_MODE1,
        PCA9685_MODE1_SLEEP);

    if (result < 0) {
        return result;
    }

    /*
     * 25 MHz oscillator, approximately 50 Hz.
     */
    result = i2c1_write_reg(
        address,
        PCA9685_PRESCALE,
        PCA9685_PRESCALE_50HZ);

    if (result < 0) {
        return result;
    }

    /*
     * Totem-pole outputs.
     */
    result = i2c1_write_reg(
        address,
        PCA9685_MODE2,
        PCA9685_MODE2_OUTDRV);

    if (result < 0) {
        return result;
    }

    /*
     * Wake oscillator and enable register
     * auto-increment.
     */
    result = i2c1_write_reg(
        address,
        PCA9685_MODE1,
        PCA9685_MODE1_AI);

    if (result < 0) {
        return result;
    }

    /*
     * PCA9685 oscillator startup time after SLEEP
     * is cleared. Give it a little margin.
     */
    usleep(600U);

     /*
     * Restart oscillator and keep
     * auto-increment enabled.
     */
     result = i2c1_write_reg(
        address,
        PCA9685_MODE1,
        PCA9685_MODE1_RESTART |
        PCA9685_MODE1_AI);
    if (result < 0) {
        return result;
    }

    return 0;
}


int pca9685_recover(uint8_t device)
{
    return pca9685_init(device);
}
