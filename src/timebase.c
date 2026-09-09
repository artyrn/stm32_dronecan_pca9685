#include <stdint.h>

#define RCC_BASE        0x40021000UL
#define TIM2_BASE       0x40000000UL
#define NVIC_ISER0      (*(volatile uint32_t *)0xE000E100UL)

#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define TIM2_CR1        (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_DIER       (*(volatile uint32_t *)(TIM2_BASE + 0x0C))
#define TIM2_SR         (*(volatile uint32_t *)(TIM2_BASE + 0x10))
#define TIM2_EGR        (*(volatile uint32_t *)(TIM2_BASE + 0x14))
#define TIM2_CNT        (*(volatile uint32_t *)(TIM2_BASE + 0x24))
#define TIM2_PSC        (*(volatile uint32_t *)(TIM2_BASE + 0x28))
#define TIM2_ARR        (*(volatile uint32_t *)(TIM2_BASE + 0x2C))

#define RCC_APB1ENR_TIM2EN   (1U << 0)

#define TIM_CR1_CEN          (1U << 0)
#define TIM_DIER_UIE         (1U << 0)
#define TIM_SR_UIF           (1U << 0)

#define TIM2_IRQn            28U

/*
 * STM32F103 TIM2 is 16-bit on this MCU.
 *
 * At 1 MHz it wraps every:
 *
 *     65536 us = 65.536 ms
 *
 * Extend it in software using the update interrupt.
 */
static volatile uint32_t tim2_high = 0U;


void TIM2_IRQHandler(void)
{
    if ((TIM2_SR & TIM_SR_UIF) != 0U) {

        /*
         * Clear update interrupt flag.
         */
        TIM2_SR &= ~TIM_SR_UIF;

        /*
         * One complete 16-bit timer period elapsed.
         */
        tim2_high++;
    }
}


void timebase_init(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * APB1 = 36 MHz.
     * APB1 timer clock = 72 MHz.
     *
     * PSC = 71:
     *
     * 72 MHz / 72 = 1 MHz
     *
     * One count = 1 us.
     */
    TIM2_CR1 = 0U;

    TIM2_PSC = 71U;

    /*
     * STM32F103 TIM2 implemented here is 16-bit.
     */
    TIM2_ARR = 0xFFFFU;

    TIM2_CNT = 0U;
    tim2_high = 0U;

    /*
     * Generate update event so PSC/ARR are loaded.
     */
    TIM2_EGR = 1U;

    /*
     * EGR sets UIF as a side effect.
     * Clear it before enabling interrupts.
     */
    TIM2_SR = 0U;

    /*
     * Enable TIM2 update interrupt.
     */
    TIM2_DIER = TIM_DIER_UIE;

    /*
     * Enable TIM2 IRQ28 in NVIC.
     */
    NVIC_ISER0 = (1U << TIM2_IRQn);

    /*
     * Start counter.
     */
    TIM2_CR1 = TIM_CR1_CEN;
}


uint32_t micros32(void)
{
    uint32_t high_before;
    uint32_t high_after;
    uint32_t low;

    /*
     * tim2_high may change while TIM2_CNT is being read.
     *
     * Repeat the snapshot if the overflow ISR ran between
     * the two high-word reads.
     */
    do {
        high_before = tim2_high;
        low = TIM2_CNT & 0xFFFFU;
        high_after = tim2_high;

    } while (high_before != high_after);

    /*
     * There is a very small interval after TIM2 wrapped but
     * before TIM2_IRQHandler has executed.
     *
     * Account for that pending overflow here.
     */
    if ((TIM2_SR & TIM_SR_UIF) != 0U) {

        high_before++;
        low = TIM2_CNT & 0xFFFFU;
    }

    return (high_before << 16) | low;
}


uint64_t micros64(void)
{
    /*
     * micros32 wraps every ~71.58 minutes.
     * Extend that to 64 bits.
     *
     * micros64() is called frequently by DroneCAN.
     */
    static uint32_t last_low = 0U;
    static uint64_t high32 = 0U;

    const uint32_t current = micros32();

    if (current < last_low) {
        high32 += (1ULL << 32);
    }

    last_low = current;

    return high32 | (uint64_t)current;
}
