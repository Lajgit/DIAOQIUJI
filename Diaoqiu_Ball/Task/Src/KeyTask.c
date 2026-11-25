#include "KeyTask.h"
#include "CommTask.h"
#include "LightTask.h"
#include "main.h"

#define BALL_EYE_COUNT 11U
#define BALL_EYE_SCAN_MS 1U
#define BALL_EYE_TRIGGER_MS 10U
#define BALL_EYE_LOCKOUT_MS 700U
#define BALL_EYE_HOLD_MS 700U

/*
 * 中文注释：人物1~9依次对应U1、U3~U10；CN1/CN2使用上报编号10/11。
 * 人物顺序：杨戬、姜子牙、雷震子、哪吒、太上老君、闻太师、李靖、元始天尊、土行孙。
 * 保持原14字节帧及0x05/0x01光眼功能码，仅补齐新硬件的输入编号。
 */
static GPIO_TypeDef *BallEye_Port[BALL_EYE_COUNT] = {
    BallEye1_GPIO_Port,
    BallEye2_GPIO_Port,
    BallEye3_GPIO_Port,
    BallEye4_GPIO_Port,
    BallEye5_GPIO_Port,
    BallEye6_GPIO_Port,
    BallEye7_GPIO_Port,
    BallEye8_GPIO_Port,
    BallEye9_GPIO_Port,
    BallEyeExternal1_GPIO_Port,
    BallEyeExternal2_GPIO_Port,
};

static uint16_t BallEye_Pin[BALL_EYE_COUNT] = {
    BallEye1_Pin,
    BallEye2_Pin,
    BallEye3_Pin,
    BallEye4_Pin,
    BallEye5_Pin,
    BallEye6_Pin,
    BallEye7_Pin,
    BallEye8_Pin,
    BallEye9_Pin,
    BallEyeExternal1_Pin,
    BallEyeExternal2_Pin,
};

static GPIO_PinState BallEye_LastState[BALL_EYE_COUNT];
static uint32_t BallEye_StateTick[BALL_EYE_COUNT];
static uint8_t BallEye_Armed[BALL_EYE_COUNT];
static uint32_t BallEye_HitTick[BALL_EYE_COUNT];
static uint8_t BallEye_HitSeen[BALL_EYE_COUNT], BallEye_HoldSent[BALL_EYE_COUNT];
static uint32_t LastScanTick;
extern Tx_HandleTypeDef Tx;

void KeyAll_Init(void)
{
    LastScanTick = HAL_GetTick();
    for (uint8_t i = 0U; i < BALL_EYE_COUNT; i++)
    {
        BallEye_LastState[i] = HAL_GPIO_ReadPin(BallEye_Port[i], BallEye_Pin[i]);
        BallEye_StateTick[i] = LastScanTick;
        /* 上电已低时不报普通命中，先观察一次释放；持续低仍按卡住规则上报。 */
        BallEye_Armed[i] = BallEye_LastState[i] == GPIO_PIN_SET;
        BallEye_HitTick[i] = LastScanTick;
        BallEye_HitSeen[i] = BallEye_HoldSent[i] = 0U;
    }
}

void Key_Task(void)
{
    uint32_t CurrentTick = HAL_GetTick();

    if ((uint32_t)(CurrentTick - LastScanTick) < BALL_EYE_SCAN_MS)
        return;

    LastScanTick = CurrentTick;

    for (uint8_t i = 0U; i < BALL_EYE_COUNT; i++)
    {
        GPIO_PinState CurrentState = HAL_GPIO_ReadPin(BallEye_Port[i], BallEye_Pin[i]);

        if (CurrentState != BallEye_LastState[i])
        {
            BallEye_LastState[i] = CurrentState;
            BallEye_StateTick[i] = CurrentTick;
        }

        /* 中文注释：低稳定10ms确认普通触发，从上次有效命中起固定屏蔽700ms。
         * 高电平一经观察到就允许新的低脉冲；被屏蔽的脉冲不延长窗口，也不延后补报。 */
        if (CurrentState == GPIO_PIN_SET)
        {
            BallEye_Armed[i] = 1U;
            BallEye_HoldSent[i] = 0U;
        }
        else if (BallEye_Armed[i] && (uint32_t)(CurrentTick - BallEye_StateTick[i]) >= BALL_EYE_TRIGGER_MS)
        {
            BallEye_Armed[i] = 0U;
            if (!BallEye_HitSeen[i] || (uint32_t)(CurrentTick - BallEye_HitTick[i]) >= BALL_EYE_LOCKOUT_MS)
            {
                BallEye_HitSeen[i] = 1U;
                BallEye_HitTick[i] = CurrentTick;
                if (i < 9U)
                    BallLight_Hit(i + 1U);
                Comm_SendMesg_FillData(&Tx, Ball_to_Board, BALL_REPORT_EYE,
                    (uint32_t)(i + 1U), BALL_EYE_TRIGGER);
            }
        }
        /* 九路人物连续低满700ms单独报长按判负，CN1/CN2不报；每次持续低只报一次。 */
        if (i < 9U && CurrentState == GPIO_PIN_RESET && !BallEye_HoldSent[i] &&
            (uint32_t)(CurrentTick - BallEye_StateTick[i]) >= BALL_EYE_HOLD_MS)
        {
            BallEye_HoldSent[i] = 1U;
            Comm_SendMesg_FillData(&Tx, Ball_to_Board, BALL_REPORT_EYE,
                (uint32_t)(i + 1U), BALL_EYE_HOLD);
        }
    }
}
