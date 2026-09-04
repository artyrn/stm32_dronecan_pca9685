#ifndef DRONECAN_H
#define DRONECAN_H

#include <stdint.h>

void dronecan_actuator_init(void);
void dronecan_actuator_process(void);

void dronecan_init(void);
void dronecan_process(void);

uint32_t dronecan_get_actuator_rx_count(void);

#endif
