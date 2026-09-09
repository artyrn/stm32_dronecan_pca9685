#include <stdint.h>

#include "board.h"
#include "status.h"
#include "pca9685.h"
#include "i2c.h"
#include "oled.h"
#include "dronecan.h"
#include "can_hw.h"
#include "config.h"

void system_clock_init(void);
void timebase_init(void);


static int pca_init_and_safe(uint8_t device)
{
    int result = pca9685_init(device);

    if (result < 0) {
        return result;
    }

    const uint8_t first_output =
        (uint8_t)(
            device * PCA9685_CHANNELS_PER_DEVICE);

    for (uint8_t channel = 0U;
         channel < PCA9685_CHANNELS_PER_DEVICE;
         channel++) {

        const uint8_t output =
            (uint8_t)(first_output + channel);

        const output_config_t *out =
            config_get_output(output);

        uint16_t safe_pwm =
            CONFIG_PWM_NEUTRAL_US;

        if ((out != 0) &&
            (out->type == OUTPUT_PWM)) {

            safe_pwm =
                out->failsafe_pwm_us;
        }

        result = pca9685_set_pwm_us(
            device,
            channel,
            safe_pwm);

        if (result < 0) {
            return result;
        }
    }

    return 0;
}


int main(void)
{
    system_clock_init();
    timebase_init();
    status_init();
    
    config_init();


    /*
     * I2C2:
     *
     * PB10 = SCL
     * PB11 = SDA
     */
    i2c_init();

    /*
     * Initialize both PCA9685 devices and
     * immediately drive all actuator outputs
     * to their safe PWM value.
     *
     * Missing PCA devices are allowed here.
     * Their error flags will be set below and
     * automatic recovery will retry them later.
     */

     const config_t *cfg = config_get();

     int pca0_result = -1;
     int pca1_result = -1;

     if (cfg->pca_count >= 1U) {
        pca0_result = pca_init_and_safe(0U);
    }

    if (cfg->pca_count >= 2U) {
        pca1_result = pca_init_and_safe(1U);
    }


    /*
     * OLED is non-critical.
     *
     * Initialize it only after actuator outputs
     * have been driven to their safe state.
     */
    (void)oled_init();

    /*
     * Publish startup PCA state before actuator
     * subsystem initialization.
     *
     * dronecan_actuator_init() uses these flags
     * to arm automatic PCA recovery.
     */

     if ((cfg->pca_count >= 1U) && (pca0_result < 0)) {
        status_set_flags(STATUS_FLAG_PCA0_ERROR);
    } else {
        status_clear_flags(STATUS_FLAG_PCA0_ERROR);
    }

    if ((cfg->pca_count >= 2U) && (pca1_result < 0)) {
        status_set_flags(STATUS_FLAG_PCA1_ERROR);
    } else {
        status_clear_flags(STATUS_FLAG_PCA1_ERROR);
    }


    /*
     * Actuator failsafe and PCA recovery must
     * operate independently of CAN availability.
     */
    dronecan_actuator_init();

    /*
     * Initialize bxCAN.
     */
    const int can_result =
        can_hw_init();

    if (can_result == 0) {
        dronecan_init();
    }

    if (can_result < 0) {
        status_set_flags(STATUS_FLAG_CAN_ERROR);
    } else {
        status_clear_flags(STATUS_FLAG_CAN_ERROR);
    }

    /*
     * Main cooperative loop.
     */
    while (1) {

        if (can_result == 0) {
            dronecan_process();
        }

        dronecan_actuator_process();
        oled_process();
        status_process();
    }
}
