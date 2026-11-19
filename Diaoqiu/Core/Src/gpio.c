/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, CardOutput_Pin | CardOutput2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, LED1_Pin | LED2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, Motor3_IN1_Pin | Motor3_IN2_Pin, GPIO_PIN_RESET);
  /* 中文注释：尚未启用RGB驱动，先将两路数据保持低电平。 */
  HAL_GPIO_WritePin(GPIOC, RGB1_Pin | RGB2_Pin, GPIO_PIN_RESET);

  /* 中文注释：最新上球盘网表中电子锁控制为 PA0，默认输出低电平保持关闭。 */
  HAL_GPIO_WritePin(LockOutput_GPIO_Port, LockOutput_Pin, GPIO_PIN_RESET);

  /* 中文注释：RCLK1经过74HC14反相，MCU侧默认拉高可使SM16306的LE保持低电平锁存；使能脚由硬件接地。 */
  HAL_GPIO_WritePin(Tube_RCLK_GPIO_Port, Tube_RCLK_Pin, GPIO_PIN_SET);

  /* 进珠光眼由SysTick软件滤波扫描，不再配置无效的EXTI2。 */
  GPIO_InitStruct.Pin = HoolleInput_Pin | CardFeedback2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(HoolleInput_GPIO_Port, &GPIO_InitStruct);

  /* 出卡反馈和投币保持原上升沿。 */
  GPIO_InitStruct.Pin = CardFeedback_Pin | CoinInput_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* 中文注释：PE3为吐珠光眼；PE4对应摆动电机，当前不使用反馈。 */
  GPIO_InitStruct.Pin = HoolleOutput_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* 中文注释：PE4暂不使用，只配置普通输入，不能计数或停止摆动电机。 */
  GPIO_InitStruct.Pin = HoolleOutput_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* 中文注释：转盘ANGLE/RESET为普通输入，边沿语义待业务确定。 */
  GPIO_InitStruct.Pin = AngleInput_Pin | ResetInput_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* 系统指示灯 */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* 卡片机控制输出 */
  GPIO_InitStruct.Pin = CardOutput_Pin | CardOutput2_Pin | RGB1_Pin | RGB2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CardOutput_GPIO_Port, &GPIO_InitStruct);

  /* 中文注释：电子锁MOS控制脚，原弹界TIM5灯效占用已取消。 */
  GPIO_InitStruct.Pin = LockOutput_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LockOutput_GPIO_Port, &GPIO_InitStruct);

  /* 中文注释：PB12为锁存时钟，PB3/PB4为两组负载；删除无连接的PB14使能。 */
  GPIO_InitStruct.Pin = Tube_RCLK_Pin | LED1_Pin | LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 中文注释：后台设置按键K1/K2使用PB6/PB7，均由板上电阻上拉，按下为低。 */
  GPIO_InitStruct.Pin = KeyBoard3_Pin | KeyBoard2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 中文注释：第三路电机先保持双输入低电平；本次不定义驱动真值表。 */
  GPIO_InitStruct.Pin = Motor3_IN1_Pin | Motor3_IN2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* 中文注释：K3仅使用PD0，删除没有网表接线的PD8/PD9/PD10游玩按键。 */
  GPIO_InitStruct.Pin = KeyBoard1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */