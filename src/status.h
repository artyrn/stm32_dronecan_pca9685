#ifndef STATUS_H
#define STATUS_H

typedef enum {
    STATUS_OK = 0,
    STATUS_PCA_ERROR,
    STATUS_CAN_ERROR,
    STATUS_ACTUATOR_FAILSAFE,
    STATUS_FATAL
} system_status_t;

void status_init(void);
void status_set(system_status_t status);
system_status_t status_get(void);
void status_process(void);

#endif
