#include <stdint.h>

#define RCC_BASE        0x40021000UL
#define FLASH_BASE      0x40022000UL

#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_BASE + 0x00))

#define RCC_CR_HSEON    (1U << 16)
#define RCC_CR_HSERDY   (1U << 17)
#define RCC_CR_PLLON    (1U << 24)
#define RCC_CR_PLLRDY   (1U << 25)

void system_clock_init(void)
{
    /*
     * Blue Pill normally has 8 MHz HSE crystal.
     *
     * 8 MHz × 9 = 72 MHz SYSCLK
     */

    RCC_CR |= RCC_CR_HSEON;

    while ((RCC_CR & RCC_CR_HSERDY) == 0) {
    }

    /*
     * Flash:
     * 2 wait states for 72 MHz.
     * Prefetch enable.
     */
    FLASH_ACR = (1U << 4) | 2U;

    /*
     * RCC_CFGR:
     *
     * HPRE  = /1
     * PPRE1 = /2  -> APB1 = 36 MHz
     * PPRE2 = /1  -> APB2 = 72 MHz
     *
     * PLL source = HSE
     * PLL multiplier = x9
     */

    RCC_CFGR =
        (0U << 4)  |      /* AHB /1 */
        (4U << 8)  |      /* APB1 /2 */
        (0U << 11) |      /* APB2 /1 */
        (1U << 16) |      /* PLL source = HSE */
        (7U << 18);       /* PLL x9 */

    RCC_CR |= RCC_CR_PLLON;

    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {
    }

    /*
     * SYSCLK = PLL
     */
    RCC_CFGR &= ~3U;
    RCC_CFGR |= 2U;

    /*
     * Wait until PLL actually becomes SYSCLK.
     */
    while (((RCC_CFGR >> 2) & 3U) != 2U) {
    }
}
