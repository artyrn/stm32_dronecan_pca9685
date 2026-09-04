#include <stdint.h>

#include "board.h"
#include "i2c.h"
#include "oled.h"
#include "status.h"
#include "dronecan.h"
#include "can_hw.h"
#include "canard_stm32.h"

#define SSD1306_CONTROL_COMMAND  0x00U
#define SSD1306_CONTROL_DATA     0x40U

#define OLED_BUFFER_SIZE \
    ((OLED_WIDTH * OLED_HEIGHT) / 8U)

#define OLED_FLUSH_CHUNK 16U

static uint16_t flush_offset = 0U;
static uint8_t flush_active = 0U;
static uint32_t last_refresh_us = 0U;
static uint32_t last_recovery_us = 0U;
static uint8_t oled_recovering = 0U;

uint32_t micros32(void);

static uint8_t framebuffer[OLED_BUFFER_SIZE];

static const uint8_t font_digits[10][5] = {
    {0x3EU,0x51U,0x49U,0x45U,0x3EU}, /* 0 */
    {0x00U,0x42U,0x7FU,0x40U,0x00U}, /* 1 */
    {0x42U,0x61U,0x51U,0x49U,0x46U}, /* 2 */
    {0x21U,0x41U,0x45U,0x4BU,0x31U}, /* 3 */
    {0x18U,0x14U,0x12U,0x7FU,0x10U}, /* 4 */
    {0x27U,0x45U,0x45U,0x45U,0x39U}, /* 5 */
    {0x3CU,0x4AU,0x49U,0x49U,0x30U}, /* 6 */
    {0x01U,0x71U,0x09U,0x05U,0x03U}, /* 7 */
    {0x36U,0x49U,0x49U,0x49U,0x36U}, /* 8 */
    {0x06U,0x49U,0x49U,0x29U,0x1EU}  /* 9 */
};

static const uint8_t font_letters[26][5] = {
    {0x7EU,0x11U,0x11U,0x11U,0x7EU}, /* A */
    {0x7FU,0x49U,0x49U,0x49U,0x36U}, /* B */
    {0x3EU,0x41U,0x41U,0x41U,0x22U}, /* C */
    {0x7FU,0x41U,0x41U,0x22U,0x1CU}, /* D */
    {0x7FU,0x49U,0x49U,0x49U,0x41U}, /* E */
    {0x7FU,0x09U,0x09U,0x09U,0x01U}, /* F */
    {0x3EU,0x41U,0x49U,0x49U,0x7AU}, /* G */
    {0x7FU,0x08U,0x08U,0x08U,0x7FU}, /* H */
    {0x00U,0x41U,0x7FU,0x41U,0x00U}, /* I */
    {0x20U,0x40U,0x41U,0x3FU,0x01U}, /* J */
    {0x7FU,0x08U,0x14U,0x22U,0x41U}, /* K */
    {0x7FU,0x40U,0x40U,0x40U,0x40U}, /* L */
    {0x7FU,0x02U,0x0CU,0x02U,0x7FU}, /* M */
    {0x7FU,0x04U,0x08U,0x10U,0x7FU}, /* N */
    {0x3EU,0x41U,0x41U,0x41U,0x3EU}, /* O */
    {0x7FU,0x09U,0x09U,0x09U,0x06U}, /* P */
    {0x3EU,0x41U,0x51U,0x21U,0x5EU}, /* Q */
    {0x7FU,0x09U,0x19U,0x29U,0x46U}, /* R */
    {0x46U,0x49U,0x49U,0x49U,0x31U}, /* S */
    {0x01U,0x01U,0x7FU,0x01U,0x01U}, /* T */
    {0x3FU,0x40U,0x40U,0x40U,0x3FU}, /* U */
    {0x1FU,0x20U,0x40U,0x20U,0x1FU}, /* V */
    {0x7FU,0x20U,0x18U,0x20U,0x7FU}, /* W */
    {0x63U,0x14U,0x08U,0x14U,0x63U}, /* X */
    {0x03U,0x04U,0x78U,0x04U,0x03U}, /* Y */
    {0x61U,0x51U,0x49U,0x45U,0x43U}  /* Z */
};


static void oled_clear_buffer(void)
{
    for (uint16_t i = 0U;
         i < OLED_BUFFER_SIZE;
         i++) {
         framebuffer[i] = 0U;
    }
}


static void oled_draw_char(uint8_t column,
                           uint8_t page,
                           char ch)
{
    if (page >= 4U) {
        return;
    }

    if (column > (OLED_WIDTH - 6U)) {
        return;
    }

    const uint8_t *glyph = 0;

    if ((ch >= '0') && (ch <= '9')) {
        glyph = font_digits[(uint8_t)(ch - '0')];
    } else if ((ch >= 'A') && (ch <= 'Z')) {
        glyph = font_letters[(uint8_t)(ch - 'A')];
    }

    const uint16_t offset =
        (uint16_t)page * OLED_WIDTH + column;

    if (glyph != 0) {
        for (uint8_t i = 0U; i < 5U; i++) {
            framebuffer[offset + i] = glyph[i];
        }
    } else if (ch == '.') {
        framebuffer[offset + 2U] = 0x40U;
    } else if (ch == '-') {
        framebuffer[offset + 0U] = 0x08U;
        framebuffer[offset + 1U] = 0x08U;
        framebuffer[offset + 2U] = 0x08U;
        framebuffer[offset + 3U] = 0x08U;
        framebuffer[offset + 4U] = 0x08U;
    } else {
        for (uint8_t i = 0U; i < 5U; i++) {
            framebuffer[offset + i] = 0U;
        }
    }

    framebuffer[offset + 5U] = 0U;
}


static void oled_draw_text(uint8_t column,
                           uint8_t page,
                           const char *text)
{
    while ((*text != '\0') &&
           (column <= (OLED_WIDTH - 6U))) {

        oled_draw_char(column, page, *text);

        column =
            (uint8_t)(column + 6U);

        text++;
    }
}

static void oled_draw_u32(uint8_t column,
                          uint8_t page,
                          uint32_t value)
{
    char buffer[11];
    uint8_t length = 0U;

    if (value == 0U) {
        oled_draw_char(column, page, '0');
        return;
    }

    while ((value != 0U) &&
           (length < sizeof(buffer))) {

        buffer[length++] =
            (char)('0' + (value % 10U));

        value /= 10U;
    }

    while (length != 0U) {

        length--;

        oled_draw_char(
            column,
            page,
            buffer[length]);

        column =
            (uint8_t)(column + 6U);

        if (column > (OLED_WIDTH - 6U)) {
            break;
        }
    }
}

static void oled_build_screen(void)
{
    const uint16_t flags =
        status_get_flags();

    oled_clear_buffer();

    /*
     * Line 0:
     * CAN OK / CAN ERR
     */
    if ((flags & STATUS_FLAG_CAN_ERROR) != 0U) {
        oled_draw_text(0U, 0U, "CAN ERR");
    } else {
        oled_draw_text(0U, 0U, "CAN OK");
    }

    if ((flags & STATUS_FLAG_FAILSAFE) != 0U) {
        oled_draw_text(72U, 0U, "FS ON");
    } else {
        oled_draw_text(72U, 0U, "FS OFF");
    }

    /*
     * Line 1:
     * PCA0 / PCA1
     */
    if ((flags & STATUS_FLAG_PCA0_ERROR) != 0U) {
        oled_draw_text(0U, 1U, "P0 ERR");
    } else {
        oled_draw_text(0U, 1U, "P0 OK");
    }

    if ((flags & STATUS_FLAG_PCA1_ERROR) != 0U) {
        oled_draw_text(72U, 1U, "P1 ERR");
    } else {
        oled_draw_text(72U, 1U, "P1 OK");
    }

    /*
     * Line 2:
     *
     * RX = accepted actuator PWM commands
     * E  = accumulated CAN controller errors
     */
    oled_draw_text(0U, 2U, "RX");

    oled_draw_u32(
        18U,
        2U,
        dronecan_get_actuator_rx_count());

    const CanardSTM32Stats can_stats =
        canardSTM32GetStats();

    oled_draw_text(78U, 2U, "E");

    oled_draw_u32(
        90U,
        2U,
        (uint32_t)can_stats.error_count);

    /*
     * Line 3:
     *
     * T = bxCAN transmit error counter
     * R = bxCAN receive error counter
     * O = RX FIFO overflow count
     */
    oled_draw_text(0U, 3U, "T");

    oled_draw_u32(
        12U,
        3U,
        can_hw_get_tec());

    oled_draw_text(36U, 3U, "R");

    oled_draw_u32(
        48U,
        3U,
        can_hw_get_rec());

    oled_draw_text(78U, 3U, "O");

    oled_draw_u32(
        90U,
        3U,
        (uint32_t)can_stats.rx_overflow_count);

}


static int oled_command(uint8_t command)
{
    return i2c1_write(
        OLED_I2C_ADDRESS,
        SSD1306_CONTROL_COMMAND,
        &command,
        1U);
}


static int oled_send_init(void)
{
    static const uint8_t init_commands[] = {
        0xAEU,       /* Display OFF */

        0xD5U, 0x80U,
        0xA8U, 0x1FU,
        0xD3U, 0x00U,
        0x40U,

        0x8DU, 0x14U,

        0x20U, 0x00U,

        0xA1U,
        0xC8U,

        0xDAU, 0x02U,
        0x81U, 0x8FU,

        0xD9U, 0xF1U,
        0xDBU, 0x40U,

        0xA4U,
        0xA6U,

        0x2EU,

        0xAFU        /* Display ON */
    };

    /*
     * Send commands one by one for now.
     * This keeps the I2C abstraction simple
     * and is only executed during startup.
     */
    for (uint32_t i = 0U;
         i < sizeof(init_commands);
         i++) {

        if (oled_command(init_commands[i]) < 0) {
            return -1;
        }
    }

    return 0;
}


static int oled_clear_display(void)
{

    for (uint32_t i = 0U;
         i < OLED_BUFFER_SIZE;
         i++) {

        framebuffer[i] = 0U;
    }
    /*
     * Horizontal addressing:
     *
     * columns 0..127
     * pages   0..3
     */
    if (oled_command(0x21U) < 0) {
        return -1;
    }

    if (oled_command(0x00U) < 0) {
        return -1;
    }

    if (oled_command(0x7FU) < 0) {
        return -1;
    }

    if (oled_command(0x22U) < 0) {
        return -1;
    }

    if (oled_command(0x00U) < 0) {
        return -1;
    }

    if (oled_command(0x03U) < 0) {
        return -1;
    }

    /*
     * 512 bytes in one transfer is acceptable
     * only during boot.
     *
     * Runtime refresh will be split into small
     * chunks later so CAN processing is not
     * blocked for tens of milliseconds.
     */
    if (i2c1_write(
            OLED_I2C_ADDRESS,
            SSD1306_CONTROL_DATA,
            framebuffer,
            OLED_BUFFER_SIZE) < 0) {

        return -1;
    }

    return 0;
}


int oled_init(void)
{
    if (oled_send_init() < 0) {

        status_set_flags(
            STATUS_FLAG_OLED_ERROR);

        return -1;
    }

    if (oled_clear_display() < 0) {

        status_set_flags(
            STATUS_FLAG_OLED_ERROR);

        return -2;
    }

    status_clear_flags(
        STATUS_FLAG_OLED_ERROR);

    return 0;
}


void oled_process(void)
{
    const uint32_t now = micros32();

    /*
     * Runtime OLED recovery.
     *
     * oled_init() is intentionally not used here:
     * its 512-byte clear is acceptable during boot,
     * but not while CAN is running.
     */
    if (((status_get_flags() & STATUS_FLAG_OLED_ERROR) != 0U) &&
        (oled_recovering == 0U)) {

        if ((uint32_t)(now - last_recovery_us) <
            1000000U) {
            return;
        }

        last_recovery_us = now;
        flush_active = 0U;

        if (oled_send_init() < 0) {
            return;
        }

        /*
         * SSD1306 answered.
         * Keep OLED_ERROR set until one full framebuffer
         * refresh has completed successfully.
         */
        oled_recovering = 1U;
        last_refresh_us = 0U;
    }

    if (flush_active == 0U) {

        if ((uint32_t)(now - last_refresh_us) <
            OLED_REFRESH_INTERVAL_US) {
            return;
        }

        last_refresh_us = now;
        oled_build_screen();
        flush_offset = 0U;
        flush_active = 1U;

        /*
         * Reset SSD1306 write window.
         */
        if ((oled_command(0x21U) < 0) ||
            (oled_command(0x00U) < 0) ||
            (oled_command(0x7FU) < 0) ||
            (oled_command(0x22U) < 0) ||
            (oled_command(0x00U) < 0) ||
            (oled_command(0x03U) < 0)) {

            status_set_flags(
                STATUS_FLAG_OLED_ERROR);

            flush_active = 0U;
            oled_recovering = 0U;
            return;
        }
    }

    uint16_t remaining =
        (uint16_t)(OLED_BUFFER_SIZE -
                   flush_offset);

    uint16_t length = remaining;

    if (length > OLED_FLUSH_CHUNK) {
        length = OLED_FLUSH_CHUNK;
    }

    if (i2c1_write(
            OLED_I2C_ADDRESS,
            SSD1306_CONTROL_DATA,
            &framebuffer[flush_offset],
            length) < 0) {

        status_set_flags(
            STATUS_FLAG_OLED_ERROR);

        flush_active = 0U;
        oled_recovering = 0U;
        return;
    }

    flush_offset =
        (uint16_t)(flush_offset + length);

    if (flush_offset >= OLED_BUFFER_SIZE) {

        flush_active = 0U;
        oled_recovering = 0U;

        status_clear_flags(
            STATUS_FLAG_OLED_ERROR);
    }
}
