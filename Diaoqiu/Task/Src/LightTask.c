#include "LightTask.h"
#include "FlashTask.h"
#include "CommTask.h"
#include "tim.h"
#include "string.h"
#define SUCCESS_FLASH_INTERVAL_MS 300U
#define RESET_SLOTS 256U
#define PWM_SIZE (1U + 49U * 24U + RESET_SLOTS)
static uint16_t LightPwm[2][PWM_SIZE];
static const uint8_t LightCount[2] = {36U, 49U};
static volatile uint8_t DmaBusy[2], DmaRetry;
static uint16_t GridMask;
static uint8_t LineCount, FullFlash, Dirty;
static uint8_t IdleWhite = 1U;
static uint32_t FlashTick;
static uint8_t FlashPhase;
extern Tx_HandleTypeDef Tx1;

static uint16_t EncodeByte(uint8_t strip, uint16_t offset, uint8_t data)
{
    uint8_t mask;
    for (mask = 0x80U; mask; mask >>= 1U)
        LightPwm[strip][offset++] = (data & mask) ? 10U : 4U;
    return offset;
}

void LightTask_Init(void)
{
    memset(LightPwm, 0, sizeof(LightPwm));
    IdleWhite = 1U;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_Delay(1U);
    Dirty = 1U;
}

void Light_SetIdle(uint8_t idle)
{
    /* 中文注释：待机时LD1~LD85全部白光，仍使用原亮度；只标记刷新，不阻塞发送。 */
    idle = idle != 0U;
    if (IdleWhite != idle)
    {
        IdleWhite = idle;
        Dirty = 1U;
    }
}

void Light_SetGrid(uint16_t mask, uint8_t lines, uint8_t flash)
{
    if (GridMask != mask || LineCount != lines || FullFlash != flash)
        Dirty = 1U;
    if (flash && !FullFlash)
    {
        FlashTick = HAL_GetTick();
        FlashPhase = 0xFFU;
    }
    /* 中文注释：成功结束广播清灯，避免下板保留最后一帧；仍沿用14字节0x04/0x02。 */
    if (!flash && FullFlash)
        Comm_SendMesg_FillData(&Tx1, Board_to_Ball, BALL_CMD_RGB_MODE, 0U, 1U);
    GridMask = mask;
    LineCount = lines;
    FullFlash = flash;
}

static void Light_DmaIdle(uint8_t strip)
{
    uint32_t channel = strip ? TIM_CHANNEL_2 : TIM_CHANNEL_1;
    HAL_TIM_PWM_Stop_DMA(&htim3, channel);
    __HAL_TIM_SET_COMPARE(&htim3, channel, 0U);
    HAL_TIM_PWM_Start(&htim3, channel);
    /* 两路共用TIM3，不能清零计数器/EGR，否则会破坏另一条灯带的数据位。 */
    DmaBusy[strip] = 0U;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &htim3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
        Light_DmaIdle(0U);
    else if (htim == &htim3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
        Light_DmaIdle(1U);
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
    if (htim != &htim3)
        return;
    DmaRetry = 1U;
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
        Light_DmaIdle(0U);
    else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
        Light_DmaIdle(1U);
}

void LightTask(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t strip, led, red, white, r, g, b;
    uint8_t phase = (uint8_t)(((uint32_t)(now - FlashTick) / SUCCESS_FLASH_INTERVAL_MS) & 1U);
    uint8_t value = (uint8_t)(255U * Setting.Board_Lightness / 10U);
    if (DmaBusy[0] || DmaBusy[1])
        return;
    if (DmaRetry)
    {
        DmaRetry = 0U;
        Dirty = 1U;
    }
    if (FullFlash && FlashPhase != phase)
    {
        /* 中文注释：所有RGB同相300ms红亮/300ms全灭；下板跟随每一相位，不独立计时。 */
        FlashPhase = phase;
        Dirty = 1U;
        Comm_SendMesg_FillData(&Tx1, Board_to_Ball, BALL_CMD_RGB_MODE, 0U, phase ? 3U : 2U);
    }
    if (!Dirty)
        return;
    Dirty = 0U;
    for (strip = 0U; strip < 2U; strip++)
    {
        uint16_t offset = 1U;
        for (led = 0U; led < LightCount[strip]; led++)
        {
            /* RGB1=LD1~36；RGB2前24颗是六档指示，后25颗是白色背景。 */
            white = IdleWhite || (strip && led >= 24U);
            red = strip ? (led < 24U && led / 4U < LineCount) : ((GridMask & (1U << (led / 4U))) != 0U);
            r = (red || white) ? value : 0U;
            g = b = white ? value : 0U;
            if (FullFlash)
            {
                r = phase ? 0U : value;
                g = b = 0U;
            }
            offset = EncodeByte(strip, offset, g);
            offset = EncodeByte(strip, offset, r);
            offset = EncodeByte(strip, offset, b);
        }
        HAL_TIM_PWM_Stop(&htim3, strip ? TIM_CHANNEL_2 : TIM_CHANNEL_1);
        __HAL_TIM_SET_COMPARE(&htim3, strip ? TIM_CHANNEL_2 : TIM_CHANNEL_1, 0U);
        DmaBusy[strip] = 1U;
        if (HAL_TIM_PWM_Start_DMA(&htim3, strip ? TIM_CHANNEL_2 : TIM_CHANNEL_1,
            (uint32_t *)LightPwm[strip], (uint16_t)(1U + LightCount[strip] * 24U + RESET_SLOTS)) != HAL_OK)
        {
            DmaRetry = 1U;
            Light_DmaIdle(strip);
        }
    }
}
