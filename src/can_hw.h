#ifndef CAN_HW_H
#define CAN_HW_H

#include <stdint.h>

#define CAN_HW_STATUS_WARNING   (1U << 0)
#define CAN_HW_STATUS_PASSIVE   (1U << 1)
#define CAN_HW_STATUS_BUS_OFF   (1U << 2)

int can_hw_init(void);

uint8_t can_hw_get_status(void);
uint8_t can_hw_get_tec(void);
uint8_t can_hw_get_rec(void);

#endif
