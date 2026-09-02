#include <stdint.h>

#include "canard_stm32.h"

/*
 * STM32F103C8T6
 *
 * CAN1:
 *   PA11 = CAN_RX
 *   PA12 = CAN_TX
 *
 * PCLK1 = 36 MHz
 * CAN bitrate = 500 kbit/s
 */

#define RCC_BASE            0x40021000UL
#define GPIOA_BASE          0x40010800UL

#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define GPIOA_CRH           (*(volatile uint32_t *)(GPIOA_BASE + 0x04))

#define RCC_APB2ENR_AFIOEN  (1U << 0)
#define RCC_APB2ENR_IOPAEN  (1U << 2)

#define RCC_APB1ENR_CAN1EN  (1U << 25)

#define CAN_PCLK_HZ         36000000UL
#define CAN_BITRATE         500000UL


int can_hw_init(void)
{
    /*
     * Enable AFIO and GPIOA clocks.
     */
    RCC_APB2ENR |= RCC_APB2ENR_AFIOEN |
                   RCC_APB2ENR_IOPAEN;

    /*
     * PA11 = CAN_RX
     *
     * Input floating:
     * MODE11 = 00
     * CNF11  = 01
     *
     * CRH bits 15:12.
     */
    GPIOA_CRH &= ~(0xFU << 12);
    GPIOA_CRH |=  (0x4U << 12);

    /*
     * PA12 = CAN_TX
     *
     * Alternate function push-pull.
     * 50 MHz output:
     *
     * MODE12 = 11
     * CNF12  = 10
     *
     * CRH bits 19:16.
     */
    GPIOA_CRH &= ~(0xFU << 16);
    GPIOA_CRH |=  (0xBU << 16);

    /*
     * Enable CAN1 peripheral clock.
     */
    RCC_APB1ENR |= RCC_APB1ENR_CAN1EN;

    /*
     * Compute bxCAN timings for:
     *
     * PCLK1 = 36 MHz
     * bitrate = 500 kbit/s
     */
    CanardSTM32CANTimings timings;

    int16_t result =
        canardSTM32ComputeCANTimings(
            CAN_PCLK_HZ,
            CAN_BITRATE,
            &timings);

    if (result < 0) {
        return result;
    }

    /*
     * Start CAN1 in normal mode.
     */
    result =
        canardSTM32Init(
            &timings,
            CanardSTM32IfaceModeNormal);

    if (result < 0) {
        return result;
    }

    return 0;
}
