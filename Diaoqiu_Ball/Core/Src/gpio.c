/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "gpio.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* 中文注释：仅保留SWD，释放旧JTAG功能；本板没有PA15状态灯。 */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  /* 中文注释：RGB1复用由TIM3配置，RGB2驱动尚未启用，先保持低电平。 */
  HAL_GPIO_WritePin(GPIOA, BallLight_Pin | BallLight2_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = BallLight2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 中文注释：11路SG402均有板上3.3V上拉及RC滤波，不增加内部上下拉。 */
  GPIO_InitStruct.Pin = BallEye1_Pin | BallEye2_Pin | BallEye3_Pin |
                        BallEye8_Pin | BallEye9_Pin | BallEyeExternal1_Pin | BallEyeExternal2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BallEye4_Pin | BallEye5_Pin | BallEye6_Pin | BallEye7_Pin;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}
