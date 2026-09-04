#include <stdint.h>

#include "board.h"
#include "status.h"
#include "pca9685.h"
#include "i2c.h"
#include "oled.h"
#include "dronecan.h"
#include "can_hw.h"

void system_clock_init(void);
void timebase_init(void);

static int pca_init_and_safe(uint8_t device)
{
    int result = pca9685_init(device);

    if (result < 0) {
        return result;
    }

    for (uint8_t channel = 0U;
         channel < PCA9685_CHANNELS_PER_DEVICE;
         channel++) {

        result = pca9685_set_pwm_us(
            device,
            channel,
            ACTUATOR_SAFE_PWM_US);

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

    i2c1_init();

    const int pca0_result =
        pca_init_and_safe(0U);

    const int pca1_result =
        pca_init_and_safe(1U);

    /*
    * OLED is non-critical.
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
    if (pca0_result < 0) {
        status_set_flags(STATUS_FLAG_PCA0_ERROR);
    } else {
        status_clear_flags(STATUS_FLAG_PCA0_ERROR);
    }

    if (pca1_result < 0) {
        status_set_flags(STATUS_FLAG_PCA1_ERROR);
    } else {
        status_clear_flags(STATUS_FLAG_PCA1_ERROR);
    }

    dronecan_actuator_init();

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

    while (1) {

        if (can_result == 0) {
            dronecan_process();
        }
        
        dronecan_actuator_process();
        oled_process();
        status_process();
    }
}
