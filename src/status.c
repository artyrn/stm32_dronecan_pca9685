#include <stdint.h>
#include "status.h"

#define GPIOC_BASE      0x40011000UL
#define RCC_BASE        0x40021000UL

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define GPIOC_CRH       (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
#define GPIOC_BSRR      (*(volatile uint32_t *)(GPIOC_BASE + 0x10))
#define GPIOC_BRR       (*(volatile uint32_t *)(GPIOC_BASE + 0x14))

#define RCC_APB2ENR_IOPCEN (1U << 4)

#define LED_PIN         13U

uint32_t micros32(void);


static system_status_t current_status = STATUS_OK;

static void led_on(void)
{
    /* Blue Pill PC13 LED is active-low */
    GPIOC_BRR = (1U << LED_PIN);
}

static void led_off(void)
{
    GPIOC_BSRR = (1U << LED_PIN);
}

void status_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * PC13 output push-pull, 2 MHz.
     * PC13 = CRH bits 20..23.
     *
     * MODE = 10
     * CNF  = 00
     * nibble = 0x2
     */
    GPIOC_CRH &= ~(0xFU << 20);
    GPIOC_CRH |=  (0x2U << 20);

    led_off();
    current_status = STATUS_OK;
}

void status_set(system_status_t status)
{
    current_status = status;
}

system_status_t status_get(void)
{
    return current_status;
}

void status_process(void)
{
    const uint32_t now_ms = micros32() / 1000U;
    const uint32_t phase = now_ms % 1000U;

    switch (current_status) {

    case STATUS_OK:
        /*
         * Short 100 ms flash every second.
         */
        if (phase < 100U) {
            led_on();
        } else {
            led_off();
        }
        break;

    case STATUS_PCA_ERROR:
        /*
         * Two flashes:
         * 0..100 ON
         * 100..200 OFF
         * 200..300 ON
         * remainder OFF
         */
        if ((phase < 100U) ||
            ((phase >= 200U) && (phase < 300U))) {
            led_on();
        } else {
            led_off();
        }
        break;

    case STATUS_CAN_ERROR:
        /*
         * Three flashes.
         */
        if ((phase < 100U) ||
            ((phase >= 200U) && (phase < 300U)) ||
            ((phase >= 400U) && (phase < 500U))) {
            led_on();
        } else {
            led_off();
        }
        break;

    case STATUS_ACTUATOR_FAILSAFE:
        /*
         * 5 Hz fast blinking.
         */
        if ((now_ms % 200U) < 100U) {
            led_on();
        } else {
            led_off();
        }
        break;

    case STATUS_FATAL:
    default:
        led_on();
        break;
    }
}
