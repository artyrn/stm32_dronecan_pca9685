.syntax unified
.cpu cortex-m3
.thumb

.global _estack
.global Reset_Handler

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
