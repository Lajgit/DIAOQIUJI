#include "KeyTask.h"
#include "CommTask.h"
#include "LightTask.h"
#include "main.h"

#define BALL_EYE_COUNT 11U
#define BALL_EYE_SCAN_MS 1U
#define BALL_EYE_TRIGGER_MS 10U
#define BALL_EYE_RELEASE_MS 1000U

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
static uint32_t LastScanTick;
extern Tx_HandleTypeDef Tx;

void KeyAll_Init(void)
{
    LastScanTick = HAL_GetTick();
    for (uint8_t i = 0U; i < BALL_EYE_COUNT; i++)
    {
        BallEye_LastState[i] = HAL_GPIO_ReadPin(BallEye_Port[i], BallEye_Pin[i]);
        BallEye_StateTick[i] = LastScanTick;
        /* 上电已触发的输入先等完整释放，避免初始化后产生一次伪命中。 */
        BallEye_Armed[i] = BallEye_LastState[i] == GPIO_PIN_RESET;
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

        /* 中文注释：沿用原输入高电平触发；九路人物和CN1/CN2分别独立消抖。
         * 触发连续稳定10ms才上报一次，之后低电平释放连续稳定1000ms才重新允许触发。
         * 任一电平变化都重新计时，长时间按住或释放期间抖动不会重复上报。 */
        if (CurrentState == GPIO_PIN_RESET)
        {
            if (!BallEye_Armed[i] && (uint32_t)(CurrentTick - BallEye_StateTick[i]) >= BALL_EYE_RELEASE_MS)
                BallEye_Armed[i] = 1U;
        }
        else if (BallEye_Armed[i] && (uint32_t)(CurrentTick - BallEye_StateTick[i]) >= BALL_EYE_TRIGGER_MS)
        {
            BallEye_Armed[i] = 0U;
            if (i < 9U)
                BallLight_Hit(i + 1U);
            Comm_SendMesg_FillData(
                &Tx,
                Ball_to_Board,
                BALL_REPORT_EYE,
                (uint32_t)(i + 1U),
                BALL_EYE_TRIGGER);
        }
    }
}
