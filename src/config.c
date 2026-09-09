#include <stdint.h>

#include "config.h"


#define CONFIG_FLASH_ADDRESS    0x0800FC00UL

#define FLASH_BASE              0x40022000UL

#define FLASH_KEYR              (*(volatile uint32_t *)(FLASH_BASE + 0x04U))
#define FLASH_SR                (*(volatile uint32_t *)(FLASH_BASE + 0x0CU))
#define FLASH_CR                (*(volatile uint32_t *)(FLASH_BASE + 0x10U))
#define FLASH_AR                (*(volatile uint32_t *)(FLASH_BASE + 0x14U))

#define FLASH_SR_BSY            (1U << 0)
#define FLASH_SR_PGERR          (1U << 2)
#define FLASH_SR_WRPRTERR       (1U << 4)
#define FLASH_SR_EOP            (1U << 5)

#define FLASH_CR_PG             (1U << 0)
#define FLASH_CR_PER            (1U << 1)
#define FLASH_CR_STRT           (1U << 6)
#define FLASH_CR_LOCK           (1U << 7)

#define FLASH_KEY1              0x45670123UL
#define FLASH_KEY2              0xCDEF89ABUL


static config_t g_config;


/* ----------------------------------------------------------
 * CRC32
 *
 * Standard CRC-32 polynomial:
 * 0xEDB88320
 * ---------------------------------------------------------- */

static uint32_t config_crc32(
    const uint8_t *data,
    uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < length; i++) {

        crc ^= data[i];

        for (uint8_t bit = 0U;
             bit < 8U;
             bit++) {

            if ((crc & 1U) != 0U) {

                crc =
                    (crc >> 1U) ^
                    0xEDB88320UL;

            } else {

                crc >>= 1U;
            }
        }
    }

    return ~crc;
}


static uint32_t config_calculate_crc(
    const config_t *cfg)
{
    /*
     * CRC covers everything except
     * the crc field itself.
     *
     * crc is currently the last field
     * of config_t.
     */
    const uint32_t length =
        (uint32_t)sizeof(config_t) -
        (uint32_t)sizeof(cfg->crc);

    return config_crc32(
        (const uint8_t *)cfg,
        length);
}


/* ----------------------------------------------------------
 * Defaults
 * ---------------------------------------------------------- */

static void config_zero(
    config_t *cfg)
{
    uint8_t *p = (uint8_t *)cfg;

    for (uint32_t i = 0U;
         i < sizeof(config_t);
         i++) {

        p[i] = 0U;
    }
}


static void output_load_defaults(
    output_config_t *out)
{
    out->type = OUTPUT_PWM;

    out->on_pwm_us = 2000U;
    out->off_pwm_us = 1000U;

    out->failsafe_pwm_us =
        CONFIG_PWM_NEUTRAL_US;

    out->failsafe_state = 0U;

    out->pulse_time_ms = 1000U;

    out->pulse_retrigger =
        PULSE_IGNORE;
}


void config_load_defaults(void)
{
    config_zero(&g_config);

    g_config.magic =
        CONFIG_MAGIC;

    g_config.version =
        CONFIG_VERSION;

    g_config.node_id =
        CONFIG_DEFAULT_NODE_ID;

    g_config.can_bitrate =
        CONFIG_DEFAULT_CAN_BITRATE;

    g_config.pca_count =
        CONFIG_DEFAULT_PCA_COUNT;

    g_config.failsafe_timeout_ms =
        CONFIG_DEFAULT_FS_TIMEOUT;

    for (uint8_t i = 0U;
         i < CONFIG_MAX_OUTPUTS;
         i++) {

        output_load_defaults(
            &g_config.output[i]);
    }

    g_config.crc =
        config_calculate_crc(
            &g_config);
}


/* ----------------------------------------------------------
 * Validation
 * ---------------------------------------------------------- */

static int valid_pwm(
    uint16_t value)
{
    return
        (value >= CONFIG_PWM_MIN_US) &&
        (value <= CONFIG_PWM_MAX_US);
}


static int valid_can_bitrate(
    uint32_t bitrate)
{
    switch (bitrate) {

    case 125000UL:
    case 250000UL:
    case 500000UL:
    case 1000000UL:
        return 1;

    default:
        return 0;
    }
}


int config_validate(void)
{
    if (g_config.magic !=
        CONFIG_MAGIC) {

        return -1;
    }

    if (g_config.version !=
        CONFIG_VERSION) {

        return -2;
    }

    if ((g_config.node_id == 0U) ||
        (g_config.node_id > 127U)) {

        return -3;
    }

    if (!valid_can_bitrate(
            g_config.can_bitrate)) {

        return -4;
    }

    if ((g_config.pca_count == 0U) ||
        (g_config.pca_count >
         CONFIG_MAX_PCA)) {

        return -5;
    }

    if (g_config.failsafe_timeout_ms ==
        0U) {

        return -6;
    }

    for (uint8_t i = 0U;
         i < CONFIG_MAX_OUTPUTS;
         i++) {

        const output_config_t *out =
            &g_config.output[i];

        if (out->type >
            OUTPUT_PULSE) {

            return -10;
        }

        if (!valid_pwm(
                out->on_pwm_us)) {

            return -11;
        }

        if (!valid_pwm(
                out->off_pwm_us)) {

            return -12;
        }

        if (!valid_pwm(
                out->failsafe_pwm_us)) {

            return -13;
        }

        if (out->failsafe_state >
            1U) {

            return -14;
        }

        if (out->pulse_retrigger >
            PULSE_RESTART) {

            return -15;
        }
    }

    return 0;
}


/* ----------------------------------------------------------
 * Flash low level
 * ---------------------------------------------------------- */

static int flash_wait_ready(void)
{
    uint32_t timeout =
        10000000UL;

    while ((FLASH_SR &
            FLASH_SR_BSY) != 0U) {

        if (--timeout == 0U) {
            return -1;
        }
    }

    if ((FLASH_SR &
        (FLASH_SR_PGERR |
         FLASH_SR_WRPRTERR)) != 0U) {

        return -2;
    }

    return 0;
}


static void flash_clear_status(void)
{
    /*
     * EOP / PGERR / WRPRTERR
     * are cleared by writing 1.
     */
    FLASH_SR =
        FLASH_SR_EOP |
        FLASH_SR_PGERR |
        FLASH_SR_WRPRTERR;
}


static void flash_unlock(void)
{
    if ((FLASH_CR &
         FLASH_CR_LOCK) != 0U) {

        FLASH_KEYR =
            FLASH_KEY1;

        FLASH_KEYR =
            FLASH_KEY2;
    }
}


static void flash_lock(void)
{
    FLASH_CR |=
        FLASH_CR_LOCK;
}


static int flash_erase_config_page(void)
{
    if (flash_wait_ready() < 0) {
        return -1;
    }

    flash_clear_status();

    FLASH_CR |=
        FLASH_CR_PER;

    FLASH_AR =
        CONFIG_FLASH_ADDRESS;

    FLASH_CR |=
        FLASH_CR_STRT;

    if (flash_wait_ready() < 0) {

        FLASH_CR &=
            ~FLASH_CR_PER;

        return -2;
    }

    FLASH_CR &=
        ~FLASH_CR_PER;

    flash_clear_status();

    return 0;
}


static int flash_program_halfword(
    uint32_t address,
    uint16_t value)
{
    if (flash_wait_ready() < 0) {
        return -1;
    }

    flash_clear_status();

    FLASH_CR |=
        FLASH_CR_PG;

    *(volatile uint16_t *)address =
        value;

    if (flash_wait_ready() < 0) {

        FLASH_CR &=
            ~FLASH_CR_PG;

        return -2;
    }

    FLASH_CR &=
        ~FLASH_CR_PG;

    flash_clear_status();

    /*
     * Read-back verification.
     */
    if (*(volatile uint16_t *)address !=
        value) {

        return -3;
    }

    return 0;
}


/* ----------------------------------------------------------
 * Save / load
 * ---------------------------------------------------------- */

int config_save(void)
{
    if (config_validate() < 0) {
        return -1;
    }

    /*
     * CRC must represent the exact data
     * which will be written.
     */
    g_config.crc =
        config_calculate_crc(
            &g_config);

    flash_unlock();

    if (flash_erase_config_page() < 0) {

        flash_lock();
        return -2;
    }

    const uint8_t *src =
        (const uint8_t *)&g_config;

    /*
     * STM32F1 Flash is programmed
     * halfword-by-halfword.
     */
    for (uint32_t offset = 0U;
         offset < sizeof(config_t);
         offset += 2U) {

        uint16_t value =
            src[offset];

        if ((offset + 1U) <
            sizeof(config_t)) {

            value |=
                (uint16_t)
                ((uint16_t)src[offset + 1U]
                 << 8U);

        } else {

            value |= 0xFF00U;
        }

        if (flash_program_halfword(
                CONFIG_FLASH_ADDRESS +
                offset,
                value) < 0) {

            flash_lock();
            return -3;
        }
    }

    flash_lock();

    /*
     * Verify complete structure.
     */
    const config_t *flash_cfg =
        (const config_t *)
        CONFIG_FLASH_ADDRESS;

    const uint8_t *a =
        (const uint8_t *)&g_config;

    const uint8_t *b =
        (const uint8_t *)flash_cfg;

    for (uint32_t i = 0U;
         i < sizeof(config_t);
         i++) {

        if (a[i] != b[i]) {
            return -4;
        }
    }

    return 0;
}


int config_reload(void)
{
    const config_t *flash_cfg =
        (const config_t *)
        CONFIG_FLASH_ADDRESS;

    /*
     * Copy raw bytes first.
     */
    const uint8_t *src =
        (const uint8_t *)flash_cfg;

    uint8_t *dst =
        (uint8_t *)&g_config;

    for (uint32_t i = 0U;
         i < sizeof(config_t);
         i++) {

        dst[i] = src[i];
    }

    /*
     * Basic structure validation.
     */
    if (config_validate() < 0) {
        return -1;
    }

    const uint32_t expected_crc =
        config_calculate_crc(
            &g_config);

    if (g_config.crc !=
        expected_crc) {

        return -2;
    }

    return 0;
}


void config_init(void)
{
    /*
     * Try persisted configuration.
     */
    if (config_reload() == 0) {
        return;
    }

    /*
     * Empty / corrupt / old Flash:
     * use defaults.
     *
     * We deliberately do NOT automatically
     * erase/program Flash here.
     */
    config_load_defaults();
}


/* ----------------------------------------------------------
 * Access
 * ---------------------------------------------------------- */

const config_t *config_get(void)
{
    return &g_config;
}


config_t *config_get_mutable(void)
{
    return &g_config;
}


uint8_t config_get_output_count(void)
{
    if (g_config.pca_count >
        CONFIG_MAX_PCA) {

        return 0U;
    }

    return
        (uint8_t)
        (g_config.pca_count * 16U);
}


const output_config_t *
config_get_output(
    uint8_t index)
{
    if (index >=
        CONFIG_MAX_OUTPUTS) {

        return 0;
    }

    return
        &g_config.output[index];
}


output_config_t *
config_get_output_mutable(
    uint8_t index)
{
    if (index >=
        CONFIG_MAX_OUTPUTS) {

        return 0;
    }

    return
        &g_config.output[index];
}
