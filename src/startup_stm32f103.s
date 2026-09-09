.syntax unified
.cpu cortex-m3
.thumb

.global _estack
.global Reset_Handler
.global TIM2_IRQHandler

.section .isr_vector,"a",%progbits

.word _estack
.word Reset_Handler

/* NMI */
.word Default_Handler

/* HardFault */
.word Default_Handler

/* MemManage */
.word Default_Handler

/* BusFault */
.word Default_Handler

/* UsageFault */
.word Default_Handler

/* Reserved */
.word 0
.word 0
.word 0
.word 0

/* SVCall */
.word Default_Handler

/* Debug Monitor */
.word Default_Handler

/* Reserved */
.word 0

/* PendSV */
.word Default_Handler

/* SysTick */
.word Default_Handler

/*
 * STM32F103 external interrupts.
 *
 * IRQ0..IRQ27 are currently unused and go to
 * Default_Handler.
 *
 * IRQ28 = TIM2.
 */

/* IRQ0  WWDG */
.word Default_Handler

/* IRQ1  PVD */
.word Default_Handler

/* IRQ2  TAMPER */
.word Default_Handler

/* IRQ3  RTC */
.word Default_Handler

/* IRQ4  FLASH */
.word Default_Handler

/* IRQ5  RCC */
.word Default_Handler

/* IRQ6  EXTI0 */
.word Default_Handler

/* IRQ7  EXTI1 */
.word Default_Handler

/* IRQ8  EXTI2 */
.word Default_Handler

/* IRQ9  EXTI3 */
.word Default_Handler

/* IRQ10 EXTI4 */
.word Default_Handler

/* IRQ11 DMA1_Channel1 */
.word Default_Handler

/* IRQ12 DMA1_Channel2 */
.word Default_Handler

/* IRQ13 DMA1_Channel3 */
.word Default_Handler

/* IRQ14 DMA1_Channel4 */
.word Default_Handler

/* IRQ15 DMA1_Channel5 */
.word Default_Handler

/* IRQ16 DMA1_Channel6 */
.word Default_Handler

/* IRQ17 DMA1_Channel7 */
.word Default_Handler

/* IRQ18 ADC1_2 */
.word Default_Handler

/* IRQ19 USB_HP_CAN1_TX */
.word Default_Handler

/* IRQ20 USB_LP_CAN1_RX0 */
.word Default_Handler

/* IRQ21 CAN1_RX1 */
.word Default_Handler

/* IRQ22 CAN1_SCE */
.word Default_Handler

/* IRQ23 EXTI9_5 */
.word Default_Handler

/* IRQ24 TIM1_BRK */
.word Default_Handler

/* IRQ25 TIM1_UP */
.word Default_Handler

/* IRQ26 TIM1_TRG_COM */
.word Default_Handler

/* IRQ27 TIM1_CC */
.word Default_Handler

/* IRQ28 TIM2 */
.word TIM2_IRQHandler

.section .text.Reset_Handler
.type Reset_Handler, %function

Reset_Handler:

    /*
     * Copy .data from FLASH to RAM
     */

    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata

1:
    cmp r1, r2
    bcc 2f
    b 3f

2:
    ldr r3, [r0]
    str r3, [r1]
    adds r0, r0, #4
    adds r1, r1, #4
    b 1b

3:
    /*
     * Clear .bss
     */

    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0

4:
    cmp r0, r1
    bcc 5f
    b 6f

5:
    str r2, [r0]
    adds r0, r0, #4
    b 4b

6:
    bl main

7:
    b 7b


.type Default_Handler, %function

Default_Handler:
    b Default_Handler
