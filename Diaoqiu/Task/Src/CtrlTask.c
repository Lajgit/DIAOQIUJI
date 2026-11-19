#include "CtrlTask.h"
#include "MesgTask.h"
#include "FlashTask.h"
#include "CommTask.h"
#include "tim.h"

Motor_Hoolle Motor_Hoolle1, Motor_Hoolle2;
Motor_Card Card;
static motor_t WheelMotor;
static uint8_t DeviceReady;
static uint8_t OutputsPaused;
/* 中文注释：普通直流电机先随机选人物，再回零；每人四格，目标取档位中间第2格。 */
typedef enum { WHEEL_IDLE, WHEEL_HOME, WHEEL_MOVE, WHEEL_DONE, WHEEL_FAULT } Wheel_State_t;
static volatile Wheel_State_t WheelState;
static volatile uint8_t WheelEdges;
static volatile uint32_t WheelLastEdge;
static uint8_t WheelCharacter;
static uint8_t WheelTarget;
static uint32_t RandomState = 0x13579BDFU;
extern Tx_HandleTypeDef Tx1;

void Device_Init(void)
{
    Device_Motor_Init(&Motor_Hoolle1.Motor, &htim1, TIM_CHANNEL_1, &htim1, TIM_CHANNEL_2);
    Device_Motor_Init(&Motor_Hoolle2.Motor, &htim1, TIM_CHANNEL_3, &htim1, TIM_CHANNEL_4);
    /* PD13=输入1/TIM4_CH2，PD12=输入2/TIM4_CH1，与吐珠电机使用同一驱动约定。 */
    Device_Motor_Init(&WheelMotor, &htim4, TIM_CHANNEL_2, &htim4, TIM_CHANNEL_1);
    Device_Switch_Init(&Card.Switch, CardOutput_GPIO_Port, CardOutput_Pin, GPIO_PIN_SET);
    Motor_Hoolle1.Hoolle_num = 0U;
    Motor_Hoolle1.RetryCount = 0U;
    Card.Card_num = 0U;
    WheelState = WHEEL_IDLE;
    DeviceReady = 1U;
}

void Hoolle_Output(Motor_Hoolle *Motor, uint16_t num)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (num <= UINT16_MAX - Motor->Hoolle_num)
        Motor->Hoolle_num += num;
    if (Motor->Hoolle_num && Motor->Motor.state == DEVICE_STATE_IDLE)
    {
        Motor->Motor.state = DEVICE_STATE_START;
        Motor->RetryCount = 0U;
    }
    __set_PRIMASK(primask);
}

void Card_Output(Motor_Card *card, uint16_t num)
{
    if (card->Card_num == 0U && num != 0U)
    {
        card->Card_num = num;
        card->Switch.state = DEVICE_STATE_START;
    }
}

void Ctrl_PauseOutputs(void)
{
    if (!DeviceReady)
        return;
    OutputsPaused = 1U;
    Motor_Hoolle1.Motor.Stop(&Motor_Hoolle1.Motor);
    Motor_Hoolle1.Motor.state = DEVICE_STATE_PAUSE;
    Motor_Hoolle2.Motor.Stop(&Motor_Hoolle2.Motor);
    Motor_Hoolle2.Motor.state = DEVICE_STATE_IDLE;
    Card.Switch.off(&Card.Switch);
    Card.Switch.state = DEVICE_STATE_PAUSE;
    WheelMotor.Stop(&WheelMotor);
    /* 中断计数暂停；保留中奖人物，恢复时重新找零，避免把惯性位移当成准确位置。 */
    if (WheelState == WHEEL_HOME || WheelState == WHEEL_MOVE)
        WheelState = WHEEL_IDLE;
}

void Ctrl_ResumeOutputs(void)
{
    OutputsPaused = 0U;
    Motor_Hoolle1.Motor.state = Motor_Hoolle1.Hoolle_num ? DEVICE_STATE_START : DEVICE_STATE_IDLE;
    Card.Switch.state = Card.Card_num ? DEVICE_STATE_START : DEVICE_STATE_IDLE;
}

void Ctrl_ResetFaults(void)
{
    Motor_Hoolle1.RetryCount = 0U;
    if (WheelState != WHEEL_DONE)
        WheelState = WHEEL_IDLE;
    if (Game.WheelPending == 0U)
    {
        WheelCharacter = 0U;
        WheelState = WHEEL_IDLE;
    }
}

uint8_t Ctrl_OutputsIdle(void)
{
    return Motor_Hoolle1.Motor.state != DEVICE_STATE_BUSY &&
           Motor_Hoolle1.Motor.state != DEVICE_STATE_TIMEOUT &&
           Card.Switch.state != DEVICE_STATE_BUSY &&
           WheelState != WHEEL_HOME && WheelState != WHEEL_MOVE;
}

void Ctrl_WheelResetIRQ(void)
{
    if (!DeviceReady || OutputsPaused || WheelState != WHEEL_HOME)
        return;
    WheelEdges = 0U;
    WheelLastEdge = HAL_GetTick();
    WheelState = WHEEL_MOVE;
}

void Ctrl_WheelAngleIRQ(void)
{
    if (!DeviceReady || OutputsPaused)
        return;
    if (WheelState != WHEEL_HOME && WheelState != WHEEL_MOVE)
        return;
    WheelLastEdge = HAL_GetTick();
    WheelEdges++;
    if (WheelState == WHEEL_HOME && WheelEdges >= 72U)
    {
        /* 两圈仍未见RESET，立即停止并锁定故障。 */
        WheelMotor.Stop(&WheelMotor);
        WheelState = WHEEL_FAULT;
    }
    else if (WheelState == WHEEL_MOVE && WheelEdges >= WheelTarget)
    {
        /* 在ANGLE中断内制动，避免LCD/串口任务延迟停机；实际惯性误差需实机校准。 */
        WheelMotor.Stop(&WheelMotor);
        WheelState = WHEEL_DONE;
    }
}

static void Ctrl_Wheel(void)
{
    if (WheelState == WHEEL_DONE && !Game.FullFlash)
    {
        if (Game.WheelPending)
            Game.WheelPending--;
        Game_CharacterHit(WheelCharacter);
        Comm_SendMesg_FillData(&Tx1, Board_to_Ball, BALL_CMD_RGB_MODE, (uint32_t)WheelCharacter << 8, 0U);
        WheelCharacter = 0U;
        WheelState = WHEEL_IDLE;
    }
    if (WheelState == WHEEL_FAULT)
    {
        Game_SetFault(GAME_FAULT_WHEEL);
        return;
    }
    if (WheelState == WHEEL_IDLE && Game.WheelPending && !Game.FullFlash)
    {
        if (WheelCharacter == 0U)
        {
            RandomState ^= HAL_GetTick();
            RandomState ^= RandomState << 13;
            RandomState ^= RandomState >> 17;
            RandomState ^= RandomState << 5;
            if (RandomState == 0U)
                RandomState = 0x13579BDFU;
            WheelCharacter = (uint8_t)(RandomState % 9U + 1U);
        }
        WheelTarget = (uint8_t)((WheelCharacter - 1U) * 4U + 2U);
        WheelEdges = 0U;
        WheelLastEdge = HAL_GetTick();
        WheelState = WHEEL_HOME;
        WheelMotor.SetSpeed(&WheelMotor, (uint16_t)Setting.WheelSpeed, HoolleMotor_Dir);
    }
    if ((WheelState == WHEEL_HOME || WheelState == WHEEL_MOVE) &&
        (uint32_t)(HAL_GetTick() - WheelLastEdge) >= HoolleMotorTimeout_time)
    {
        WheelMotor.Stop(&WheelMotor);
        WheelState = WHEEL_FAULT;
        Game_SetFault(GAME_FAULT_WHEEL);
    }
}

static void Ctrl_Ball(void)
{
    motor_t *motor = &Motor_Hoolle1.Motor;
    if (motor->state == DEVICE_STATE_STOP)
    {
        motor->Stop(motor);
        motor->state = DEVICE_STATE_IDLE;
    }
    if (motor->state == DEVICE_STATE_START)
    {
        motor->SetSpeed(motor, HoolleMotor_Speed, HoolleMotor_Dir);
        motor->state = DEVICE_STATE_BUSY;
    }
    if (motor->state == DEVICE_STATE_BUSY && motor->GetRuntime(motor) >= HoolleMotorTimeout_time)
    {
        if (Motor_Hoolle1.RetryCount >= HoolleMotorRetry_Times)
        {
            Game_SetFault(GAME_FAULT_BALL);
            return;
        }
        /* 堵珠先反转300ms，最多重试三次；未出数量一直保留给后台决定。 */
        motor->SetSpeed(motor, HoolleMotor_Speed, !HoolleMotor_Dir);
        motor->state = DEVICE_STATE_TIMEOUT;
        Motor_Hoolle1.RetryCount++;
    }
    if (motor->state == DEVICE_STATE_TIMEOUT && motor->GetRuntime(motor) >= HoolleMotorReverse_Time)
    {
        motor->Stop(motor);
        motor->state = DEVICE_STATE_START;
    }
}

static void Ctrl_Card(void)
{
    if (Card.Switch.state == DEVICE_STATE_STOP)
    {
        Card.Switch.off(&Card.Switch);
        Card.Switch.state = DEVICE_STATE_IDLE;
    }
    if (Card.Switch.state == DEVICE_STATE_START)
    {
        Card.Switch.on(&Card.Switch);
        Card.Switch.state = DEVICE_STATE_BUSY;
    }
    if (Card.Switch.state == DEVICE_STATE_BUSY && Card.Switch.GetRuntime(&Card.Switch) >= CardMotorTimeout_time)
        Game_SetFault(GAME_FAULT_CARD);
}

void CtrlTask(void)
{
    uint32_t primask;
    if (!Game_CanRun())
        return;
    /* 出珠/出卡启动与超时转换是短临界段，避免最后一份反馈到达时又启动电机。 */
    primask = __get_PRIMASK();
    __disable_irq();
    Ctrl_Ball();
    __set_PRIMASK(primask);
    if (!Game_CanRun())
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    Ctrl_Card();
    __set_PRIMASK(primask);
    if (!Game_CanRun())
        return;
    Ctrl_Wheel();
    if (!Game_CanRun())
        return;
    /* PE4不参与摆动控制，余额归零立即停机。 */
    if (Game.BallCredits && Motor_Hoolle2.Motor.state == DEVICE_STATE_IDLE)
    {
        Motor_Hoolle2.Motor.SetSpeed(&Motor_Hoolle2.Motor, HoolleMotor2_Speed, HoolleMotor_Dir);
        Motor_Hoolle2.Motor.state = DEVICE_STATE_BUSY;
    }
    else if (!Game.BallCredits && Motor_Hoolle2.Motor.state != DEVICE_STATE_IDLE)
    {
        Motor_Hoolle2.Motor.Stop(&Motor_Hoolle2.Motor);
        Motor_Hoolle2.Motor.state = DEVICE_STATE_IDLE;
    }
}
