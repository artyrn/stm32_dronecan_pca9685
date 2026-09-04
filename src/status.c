#include <stdint.h>

#include "status.h"

#define RCC_BASE        0x40021000UL
#define AFIO_BASE       0x40010000UL
#define GPIOA_BASE      0x40010800UL
#define GPIOB_BASE      0x40010C00UL

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))

#define AFIO_MAPR       (*(volatile uint32_t *)(AFIO_BASE + 0x04))

#define GPIOA_CRL       (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_CRH       (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_BSRR      (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

#define GPIOB_CRL       (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_BSRR      (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

#define RCC_APB2ENR_AFIOEN    (1U << 0)
#define RCC_APB2ENR_IOPAEN    (1U << 2)
#define RCC_APB2ENR_IOPBEN    (1U << 3)

/*
 * AFIO_MAPR SWJ_CFG bits 26:24
 *
 * 010 = JTAG disabled, SWD enabled.
 */
#define AFIO_MAPR_SWJ_CFG_MASK       (7U << 24)
#define AFIO_MAPR_SWJ_CFG_SWD_ONLY   (2U << 24)

/*
 * LEDs:
 *
 * PB4  - RUN
 * PB3  - FAILSAFE
 * PA15 - ERROR
 *
 * Schematic is active-high:
 * GPIO -> resistor -> LED -> GND
 */
#define LED_RUN_PIN       4U
#define LED_FAILSAFE_PIN  3U
#define LED_ERROR_PIN     15U

uint32_t micros32(void);

static uint16_t status_flags = 0U;


static void gpio_set(volatile uint32_t *bsrr,
                     uint8_t pin,
                     uint8_t on)
{
    if (on != 0U) {
        *bsrr = (1U << pin);
    } else {
        *bsrr = (1U << (pin + 16U));
    }
}


static void led_run_set(uint8_t on)
{
    gpio_set(&GPIOB_BSRR, LED_RUN_PIN, on);
}


static void led_failsafe_set(uint8_t on)
{
    gpio_set(&GPIOB_BSRR, LED_FAILSAFE_PIN, on);
}


static void led_error_set(uint8_t on)
{
    gpio_set(&GPIOA_BSRR, LED_ERROR_PIN, on);
}


void status_init(void)
{
    RCC_APB2ENR |=
        RCC_APB2ENR_AFIOEN |
        RCC_APB2ENR_IOPAEN |
        RCC_APB2ENR_IOPBEN;

    /*
     * Disable JTAG but preserve SWD.
     *
     * This frees:
     * PA15
     * PB3
     * PB4
     *
     * while PA13/PA14 remain available
     * for SWDIO/SWCLK.
     */
    AFIO_MAPR =
        (AFIO_MAPR & ~AFIO_MAPR_SWJ_CFG_MASK) |
        AFIO_MAPR_SWJ_CFG_SWD_ONLY;

    /*
     * PA15 = GPIO output push-pull 2 MHz.
     *
     * PA15 is CRH bits 31:28.
     * MODE = 10
     * CNF  = 00
     * nibble = 0x2
     */
    GPIOA_CRH &= ~(0xFU << 28);
    GPIOA_CRH |=  (0x2U << 28);

    /*
     * PB3/PB4 = GPIO output push-pull 2 MHz.
     */
    GPIOB_CRL &= ~((0xFU << 12) |
                   (0xFU << 16));

    GPIOB_CRL |=  ((0x2U << 12) |
                   (0x2U << 16));

    led_run_set(0U);
    led_failsafe_set(0U);
    led_error_set(0U);

    status_flags = 0U;
}


void status_set_flags(uint16_t flags)
{
    status_flags |= flags;
}


void status_clear_flags(uint16_t flags)
{
    status_flags &= (uint16_t)(~flags);
}


uint16_t status_get_flags(void)
{
    return status_flags;
}


void status_process(void)
{
    const uint32_t now_ms =
        micros32() / 1000U;

    /*
     * RUN LED:
     *
     * 100 ms ON every second.
     */
    if ((now_ms % 1000U) < 100U) {
        led_run_set(1U);
    } else {
        led_run_set(0U);
    }

    /*
     * FAILSAFE LED:
     * solid while actuator failsafe is active.
     */
    if ((status_flags &
         STATUS_FLAG_FAILSAFE) != 0U) {

        led_failsafe_set(1U);

    } else {

        led_failsafe_set(0U);
    }

    /*
     * ERROR LED:
     * any hardware/software fault.
     */
    if ((status_flags &
        (STATUS_FLAG_CAN_ERROR |
         STATUS_FLAG_PCA0_ERROR |
         STATUS_FLAG_PCA1_ERROR |
         STATUS_FLAG_OLED_ERROR |
         STATUS_FLAG_FATAL)) != 0U) {

        led_error_set(1U);

    } else {

        led_error_set(0U);
    }
}
