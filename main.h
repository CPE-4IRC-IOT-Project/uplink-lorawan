/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Conditional HAL include - comment out if not using STM32 HAL */
#ifdef USE_HAL_DRIVER
#include "stm32l4xx_hal.h"
#endif

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void SystemClock_Config(void);

/* Private defines -----------------------------------------------------------*/
#define LED1_PIN GPIO_PIN_5
#define LED1_GPIO_PORT GPIOA

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
