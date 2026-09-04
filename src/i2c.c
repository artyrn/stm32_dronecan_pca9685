#include <stdint.h>

#include "i2c.h"

#define RCC_BASE        0x40021000UL
#define GPIOB_BASE      0x40010C00UL
#define I2C1_BASE       0x40005400UL

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define GPIOB_CRL       (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_IDR       (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_ODR       (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))

#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_DR         (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_SR1        (*(volatile uint32_t *)(I2C1_BASE + 0x14))
#define I2C1_SR2        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_CCR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TRISE      (*(volatile uint32_t *)(I2C1_BASE + 0x20))

#define RCC_APB2ENR_IOPBEN   (1U << 3)
#define RCC_APB1ENR_I2C1EN   (1U << 21)

#define I2C_CR1_PE           (1U << 0)
#define I2C_CR1_START        (1U << 8)
#define I2C_CR1_STOP         (1U << 9)
#define I2C_CR1_SWRST        (1U << 15)

#define I2C_SR1_SB           (1U << 0)
#define I2C_SR1_ADDR         (1U << 1)
#define I2C_SR1_BTF          (1U << 2)
#define I2C_SR1_TXE          (1U << 7)

#define I2C_SR1_BERR         (1U << 8)
#define I2C_SR1_ARLO         (1U << 9)
#define I2C_SR1_AF           (1U << 10)
#define I2C_SR1_OVR          (1U << 11)
#define I2C_SR1_TIMEOUT      (1U << 14)

#define I2C_SR1_ERROR_MASK   \
    (I2C_SR1_BERR |          \
     I2C_SR1_ARLO |          \
     I2C_SR1_AF |            \
     I2C_SR1_OVR |           \
     I2C_SR1_TIMEOUT)

#define GPIO_SCL             (1U << 6)
#define GPIO_SDA             (1U << 7)

#define I2C_TIMEOUT          100000U


static void short_delay(void)
{
    for (volatile uint32_t i = 0U; i < 200U; i++) {
        __asm volatile ("nop");
    }
}


static void clear_i2c_errors(void)
{
    /*
     * STM32F1 I2C error flags are cleared
     * by writing zero to the corresponding bits.
     */
    I2C1_SR1 &= ~I2C_SR1_ERROR_MASK;
}


static int wait_flag(volatile uint32_t *reg,
                     uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT;

    while (((*reg) & mask) == 0U) {

        if ((I2C1_SR1 & I2C_SR1_ERROR_MASK) != 0U) {
            return -2;
        }

        if (--timeout == 0U) {
            return -1;
        }
    }

    return 0;
}


static void i2c1_configure_peripheral(void)
{
    /*
     * PB6/PB7:
     * Alternate-function open-drain, 50 MHz.
     */
    GPIOB_CRL &= ~((0xFU << 24) |
                   (0xFU << 28));

    GPIOB_CRL |=  ((0xFU << 24) |
                   (0xFU << 28));

    I2C1_CR1 = 0U;

    /*
     * APB1 = 36 MHz.
     */
    I2C1_CR2 = 36U;

    /*
     * Standard mode, 100 kHz.
     *
     * CCR = 36 MHz / (2 * 100 kHz)
     *     = 180
     */
    I2C1_CCR = 180U;

    /*
     * TRISE = FREQ_MHz + 1
     */
    I2C1_TRISE = 37U;

    clear_i2c_errors();

    I2C1_CR1 = I2C_CR1_PE;
}


void i2c1_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC_APB1ENR |= RCC_APB1ENR_I2C1EN;

    i2c1_configure_peripheral();
}


void i2c1_recover(void)
{
    /*
     * Disable peripheral before manually
     * controlling SCL/SDA.
     */
    I2C1_CR1 &= ~I2C_CR1_PE;

    /*
     * PB6/PB7 = GPIO output open-drain, 2 MHz.
     *
     * MODE = 10
     * CNF  = 01
     * nibble = 0x6
     */
    GPIOB_CRL &= ~((0xFU << 24) |
                   (0xFU << 28));

    GPIOB_CRL |=  ((0x6U << 24) |
                   (0x6U << 28));

    /*
     * Release both lines.
     */
    GPIOB_ODR |= GPIO_SCL | GPIO_SDA;

    short_delay();

    /*
     * If SDA is held LOW, clock the slave
     * up to nine times.
     */
    for (uint8_t i = 0U; i < 9U; i++) {

        if ((GPIOB_IDR & GPIO_SDA) != 0U) {
            break;
        }

        GPIOB_ODR &= ~GPIO_SCL;
        short_delay();

        GPIOB_ODR |= GPIO_SCL;
        short_delay();
    }

    /*
     * Generate manual STOP:
     *
     * SDA LOW
     * SCL HIGH
     * SDA HIGH
     */
    GPIOB_ODR &= ~GPIO_SDA;
    short_delay();

    GPIOB_ODR |= GPIO_SCL;
    short_delay();

    GPIOB_ODR |= GPIO_SDA;
    short_delay();

    /*
     * Reset I2C peripheral.
     */
    I2C1_CR1 = I2C_CR1_SWRST;
    short_delay();
    I2C1_CR1 = 0U;

    i2c1_configure_peripheral();
}


static int i2c1_write_once(uint8_t address,
                           uint8_t reg,
                           const uint8_t *data,
                           uint16_t length)
{
    clear_i2c_errors();

    /*
     * START
     */
    I2C1_CR1 |= I2C_CR1_START;

    if (wait_flag(&I2C1_SR1, I2C_SR1_SB) < 0) {
        return -1;
    }

    /*
     * 7-bit address + WRITE
     */
    I2C1_DR = (uint32_t)(address << 1);

    if (wait_flag(&I2C1_SR1, I2C_SR1_ADDR) < 0) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return -2;
    }

    /*
     * Clear ADDR.
     */
    (void)I2C1_SR1;
    (void)I2C1_SR2;

    /*
     * Register address.
     */
    if (wait_flag(&I2C1_SR1, I2C_SR1_TXE) < 0) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return -3;
    }

    I2C1_DR = reg;

    /*
     * Payload.
     */
    for (uint16_t i = 0U; i < length; i++) {

        if (wait_flag(&I2C1_SR1, I2C_SR1_TXE) < 0) {
            I2C1_CR1 |= I2C_CR1_STOP;
            return -4;
        }

        I2C1_DR = data[i];
    }

    /*
     * Wait until the final byte has actually
     * left the peripheral.
     */
    if (wait_flag(&I2C1_SR1, I2C_SR1_BTF) < 0) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return -5;
    }

    I2C1_CR1 |= I2C_CR1_STOP;

    return 0;
}


int i2c1_write(uint8_t address,
               uint8_t reg,
               const uint8_t *data,
               uint16_t length)
{
    if ((data == 0) && (length != 0U)) {
        return -10;
    }

    int result =
        i2c1_write_once(address,
                        reg,
                        data,
                        length);

    if (result == 0) {
        return 0;
    }

    const uint32_t i2c_errors =
     I2C1_SR1 & I2C_SR1_ERROR_MASK;

    /*
    * A pure NACK means that the addressed slave did
    * not acknowledge the transfer. The bus itself
    * does not need recovery.
    *
    * If another error is present together with AF,
    * perform normal bus recovery below.
    */
    if ((i2c_errors == I2C_SR1_AF)) {
     clear_i2c_errors();
     return result;
    }

    /*
     * A slave may have been reset halfway through
     * a transaction, or SDA may be stuck.
     *
     * Recover the bus and retry exactly once.
     */
    i2c1_recover();

    result =
        i2c1_write_once(address,
                        reg,
                        data,
                        length);

    return result;
}


int i2c1_write_reg(uint8_t address,
                   uint8_t reg,
                   uint8_t value)
{
    return i2c1_write(address,
                      reg,
                      &value,
                      1U);
}
