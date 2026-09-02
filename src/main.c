#include <stdint.h>
#include "status.h"

#define RCC_BASE        0x40021000UL
#define GPIOC_BASE      0x40011000UL

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define GPIOC_CRH       (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
#define GPIOC_BSRR      (*(volatile uint32_t *)(GPIOC_BASE + 0x10))
#define GPIOC_BRR       (*(volatile uint32_t *)(GPIOC_BASE + 0x14))

#define RCC_APB2ENR_IOPCEN (1U << 4)

void system_clock_init(void);
int can_hw_init(void);
void dronecan_init(void);
void timebase_init(void);
void dronecan_process(void);
uint32_t micros32(void);
void i2c1_init(void);
int pca9685_init(void);



int main(void)
{
    /*
     * Enable GPIOC clock.
     */
     
    system_clock_init();
    timebase_init();
    status_init();
    
    i2c1_init();
    int pca_result = pca9685_init();
    (void)pca_result;

    const int can_result = can_hw_init();

    if (can_result == 0) {
        dronecan_init();
    }
    
    if (pca_result < 0) {
        status_set(STATUS_PCA_ERROR);
    } else if (can_result < 0) {
        status_set(STATUS_CAN_ERROR);
    } else {
        status_set(STATUS_OK);
    }
        


    while (1) {
        
        if (can_result == 0) {
            dronecan_process();
        }
    
        status_process();
        
    }

}
