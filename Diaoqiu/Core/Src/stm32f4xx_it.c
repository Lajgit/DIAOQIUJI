/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "stm32f4xx_it.h"
/* USER CODE BEGIN Includes */
#include "InterruptTask.h"
/* USER CODE END Includes */

extern DMA_HandleTypeDef hdma_tim3_ch1_trig;
extern DMA_HandleTypeDef hdma_tim3_ch2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
  HoolleInput_Scan1ms();
}

void EXTI3_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(HoolleOutput_1_Pin);
}

void DMA1_Stream4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_tim3_ch1_trig);
}

void DMA1_Stream5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_tim3_ch2);
}

void EXTI9_5_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(CardFeedback_Pin);
  HAL_GPIO_EXTI_IRQHandler(CoinInput_Pin);
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart1);
}

void USART2_IRQHandler(void)
{
  /* 中文注释：USART2预留语音模块，当前main未启用，不再绑定球盘协议。 */
  HAL_UART_IRQHandler(&huart2);
}

void USART3_IRQHandler(void)
{
  /* 中文注释：USART3接收控台协议字节。 */
  HAL_UART_IRQHandler(&huart3);
}

/* 中文注释：RESET和ANGLE同一时刻到达时先归零，零位本身不再重复算作第1格。 */
void EXTI15_10_IRQHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(ResetInput_Pin) != RESET)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(AngleInput_Pin);
    HAL_GPIO_EXTI_IRQHandler(ResetInput_Pin);
  }
  else
    HAL_GPIO_EXTI_IRQHandler(AngleInput_Pin);
}
