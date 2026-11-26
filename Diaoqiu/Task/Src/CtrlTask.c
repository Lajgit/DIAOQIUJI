#include "CtrlTask.h"
#include "MesgTask.h"
#include "FlashTask.h"
#include "CommTask.h"
#include "InterruptTask.h"
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
static volatile uint8_t WheelTest; /* 0正常游戏，1后台归零，2后台转一个人物档位 */
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
    HoolleOutput_FilterReset();
    Card.Card_num = 0U;
    WheelState = WHEEL_IDLE;
    WheelTest = 0U;
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
    if (WheelTest)
    {
        WheelTest = 0U;
        WheelState = WHEEL_IDLE;
    }
    /* 中断计数暂停；保留中奖人物，恢复时重新找零，避免把惯性位移当成准确位置。 */
    if (WheelState == WHEEL_HOME || WheelState == WHEEL_MOVE)
        WheelState = WHEEL_IDLE;
}

void Ctrl_ResumeOutputs(void)
{
    if (WheelTest)
        Ctrl_PauseOutputs();
    OutputsPaused = 0U;
    /* 转轮禁玩期间只开放转轮光眼，其他执行器继续保持暂停。 */
    if (Game.WheelBusy)
        return;
    Motor_Hoolle1.Motor.state = Motor_Hoolle1.Hoolle_num ? DEVICE_STATE_START : DEVICE_STATE_IDLE;
    Card.Switch.state = Card.Card_num ? DEVICE_STATE_START : DEVICE_STATE_IDLE;
}

void Ctrl_ResetFaults(void)
{
    if (WheelTest)
        Ctrl_PauseOutputs();
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
    if (WheelTest == 1U)
    {
        /* 中文注释：后台归零在RESET下降沿直接制动，不再走游戏人物中心偏移。 */
        WheelMotor.Stop(&WheelMotor);
        WheelState = WHEEL_DONE;
    }
    else
        WheelState = WHEEL_MOVE;
}

uint8_t Ctrl_WheelTestBusy(void)
{
    return WheelTest != 0U;
}

uint8_t Ctrl_WheelTest(uint8_t home)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (!DeviceReady || !Game.MenuActive || Game.Faults || Game.Failing || Game.FullFlash ||
        WheelTest || WheelState == WHEEL_FAULT)
    {
        __set_PRIMASK(primask);
        return 0U;
    }
    /* 中文注释：仅后台可手动测试；其他执行器继续暂停，游戏人物/奖励任务不改。
     * 沿用当前方向和后台轮速；转一格指一个人物档位，即4个ANGLE下降沿。 */
    WheelTest = home ? 1U : 2U;
    WheelEdges = 0U;
    WheelTarget = 4U;
    WheelLastEdge = HAL_GetTick();
    WheelState = home ? WHEEL_HOME : WHEEL_MOVE;
    OutputsPaused = 0U;
    WheelMotor.SetSpeed(&WheelMotor, (uint16_t)Setting.WheelSpeed, HoolleMotor_Dir);
    __set_PRIMASK(primask);
    return 1U;
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
    if (WheelTest && WheelState == WHEEL_DONE)
    {
        /* 测试完成只停车并恢复后台暂停，不调用游戏中奖或发送人物亮灯。 */
        WheelTest = 0U;
        WheelState = WHEEL_IDLE;
        OutputsPaused = 1U;
        return;
    }
    if (WheelState == WHEEL_DONE && !Game.FullFlash)
    {
        if (Game.WheelPending)
            Game.WheelPending--;
        uint8_t result = WheelCharacter;
        Game_WheelHit(result);
        Comm_SendMesg_FillData(&Tx1, Board_to_Ball, BALL_CMD_RGB_MODE, (uint32_t)result << 8, 0U);
        WheelCharacter = 0U;
        WheelState = WHEEL_IDLE;
    }
    if (WheelState == WHEEL_FAULT)
    {
        Game_SetFault(GAME_FAULT_WHEEL);
        return;
    }
    if (WheelState == WHEEL_IDLE && Game.WheelPending && !Game.FullFlash &&
        (uint32_t)(HAL_GetTick() - Game.WheelTick) >= 1000U)
    {
        if (WheelCharacter == 0U)
        {
            RandomState ^= HAL_GetTick();
            RandomState ^= RandomState << 13;
            RandomState ^= RandomState >> 17;
            RandomState ^= RandomState << 5;
            if (RandomState == 0U)
                RandomState = 0x13579BDFU;
            uint8_t count = 0U, choice, character;
            for (character = 1U; character <= 9U; character++)
                if ((Game.GridMask & (1U << (character - 1U))) == 0U) count++;
            if (count == 0U)
            {
                Game.WheelPending = Game.WheelBusy = 0U;
                Ctrl_ResumeOutputs();
                return;
            }
            /* 中文注释：只从当前未点亮人物中随机选一个，不因转轮重复而判负。 */
            choice = (uint8_t)(RandomState % count);
            for (character = 1U; character <= 9U; character++)
                if ((Game.GridMask & (1U << (character - 1U))) == 0U && choice-- == 0U)
                {
                    WheelCharacter = character;
                    break;
                }
        }
        WheelTarget = (uint8_t)((WheelCharacter - 1U) * 4U + 2U);
        WheelEdges = 0U;
        WheelLastEdge = HAL_GetTick();
        WheelState = WHEEL_HOME;
        WheelMotor.SetSpeed(&WheelMotor, (uint16_t)Setting.WheelSpeed, HoolleMotor_Dir);
    }
    if ((WheelState == WHEEL_HOME || WheelState == WHEEL_MOVE) &&
        (uint32_t)(HAL_GetTick() - WheelLastEdge) >= WheelMotorTimeout_time)
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
        /* 末颗确认后若又排入新球，继续启动，不能把STOP窗口内的新请求留在空闲。 */
        motor->state = Motor_Hoolle1.Hoolle_num ? DEVICE_STATE_START : DEVICE_STATE_IDLE;
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
        /* 中文注释：正转5秒未确认通过则反转300ms，最多重试两次；未出数量保留给后台。 */
        HoolleOutput_FilterReset();
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
    if (WheelTest)
    {
        if (!Game.MenuActive || Game.Faults || Game.Failing || Game.FullFlash)
            Ctrl_PauseOutputs();
        else
            Ctrl_Wheel();
        return;
    }
    if (!Game_CanRun())
        return;
    if (Game.WheelBusy)
    {
        Ctrl_Wheel();
        return;
    }
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
    /* PE4不参与摆动控制；新玩法按本局是否进行控制摆动，而非按余额判结束。 */
    if (Game.RoundActive && Motor_Hoolle2.Motor.state == DEVICE_STATE_IDLE)
    {
        Motor_Hoolle2.Motor.SetSpeed(&Motor_Hoolle2.Motor, HoolleMotor2_Speed, HoolleMotor_Dir);
        Motor_Hoolle2.Motor.state = DEVICE_STATE_BUSY;
    }
    else if (!Game.RoundActive && Motor_Hoolle2.Motor.state != DEVICE_STATE_IDLE)
    {
        Motor_Hoolle2.Motor.Stop(&Motor_Hoolle2.Motor);
        Motor_Hoolle2.Motor.state = DEVICE_STATE_IDLE;
    }
}
