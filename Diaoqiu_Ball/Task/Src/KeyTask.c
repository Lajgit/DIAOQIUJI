#include "KeyTask.h"
#include "CommTask.h"
#include "main.h"

#define BALL_EYE_COUNT 11U
#define BALL_EYE_SCAN_MS 3U

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
extern Tx_HandleTypeDef Tx;

void KeyAll_Init(void)
{
    for (uint8_t i = 0U; i < BALL_EYE_COUNT; i++)
        BallEye_LastState[i] = HAL_GPIO_ReadPin(BallEye_Port[i], BallEye_Pin[i]);
}

void Key_Task(void)
{
    static uint32_t LastScanTick = 0U;
    uint32_t CurrentTick = HAL_GetTick();

    if ((uint32_t)(CurrentTick - LastScanTick) < BALL_EYE_SCAN_MS)
        return;

    LastScanTick = CurrentTick;

    for (uint8_t i = 0U; i < BALL_EYE_COUNT; i++)
    {
        GPIO_PinState CurrentState = HAL_GPIO_ReadPin(BallEye_Port[i], BallEye_Pin[i]);

        /*
         * 中文注释：当前光眼电路为光敏三极管集电极上拉。
         * 正常收到红外时三极管导通，GPIO为低；钢珠遮挡后GPIO变高。
         * 因此仅在低→高边沿上报一次，避免钢珠停留遮挡时重复发送。
         */
        if (BallEye_LastState[i] == GPIO_PIN_RESET && CurrentState == GPIO_PIN_SET)
        {
            Comm_SendMesg_FillData(
                &Tx,
                Ball_to_Board,
                BALL_REPORT_EYE,
                (uint32_t)(i + 1U),
                BALL_EYE_TRIGGER);
        }

        BallEye_LastState[i] = CurrentState;
    }
}
