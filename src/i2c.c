#include <stdint.h>

#define RCC_BASE        0x40021000UL
#define GPIOB_BASE      0x40010C00UL
#define I2C1_BASE       0x40005400UL

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define GPIOB_CRL       (*(volatile uint32_t *)(GPIOB_BASE + 0x00))

#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_DR         (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_SR1        (*(volatile uint32_t *)(I2C1_BASE + 0x14))
#define I2C1_SR2        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_CCR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TRISE      (*(volatile uint32_t *)(I2C1_BASE + 0x20))

#define RCC_APB2ENR_IOPBEN  (1U << 3)
#define RCC_APB1ENR_I2C1EN  (1U << 21)

#define I2C_CR1_PE       (1U << 0)
#define I2C_CR1_START    (1U << 8)
#define I2C_CR1_STOP     (1U << 9)

#define I2C_SR1_SB       (1U << 0)
#define I2C_SR1_ADDR     (1U << 1)
#define I2C_SR1_BTF      (1U << 2)
#define I2C_SR1_TXE      (1U << 7)

static int wait_flag(volatile uint32_t *reg,
                     uint32_t mask)
{
    uint32_t timeout = 100000U;

    while (((*reg) & mask) == 0U) {
        if (--timeout == 0U) {
            return -1;
        }
    }

    return 0;
}

void i2c1_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC_APB1ENR |= RCC_APB1ENR_I2C1EN;

    /*
     * PB6 = I2C1_SCL
     * PB7 = I2C1_SDA
     *
     * Alternate function open-drain, 50 MHz:
     * MODE = 11
     * CNF  = 11
     * nibble = 0xF
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
     * Standard mode 100 kHz:
     *
     * CCR = PCLK1 / (2 * I2C)
     *     = 36 MHz / 200 kHz
     *     = 180
     */
    I2C1_CCR = 180U;

    /*
     * Standard mode:
     * TRISE = FREQ_MHz + 1 = 37
     */
    I2C1_TRISE = 37U;

    I2C1_CR1 = I2C_CR1_PE;
}

int i2c1_write_reg(uint8_t address,
                   uint8_t reg,
                   uint8_t value)
{
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
     * Reading SR1 followed by SR2 clears ADDR.
     */
    (void)I2C1_SR1;
    (void)I2C1_SR2;

    if (wait_flag(&I2C1_SR1, I2C_SR1_TXE) < 0) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return -3;
    }

    /*
     * Register address.
     */
    I2C1_DR = reg;

    if (wait_flag(&I2C1_SR1, I2C_SR1_TXE) < 0) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return -4;
    }

    /*
     * Register value.
     */
    I2C1_DR = value;

    if (wait_flag(&I2C1_SR1, I2C_SR1_BTF) < 0) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return -5;
    }

    I2C1_CR1 |= I2C_CR1_STOP;

    return 0;
}
