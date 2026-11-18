/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* 中文注释：按Diaoqiu.tel映射进珠、出珠、两路卡片反馈、投币与转盘输入。 */
#define HoolleInput_Pin GPIO_PIN_2
#define HoolleInput_GPIO_Port GPIOE
#define HoolleOutput_1_Pin GPIO_PIN_3
#define HoolleOutput_1_GPIO_Port GPIOE
#define HoolleOutput_2_Pin GPIO_PIN_4
#define HoolleOutput_2_GPIO_Port GPIOE
#define CardFeedback_Pin GPIO_PIN_5
#define CardFeedback_GPIO_Port GPIOE
#define CoinInput_Pin GPIO_PIN_6
#define CoinInput_GPIO_Port GPIOE
#define CardFeedback2_Pin GPIO_PIN_10
#define CardFeedback2_GPIO_Port GPIOE
#define AngleInput_Pin GPIO_PIN_14
#define AngleInput_GPIO_Port GPIOD
#define ResetInput_Pin GPIO_PIN_15
#define ResetInput_GPIO_Port GPIOD

/* 中文注释：锁、两路卡片机及两组12V负载控制；ResetInput不是芯片NRST。 */
#define LockOutput_Pin GPIO_PIN_0
#define LockOutput_GPIO_Port GPIOA
#define CardOutput_Pin GPIO_PIN_4
#define CardOutput_GPIO_Port GPIOC
#define CardOutput2_Pin GPIO_PIN_5
#define CardOutput2_GPIO_Port GPIOC
#define LED1_Pin GPIO_PIN_3
#define LED1_GPIO_Port GPIOB
#define LED2_Pin GPIO_PIN_4
#define LED2_GPIO_Port GPIOB

/* 中文注释：三路电机按驱动器输入脚编号，不在引脚表中定义正反转。 */
#define Motor1_IN1_Pin GPIO_PIN_9
#define Motor1_IN1_GPIO_Port GPIOE
#define Motor1_IN2_Pin GPIO_PIN_11
#define Motor1_IN2_GPIO_Port GPIOE
#define Motor2_IN1_Pin GPIO_PIN_14
#define Motor2_IN1_GPIO_Port GPIOE
#define Motor2_IN2_Pin GPIO_PIN_13
#define Motor2_IN2_GPIO_Port GPIOE
#define Motor3_IN1_Pin GPIO_PIN_13
#define Motor3_IN1_GPIO_Port GPIOD
#define Motor3_IN2_Pin GPIO_PIN_12
#define Motor3_IN2_GPIO_Port GPIOD

/* 中文注释：两路RGB数据为PC6/PC7；PC13仅为本板状态灯。 */
#define RGB1_Pin GPIO_PIN_6
#define RGB1_GPIO_Port GPIOC
#define RGB2_Pin GPIO_PIN_7
#define RGB2_GPIO_Port GPIOC
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC

/* 中文注释：TF卡使用SPI1引脚；LCD三线接口经U6连接，具体时序另行实现。 */
#define SD_CS_Pin GPIO_PIN_4
#define SD_CS_GPIO_Port GPIOA
#define SD_CLK_Pin GPIO_PIN_5
#define SD_CLK_GPIO_Port GPIOA
#define SD_MISO_Pin GPIO_PIN_6
#define SD_MISO_GPIO_Port GPIOA
#define SD_MOSI_Pin GPIO_PIN_7
#define SD_MOSI_GPIO_Port GPIOA
#define LCD_CS_Pin GPIO_PIN_15
#define LCD_CS_GPIO_Port GPIOA
#define LCD_SCK_Pin GPIO_PIN_10
#define LCD_SCK_GPIO_Port GPIOC
#define LCD_MOSI_Pin GPIO_PIN_12
#define LCD_MOSI_GPIO_Port GPIOC

/* 中文注释：数码管PB12/PB13/PB15经U11；驱动使能已接地，没有PB14使能线。 */
#define Tube_RCLK_Pin GPIO_PIN_12
#define Tube_RCLK_GPIO_Port GPIOB
#define Tube_SRCLK_Pin GPIO_PIN_13
#define Tube_SRCLK_GPIO_Port GPIOB
#define Tube_SER_Pin GPIO_PIN_15
#define Tube_SER_GPIO_Port GPIOB

/* 中文注释：沿用已有键名：K1=KeyBoard3、K2=KeyBoard2、K3=KeyBoard1。 */
#define KeyBoard3_Pin GPIO_PIN_6
#define KeyBoard3_GPIO_Port GPIOB
#define KeyBoard2_Pin GPIO_PIN_7
#define KeyBoard2_GPIO_Port GPIOB
#define KeyBoard1_Pin GPIO_PIN_0
#define KeyBoard1_GPIO_Port GPIOD

/* 中文注释：USART1连接下球盘，USART2预留语音，USART3连接通用控台；无安卓板。 */
#define USART1_TX_Pin GPIO_PIN_9
#define USART1_TX_GPIO_Port GPIOA
#define USART1_RX_Pin GPIO_PIN_10
#define USART1_RX_GPIO_Port GPIOA
#define USART2_TX_Pin GPIO_PIN_5
#define USART2_TX_GPIO_Port GPIOD
#define USART2_RX_Pin GPIO_PIN_6
#define USART2_RX_GPIO_Port GPIOD
#define USART3_TX_Pin GPIO_PIN_10
#define USART3_TX_GPIO_Port GPIOB
#define USART3_RX_Pin GPIO_PIN_11
#define USART3_RX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
