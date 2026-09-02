#include <stdint.h>

#define RCC_BASE        0x40021000UL
#define TIM2_BASE       0x40000000UL

#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define TIM2_CR1        (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_EGR        (*(volatile uint32_t *)(TIM2_BASE + 0x14))
#define TIM2_CNT        (*(volatile uint32_t *)(TIM2_BASE + 0x24))
#define TIM2_PSC        (*(volatile uint32_t *)(TIM2_BASE + 0x28))
#define TIM2_ARR        (*(volatile uint32_t *)(TIM2_BASE + 0x2C))

#define RCC_APB1ENR_TIM2EN (1U << 0)

static uint32_t last_counter = 0U;
static uint64_t high_part = 0U;

void timebase_init(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * APB1 = 36 MHz.
     * TIM2 clock = 72 MHz.
     *
     * PSC = 71 -> 1 MHz counter.
     * 1 count = 1 us.
     */
    TIM2_PSC = 71U;
    TIM2_ARR = 0xFFFFFFFFU;

    TIM2_EGR = 1U;
    TIM2_CNT = 0U;

    last_counter = 0U;
    high_part = 0U;

    TIM2_CR1 = 1U;
}

uint32_t micros32(void)
{
    return TIM2_CNT;
}

uint64_t micros64(void)
{
    const uint32_t current = TIM2_CNT;

    /*
     * Detect 32-bit TIM2 wrap.
     */
    if (current < last_counter) {
        high_part += (1ULL << 32);
    }

    last_counter = current;

    return high_part | current;
}
