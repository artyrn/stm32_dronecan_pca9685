#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

/*
 * Status flags.
 */
#define STATUS_FLAG_CAN_ERROR      (1U << 0)
#define STATUS_FLAG_PCA0_ERROR     (1U << 1)
#define STATUS_FLAG_PCA1_ERROR     (1U << 2)
#define STATUS_FLAG_FAILSAFE       (1U << 3)
#define STATUS_FLAG_OLED_ERROR     (1U << 4)
#define STATUS_FLAG_FATAL          (1U << 15)

void status_init(void);
void status_process(void);

void status_set_flags(uint16_t flags);
void status_clear_flags(uint16_t flags);
uint16_t status_get_flags(void);

#endif
