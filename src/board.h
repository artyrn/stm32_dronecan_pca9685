#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/*
 * STM32 DroneCAN PCA9685 board
 * Hardware/Firmware version 1.0
 *
 * CAN:
 *   PA11 - CAN_RX
 *   PA12 - CAN_TX
 *
 * I2C1:
 *   PB6 - SCL
 *   PB7 - SDA
 *
 * LEDs from schematic:
 *   PB4  - LED0 / RUN
 *   PB3  - LED1 / FAILSAFE
 *   PA15 - LED2 / ERROR
 *
 * PA15/PB3/PB4 are JTAG pins after reset.
 * Firmware will disable JTAG but keep SWD enabled.
 */

#define FW_VERSION_MAJOR       1U
#define FW_VERSION_MINOR       0U

#define DRONECAN_NODE_ID       42U

/*
 * PCA9685
 */
#define PCA9685_DEVICE_COUNT           2U
#define PCA9685_CHANNELS_PER_DEVICE   16U

#define PCA9685_0_ADDRESS       0x40U
#define PCA9685_1_ADDRESS       0x41U

#define ACTUATOR_COUNT          32U

/*
 * OLED SSD1306 128x32
 */
#define OLED_I2C_ADDRESS        0x3CU
#define OLED_WIDTH              128U
#define OLED_HEIGHT             32U

/*
 * Safety
 */
#define ACTUATOR_FAILSAFE_US        300000U
#define ACTUATOR_SAFE_PWM_US        1500U

/*
 * Recovery
 */
#define PCA_RECOVERY_INTERVAL_US   1000000U

/*
 * OLED refresh
 */
#define OLED_REFRESH_INTERVAL_US    500000U

#endif
