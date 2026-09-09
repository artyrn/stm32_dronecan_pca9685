#include <stdint.h>

#include "i2c.h"

#define RCC_BASE        0x40021000UL
#define GPIOB_BASE      0x40010C00UL
#define I2C2_BASE       0x40005800UL

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define GPIOB_CRH       (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_IDR       (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_ODR       (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))

#define I2C2_CR1        (*(volatile uint32_t *)(I2C2_BASE + 0x00))
#define I2C2_CR2        (*(volatile uint32_t *)(I2C2_BASE + 0x04))
#define I2C2_DR         (*(volatile uint32_t *)(I2C2_BASE + 0x10))
#define I2C2_SR1        (*(volatile uint32_t *)(I2C2_BASE + 0x14))
#define I2C2_SR2        (*(volatile uint32_t *)(I2C2_BASE + 0x18))
#define I2C2_CCR        (*(volatile uint32_t *)(I2C2_BASE + 0x1C))
#define I2C2_TRISE      (*(volatile uint32_t *)(I2C2_BASE + 0x20))

#define RCC_APB2ENR_IOPBEN   (1U << 3)
#define RCC_APB1ENR_I2C2EN   (1U << 22)

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

#define I2C_SR2_BUSY         (1U << 1)

#define I2C_SR1_ERROR_MASK   \
    (I2C_SR1_BERR |          \
     I2C_SR1_ARLO |          \
     I2C_SR1_AF |            \
     I2C_SR1_OVR |           \
     I2C_SR1_TIMEOUT)

/*
 * Physical schematic:
 *
 * PB10 -> I2C2_SCL
 * PB11 -> I2C2_SDA
 */
#define GPIO_SCL             (1U << 10)
#define GPIO_SDA             (1U << 11)

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
     * STM32F1 I2C error flags are cleared by
     * writing zero to the corresponding bits.
     */
    I2C2_SR1 &= ~I2C_SR1_ERROR_MASK;
}


static int wait_flag(volatile uint32_t *reg,
                     uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT;

    while (((*reg) & mask) == 0U) {

        if ((I2C2_SR1 & I2C_SR1_ERROR_MASK) != 0U) {
            return -2;
        }

        if (--timeout == 0U) {
            return -1;
        }
    }

    return 0;
}

static int wait_bus_free(void)
{
    uint32_t timeout = I2C_TIMEOUT;

    while ((I2C2_SR2 & I2C_SR2_BUSY) != 0U) {

        if (--timeout == 0U) {
            return -1;
        }
    }

    return 0;
}

static void i2c_configure_peripheral(void)
{
    /*
     * PB10 = I2C2_SCL
     * PB11 = I2C2_SDA
     *
     * MODE = 11 : output 50 MHz
     * CNF  = 11 : alternate-function open-drain
     *
     * nibble = 0xF
     */
    GPIOB_CRH &= ~((0xFU << 8) |
                   (0xFU << 12));

    GPIOB_CRH |=  ((0xFU << 8) |
                   (0xFU << 12));

    /*
     * Exact sequence already verified manually
     * through OpenOCD.
     *
     * Disable peripheral.
     */
    I2C2_CR1 = 0U;

    /*
     * Software reset.
     */
    I2C2_CR1 = I2C_CR1_SWRST;
    short_delay();

    /*
     * Release software reset.
     */
    I2C2_CR1 = 0U;

    /*
     * APB1 peripheral clock = 36 MHz.
     */
    I2C2_CR2 = 36U;

    /*
     * Standard mode 100 kHz:
     *
     * CCR = 36 MHz / (2 * 100 kHz)
     *     = 180
     */
    I2C2_CCR = 180U;

    /*
     * Maximum rise time:
     *
     * TRISE = FREQ_MHz + 1
     *       = 37
     */
    I2C2_TRISE = 37U;

    /*
     * Enable I2C2.
     */
    I2C2_CR1 = I2C_CR1_PE;
}

void i2c_init(void)
{
    /*
     * Enable GPIOB and I2C2 clocks.
     */
    RCC_APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC_APB1ENR |= RCC_APB1ENR_I2C2EN;

    /*
     * Disable I2C2 while manipulating the bus
     * manually.
     */
    I2C2_CR1 = 0U;

    /*
     * PB10/PB11 = GPIO open-drain output, 2 MHz.
     *
     * PB10 = SCL
     * PB11 = SDA
     *
     * MODE = 10
     * CNF  = 01
     * nibble = 0x6
     */
    GPIOB_CRH &= ~((0xFU << 8) |
                   (0xFU << 12));

    GPIOB_CRH |=  ((0x6U << 8) |
                   (0x6U << 12));

    /*
     * Release both lines.
     */
    GPIOB_ODR |= GPIO_SCL | GPIO_SDA;
    short_delay();

    /*
     * If SDA is stuck LOW, generate up to
     * nine SCL pulses.
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
     * SDA LOW while SCL HIGH,
     * then SDA HIGH.
     */
    GPIOB_ODR &= ~GPIO_SDA;
    short_delay();

    GPIOB_ODR |= GPIO_SCL;
    short_delay();

    GPIOB_ODR |= GPIO_SDA;
    short_delay();

    /*
     * Configure and reset I2C2.
     *
     * i2c_configure_peripheral() performs:
     *
     * CR1 = 0
     * CR1 = SWRST
     * CR1 = 0
     * CR2 = 36
     * CCR = 180
     * TRISE = 37
     * CR1 = PE
     *
     * This exact register sequence has already
     * been verified manually with OpenOCD.
     */
    i2c_configure_peripheral();
}


void i2c_recover(void)
{
    /*
     * Disable I2C2 before taking direct control
     * of PB10/PB11.
     */
    I2C2_CR1 = 0U;

    /*
     * PB10/PB11 = GPIO open-drain output, 2 MHz.
     *
     * PB10 = SCL
     * PB11 = SDA
     */
    GPIOB_CRH &= ~((0xFU << 8) |
                   (0xFU << 12));

    GPIOB_CRH |=  ((0x6U << 8) |
                   (0x6U << 12));

    /*
     * Release both lines.
     */
    GPIOB_ODR |= GPIO_SCL | GPIO_SDA;
    short_delay();

    /*
     * If SDA is held LOW by a slave, clock it
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
     * Restore PB10/PB11 to I2C2 AF open-drain
     * and perform the same SWRST/configuration
     * sequence that is used during startup.
     */
    i2c_configure_peripheral();
}


static int i2c_write_once(uint8_t address,
                          uint8_t reg,
                          const uint8_t *data,
                          uint16_t length)
{

    /*
    * Never start a new transfer until the previous
    * STOP condition has completely released the bus.
    */
    if (wait_bus_free() < 0) {
     return -6;
    }
    clear_i2c_errors();

    /*
     * START
     */
    I2C2_CR1 |= I2C_CR1_START;

    if (wait_flag(&I2C2_SR1, I2C_SR1_SB) < 0) {
        return -1;
    }

    /*
     * 7-bit slave address + WRITE.
     */
    I2C2_DR = (uint32_t)(address << 1);

    if (wait_flag(&I2C2_SR1, I2C_SR1_ADDR) < 0) {
        I2C2_CR1 |= I2C_CR1_STOP;
        return -2;
    }

    /*
     * Clear ADDR by reading SR1 followed by SR2.
     */
    (void)I2C2_SR1;
    (void)I2C2_SR2;

    /*
     * Register/control byte.
     */
    if (wait_flag(&I2C2_SR1, I2C_SR1_TXE) < 0) {
        I2C2_CR1 |= I2C_CR1_STOP;
        return -3;
    }

    I2C2_DR = reg;

    /*
     * Payload.
     */
    for (uint16_t i = 0U; i < length; i++) {

        if (wait_flag(&I2C2_SR1, I2C_SR1_TXE) < 0) {
            I2C2_CR1 |= I2C_CR1_STOP;
            return -4;
        }

        I2C2_DR = data[i];
    }

    /*
     * Wait until final byte has left peripheral.
     */
    if (wait_flag(&I2C2_SR1, I2C_SR1_BTF) < 0) {
        I2C2_CR1 |= I2C_CR1_STOP;
        return -5;
    }

    I2C2_CR1 |= I2C_CR1_STOP;
    /*
    * STOP is not instantaneous.
    *
    * Do not allow the next transaction to begin until
    * hardware has actually released the I2C bus.
    */
    if (wait_bus_free() < 0) {
     return -6;
    }

    return 0;
}



int i2c_write(uint8_t address,
              uint8_t reg,
              const uint8_t *data,
              uint16_t length)
{
    if ((data == 0) && (length != 0U)) {
        return -10;
    }

    int result =
        i2c_write_once(address,
                       reg,
                       data,
                       length);

    if (result == 0) {
        return 0;
    }

    const uint32_t i2c_errors =
        I2C2_SR1 & I2C_SR1_ERROR_MASK;

    /*
     * Pure AF/NACK means that the addressed slave
     * did not acknowledge.
     *
     * Do not recover the whole bus for a missing
     * device.
     */
    if (i2c_errors == I2C_SR1_AF) {
        clear_i2c_errors();
        return result;
    }

    /*
     * Any real bus fault or timeout:
     * recover I2C2 and retry once.
     */
    i2c_recover();

    return i2c_write_once(address,
                          reg,
                          data,
                          length);
}

int i2c_write_reg(uint8_t address,
                  uint8_t reg,
                  uint8_t value)
{
    return i2c_write(address,
                     reg,
                     &value,
                     1U);
}


int i2c_probe(uint8_t address)
{
    /*
     * We only need to determine whether the slave
     * acknowledges its address.
     *
     * Send one harmless control/register byte 0x00.
     */
    return i2c_write(address,
                     0x00U,
                     0,
                     0U);
}
