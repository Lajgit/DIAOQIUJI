/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

void Error_Handler(void);

/* 中文注释：按Diaoqiu_Ball.tel映射两路RGB：PA6为LD1~18，PA8为LD19~36。 */
#define BallLight_Pin GPIO_PIN_6
#define BallLight_GPIO_Port GPIOA
#define BallLight2_Pin GPIO_PIN_8
#define BallLight2_GPIO_Port GPIOA

/* 中文注释：九路板载SG402按U1、U3~U10排列；编号仅描述本板器件顺序。 */
#define BallEye1_Pin GPIO_PIN_0
#define BallEye1_GPIO_Port GPIOB
#define BallEye2_Pin GPIO_PIN_1
#define BallEye2_GPIO_Port GPIOB
#define BallEye3_Pin GPIO_PIN_2
#define BallEye3_GPIO_Port GPIOB
#define BallEye4_Pin GPIO_PIN_3
#define BallEye4_GPIO_Port GPIOA
#define BallEye5_Pin GPIO_PIN_4
#define BallEye5_GPIO_Port GPIOA
#define BallEye6_Pin GPIO_PIN_11
#define BallEye6_GPIO_Port GPIOA
#define BallEye7_Pin GPIO_PIN_12
#define BallEye7_GPIO_Port GPIOA
#define BallEye8_Pin GPIO_PIN_5
#define BallEye8_GPIO_Port GPIOB
#define BallEye9_Pin GPIO_PIN_6
#define BallEye9_GPIO_Port GPIOB

/* 中文注释：CN1/CN2为独立外接SG402，不再称为数码管反馈。 */
#define BallEyeExternal1_Pin GPIO_PIN_7
#define BallEyeExternal1_GPIO_Port GPIOB
#define BallEyeExternal2_Pin GPIO_PIN_8
#define BallEyeExternal2_GPIO_Port GPIOB

/* 中文注释：J14通过USART1与上球盘J10交叉连接，PA3专供光眼输入。 */
#define USART1_TX_Pin GPIO_PIN_9
#define USART1_TX_GPIO_Port GPIOA
#define USART1_RX_Pin GPIO_PIN_10
#define USART1_RX_GPIO_Port GPIOA

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
