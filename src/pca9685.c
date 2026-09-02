#include <stdint.h>

#define PCA9685_ADDRESS     0x40U

#define PCA9685_MODE1       0x00U
#define PCA9685_MODE2       0x01U
#define PCA9685_PRESCALE    0xFEU

#define PCA9685_MODE1_SLEEP 0x10U
#define PCA9685_MODE1_AI    0x20U
#define PCA9685_MODE1_RESTART 0x80U

#define PCA9685_MODE2_OUTDRV 0x04U

#define PCA9685_LED0_ON_L   0x06U

int i2c1_write_reg(uint8_t address,
                   uint8_t reg,
                   uint8_t value);


#define PCA9685_LED0_ON_L   0x06U

int pca9685_set_pwm_us(uint8_t channel, uint16_t pulse_us)
{
    if (channel >= 16U) {
        return -1;
    }

    if (pulse_us < 500U) {
        pulse_us = 500U;
    }

    if (pulse_us > 2500U) {
        pulse_us = 2500U;
    }

    const uint32_t counts =
        ((uint32_t)pulse_us * 4096U) / 20000U;

    const uint8_t base =
        (uint8_t)(PCA9685_LED0_ON_L + (4U * channel));

    int result;

    /*
     * ON = 0
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        base + 0U,
        0U);

    if (result < 0) {
        return result;
    }

    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        base + 1U,
        0U);

    if (result < 0) {
        return result;
    }

    /*
     * OFF = counts
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        base + 2U,
        (uint8_t)(counts & 0xFFU));

    if (result < 0) {
        return result;
    }

    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        base + 3U,
        (uint8_t)((counts >> 8) & 0x0FU));

    if (result < 0) {
        return result;
    }

    return 0;
}

int pca9685_init(void)
{
    int result;

    /*
     * Put PCA9685 into sleep before changing PRESCALE.
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        PCA9685_MODE1,
        PCA9685_MODE1_SLEEP);

    if (result < 0) {
        return result;
    }

    /*
     * 25 MHz oscillator, approximately 50 Hz:
     *
     * prescale =
     * 25,000,000 / (4096 * 50) - 1
     * ~= 121
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        PCA9685_PRESCALE,
        121U);

    if (result < 0) {
        return result;
    }

    /*
     * Totem-pole outputs.
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        PCA9685_MODE2,
        PCA9685_MODE2_OUTDRV);

    if (result < 0) {
        return result;
    }

    /*
     * Wake up + auto increment.
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        PCA9685_MODE1,
        PCA9685_MODE1_AI);

    if (result < 0) {
        return result;
    }

    /*
     * RESTART + auto increment.
     */
    result = i2c1_write_reg(
        PCA9685_ADDRESS,
        PCA9685_MODE1,
        PCA9685_MODE1_RESTART |
        PCA9685_MODE1_AI);

    if (result < 0) {
        return result;
    }

    return 0;
}
