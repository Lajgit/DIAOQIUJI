#include "InterruptTask.h"
#include "CtrlTask.h"
#include "MesgTask.h"
#include "MainTask.h"
#include "CommTask.h"
#include "FlashTask.h"
#include "port_event.h"
#include "tim.h"
#include "usart.h"

#define HOOLLE_INPUT_FILTER_MS 3U

volatile uint16_t HoolleInputPendingCount = 0U;
volatile uint16_t CoinInputPendingCount = 0U;
volatile uint16_t BallOutputPendingCount = 0U;
volatile uint16_t CardOutputPendingCount = 0U;

extern Motor_Hoolle Motor_Hoolle1, Motor_Hoolle2;
extern Motor_Card Card;
extern Rx_HandleTypeDef Rx1;
extern Rx_HandleTypeDef Rx3;

static uint8_t HoolleInputRawState = 1U;
static uint8_t HoolleInputStableState = 1U;
static uint8_t HoolleInputSameCount = 0U;

void HoolleInput_FilterInit(void)
{
    uint8_t CurrentState =
        HAL_GPIO_ReadPin(HoolleInput_GPIO_Port, HoolleInput_Pin) == GPIO_PIN_SET ? 1U : 0U;

    HoolleInputRawState = CurrentState;
    HoolleInputStableState = CurrentState;
    HoolleInputSameCount = 0U;
}

void HoolleInput_Scan1ms(void)
{
    uint8_t CurrentState;
    Game_Timer1ms();
    CurrentState =
        HAL_GPIO_ReadPin(HoolleInput_GPIO_Port, HoolleInput_Pin) == GPIO_PIN_SET ? 1U : 0U;

    if (CurrentState == HoolleInputRawState)
    {
        if (HoolleInputSameCount < HOOLLE_INPUT_FILTER_MS)
            HoolleInputSameCount++;
    }
    else
    {
        HoolleInputRawState = CurrentState;
        HoolleInputSameCount = 1U;
    }

    if (HoolleInputSameCount >= HOOLLE_INPUT_FILTER_MS &&
        HoolleInputStableState != HoolleInputRawState)
    {
        HoolleInputStableState = HoolleInputRawState;
        if (Setting.InputMode == 1U && HoolleInputStableState == 0U && HoolleInputPendingCount < 0xFFFFU)
            HoolleInputPendingCount++;
    }
}

static void Hoolle_1_Output_IRQ(void)
{
    static uint8_t Armed, LastStopped;
    static uint32_t LowTick;
    uint32_t now = HAL_GetTick();
    if (Motor_Hoolle1.Motor.Stop == NULL)
        return;
    if (HAL_GPIO_ReadPin(HoolleOutput_1_GPIO_Port, HoolleOutput_1_Pin) == GPIO_PIN_RESET)
    {
        Armed = Motor_Hoolle1.Hoolle_num != 0U && Motor_Hoolle1.Motor.state == DEVICE_STATE_BUSY;
        LastStopped = 0U;
        LowTick = now;
        if (Armed && Motor_Hoolle1.Hoolle_num == 1U)
        {
            Motor_Hoolle1.Motor.Stop(&Motor_Hoolle1.Motor);
            LastStopped = 1U;
        }
        return;
    }
    if (!Armed)
        return;
    Armed = 0U;
    if (Motor_Hoolle1.Motor.state == DEVICE_STATE_TIMEOUT)
        return;
    /* 遮挡至少2ms且释放才确认一颗；反转和空闲噪声不计为出珠。 */
    if ((uint32_t)(now - LowTick) >= 2U && Motor_Hoolle1.Hoolle_num)
    {
        Motor_Hoolle1.Hoolle_num--;
        Motor_Hoolle1.RetryCount = 0U;
        Motor_Hoolle1.Motor.ResetRuntime(&Motor_Hoolle1.Motor);
        if (BallOutputPendingCount < UINT16_MAX)
            BallOutputPendingCount++;
        if (!Motor_Hoolle1.Hoolle_num)
        {
            Motor_Hoolle1.Motor.Stop(&Motor_Hoolle1.Motor);
            Motor_Hoolle1.Motor.state = DEVICE_STATE_STOP;
        }
    }
    else if (LastStopped && Motor_Hoolle1.Motor.state == DEVICE_STATE_BUSY)
        Motor_Hoolle1.Motor.state = DEVICE_STATE_START;
    LastStopped = 0U;
}

static void CoinInput_IRQ(void)
{
    static uint8_t Seen;
    static uint32_t LastTick;
    uint32_t now = HAL_GetTick();
    /* 中文注释：PE6只在投币模式记数；另一输入PE2由1ms滤波按模式屏蔽。 */
    if (Setting.InputMode != 0U)
        return;
    if (Seen && (uint32_t)(now - LastTick) < 50U)
        return;
    Seen = 1U;
    LastTick = now;
    if (CoinInputPendingCount < UINT16_MAX)
        CoinInputPendingCount++;
}

static void CardOutput_IRQ(void)
{
    static uint8_t Seen;
    static uint32_t LastTick;
    uint32_t now = HAL_GetTick();
    if (Card.Switch.off == NULL || !Card.Card_num ||
        (Card.Switch.state != DEVICE_STATE_BUSY && Card.Switch.state != DEVICE_STATE_PAUSE))
        return;
    if (Seen && (uint32_t)(now - LastTick) < 5U)
        return;
    Seen = 1U;
    LastTick = now;
    Card.Card_num--;
    Card.Switch.ResetRuntime(&Card.Switch);
    if (CardOutputPendingCount < UINT16_MAX)
        CardOutputPendingCount++;
    if (!Card.Card_num)
    {
        Card.Switch.off(&Card.Switch);
        Card.Switch.state = DEVICE_STATE_STOP;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    switch (GPIO_Pin)
    {
    case HoolleOutput_1_Pin:
        Hoolle_1_Output_IRQ();
        break;
    case CoinInput_Pin:
        CoinInput_IRQ();
        break;
    case CardFeedback_Pin:
        CardOutput_IRQ();
        break;
    case AngleInput_Pin:
        Ctrl_WheelAngleIRQ();
        break;
    case ResetInput_Pin:
        Ctrl_WheelResetIRQ();
        break;
    default:
        break;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    Rx_HandleTypeDef *Rx = NULL;

    if (huart == &huart2)
    {
        YC686_ReceiveIRQ();
        return;
    }

    if (huart == Rx1.Handle.huart)
        Rx = &Rx1;
    else if (huart == Rx3.Handle.huart)
        Rx = &Rx3;

    if (Rx != NULL)
    {
        /* 中文注释：USART1/USART3保留14字节链路，USART2上方分支独立处理YC686B字节。 */
        Rx->Handle.RingBuf.f_WriteByte(&Rx->Handle.RingBuf, Rx->Handle.temp_data);
        HAL_UART_Receive_IT(huart, &Rx->Handle.temp_data, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
        YC686_ErrorIRQ();
}
