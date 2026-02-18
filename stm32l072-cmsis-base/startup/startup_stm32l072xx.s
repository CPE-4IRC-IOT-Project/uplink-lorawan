.syntax unified
.cpu cortex-m0plus
.thumb

.global g_pfnVectors
.global Reset_Handler

.extern main
.extern SystemInit

.section .isr_vector, "a", %progbits
.type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word 0
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler

  .word WWDG_IRQHandler
  .word PVD_IRQHandler
  .word RTC_IRQHandler
  .word FLASH_IRQHandler
  .word RCC_IRQHandler
  .word EXTI0_1_IRQHandler
  .word EXTI2_3_IRQHandler
  .word EXTI4_15_IRQHandler
  .word TSC_IRQHandler
  .word DMA1_Channel1_IRQHandler
  .word DMA1_Channel2_3_IRQHandler
  .word DMA1_Channel4_5_6_7_IRQHandler
  .word ADC1_COMP_IRQHandler
  .word LPTIM1_IRQHandler
  .word USART4_5_6_7_8_IRQHandler
  .word TIM2_IRQHandler
  .word TIM3_IRQHandler
  .word TIM6_DAC_IRQHandler
  .word TIM7_IRQHandler
  .word TIM21_IRQHandler
  .word I2C3_IRQHandler
  .word TIM22_IRQHandler
  .word I2C1_IRQHandler
  .word I2C2_IRQHandler
  .word SPI1_IRQHandler
  .word SPI2_IRQHandler
  .word USART1_IRQHandler
  .word USART2_IRQHandler
  .word RNG_LPUART1_IRQHandler
  .word LCD_IRQHandler
  .word USB_IRQHandler

.size g_pfnVectors, .-g_pfnVectors

.section .text.Reset_Handler, "ax", %progbits
.thumb_func
.type Reset_Handler, %function
Reset_Handler:
  ldr r0, =_sidata
  ldr r1, =_sdata
  ldr r2, =_edata

CopyData:
  cmp r1, r2
  bcs ZeroBss
  ldr r3, [r0]
  str r3, [r1]
  adds r0, r0, #4
  adds r1, r1, #4
  b CopyData

ZeroBss:
  ldr r1, =_sbss
  ldr r2, =_ebss
  movs r3, #0

FillBss:
  cmp r1, r2
  bcs InitSystem
  str r3, [r1]
  adds r1, r1, #4
  b FillBss

InitSystem:
  bl SystemInit
  bl main

LoopForever:
  b LoopForever
.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler, "ax", %progbits
.thumb_func
.type Default_Handler, %function
Default_Handler:
  b Default_Handler
.size Default_Handler, .-Default_Handler

.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler

.weak WWDG_IRQHandler
.thumb_set WWDG_IRQHandler, Default_Handler
.weak PVD_IRQHandler
.thumb_set PVD_IRQHandler, Default_Handler
.weak RTC_IRQHandler
.thumb_set RTC_IRQHandler, Default_Handler
.weak FLASH_IRQHandler
.thumb_set FLASH_IRQHandler, Default_Handler
.weak RCC_IRQHandler
.thumb_set RCC_IRQHandler, Default_Handler
.weak EXTI0_1_IRQHandler
.thumb_set EXTI0_1_IRQHandler, Default_Handler
.weak EXTI2_3_IRQHandler
.thumb_set EXTI2_3_IRQHandler, Default_Handler
.weak EXTI4_15_IRQHandler
.thumb_set EXTI4_15_IRQHandler, Default_Handler
.weak TSC_IRQHandler
.thumb_set TSC_IRQHandler, Default_Handler
.weak DMA1_Channel1_IRQHandler
.thumb_set DMA1_Channel1_IRQHandler, Default_Handler
.weak DMA1_Channel2_3_IRQHandler
.thumb_set DMA1_Channel2_3_IRQHandler, Default_Handler
.weak DMA1_Channel4_5_6_7_IRQHandler
.thumb_set DMA1_Channel4_5_6_7_IRQHandler, Default_Handler
.weak ADC1_COMP_IRQHandler
.thumb_set ADC1_COMP_IRQHandler, Default_Handler
.weak LPTIM1_IRQHandler
.thumb_set LPTIM1_IRQHandler, Default_Handler
.weak USART4_5_6_7_8_IRQHandler
.thumb_set USART4_5_6_7_8_IRQHandler, Default_Handler
.weak TIM2_IRQHandler
.thumb_set TIM2_IRQHandler, Default_Handler
.weak TIM3_IRQHandler
.thumb_set TIM3_IRQHandler, Default_Handler
.weak TIM6_DAC_IRQHandler
.thumb_set TIM6_DAC_IRQHandler, Default_Handler
.weak TIM7_IRQHandler
.thumb_set TIM7_IRQHandler, Default_Handler
.weak TIM21_IRQHandler
.thumb_set TIM21_IRQHandler, Default_Handler
.weak I2C3_IRQHandler
.thumb_set I2C3_IRQHandler, Default_Handler
.weak TIM22_IRQHandler
.thumb_set TIM22_IRQHandler, Default_Handler
.weak I2C1_IRQHandler
.thumb_set I2C1_IRQHandler, Default_Handler
.weak I2C2_IRQHandler
.thumb_set I2C2_IRQHandler, Default_Handler
.weak SPI1_IRQHandler
.thumb_set SPI1_IRQHandler, Default_Handler
.weak SPI2_IRQHandler
.thumb_set SPI2_IRQHandler, Default_Handler
.weak USART1_IRQHandler
.thumb_set USART1_IRQHandler, Default_Handler
.weak USART2_IRQHandler
.thumb_set USART2_IRQHandler, Default_Handler
.weak RNG_LPUART1_IRQHandler
.thumb_set RNG_LPUART1_IRQHandler, Default_Handler
.weak LCD_IRQHandler
.thumb_set LCD_IRQHandler, Default_Handler
.weak USB_IRQHandler
.thumb_set USB_IRQHandler, Default_Handler
