#include <stdint.h>
#include <unistd.h>

/*
 * Very simple blocking delay for libcanard STM32 driver.
 *
 * CPU clock = 72 MHz.
 *
 * Accuracy is not critical here because libcanard uses this
 * only while waiting for bxCAN initialization state changes.
 */

int usleep(useconds_t usec)
{
    while (usec--) {
        /*
         * Approximately 1 us at 72 MHz.
         *
         * This is deliberately simple for the first version.
         */
        for (volatile uint32_t i = 0; i < 18U; i++) {
            __asm volatile ("nop");
        }
    }

    return 0;
}
