#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define CONFIG_MAGIC            0x524F5645UL  /* "ROVE" */
#define CONFIG_VERSION          1U

#define CONFIG_MAX_OUTPUTS      32U
#define CONFIG_MAX_PCA          2U

#define CONFIG_DEFAULT_NODE_ID      42U
#define CONFIG_DEFAULT_CAN_BITRATE  500000UL
#define CONFIG_DEFAULT_PCA_COUNT    1U
#define CONFIG_DEFAULT_FS_TIMEOUT   300U

#define CONFIG_PWM_MIN_US       500U
#define CONFIG_PWM_MAX_US       2500U
#define CONFIG_PWM_NEUTRAL_US   1500U


typedef enum {
    OUTPUT_DISABLED = 0,
    OUTPUT_PWM      = 1,
    OUTPUT_ON_OFF   = 2,
    OUTPUT_PULSE    = 3
} output_type_t;


typedef enum {
    PULSE_IGNORE  = 0,
    PULSE_RESTART = 1
} pulse_retrigger_t;


/*
 * Для ON/OFF и PULSE:
 *
 * failsafe_state = 0 -> OFF
 * failsafe_state = 1 -> ON
 *
 * Для PWM используется failsafe_pwm_us.
 */
typedef struct {
    uint8_t  type;

    uint16_t on_pwm_us;
    uint16_t off_pwm_us;

    uint16_t failsafe_pwm_us;
    uint8_t  failsafe_state;

    uint32_t pulse_time_ms;
    uint8_t  pulse_retrigger;
} output_config_t;


typedef struct {
    uint32_t magic;
    uint16_t version;

    /* DroneCAN */
    uint8_t  node_id;
    uint32_t can_bitrate;

    /* Hardware */
    uint8_t  pca_count;

    /* Global actuator failsafe */
    uint16_t failsafe_timeout_ms;

    /* OUT01 ... OUT32 */
    output_config_t output[CONFIG_MAX_OUTPUTS];

    /*
     * Используется при сохранении конфигурации во Flash.
     * CRC будет рассчитываться по структуре без этого поля.
     */
    uint32_t crc;
} config_t;


/* Initialise configuration with defaults. */
void config_init(void);

/* Restore defaults in RAM. */
void config_load_defaults(void);

/* Access whole current configuration. */
const config_t *config_get(void);
config_t *config_get_mutable(void);

/* Number of physically configured outputs: PCA_COUNT * 16. */
uint8_t config_get_output_count(void);

/* Output configuration. Index is 0...31. */
const output_config_t *config_get_output(uint8_t index);
output_config_t *config_get_output_mutable(uint8_t index);

/* Validate current configuration. Returns 0 if valid. */
int config_validate(void);

/*
 * Save current RAM configuration to Flash.
 * Returns 0 on success.
 */
int config_save(void);

/*
 * Reload configuration from Flash.
 * Returns 0 if valid configuration was loaded.
 */
int config_reload(void);

#endif
