#include "LightTask.h"
#include "tim.h"
#include "string.h"
#define BALL_LED_COUNT 18U
#define RESET_SLOTS 256U
#define PWM_SIZE (1U + BALL_LED_COUNT * 24U + RESET_SLOTS)
static uint16_t LightPwm[2][PWM_SIZE];
static volatile uint8_t DmaBusy[2], DmaRetry;
static uint8_t Active[9], Dirty = 1U, Brightness = 5U;
static uint32_t HitTick[9];

void Light_Init(void)
{
    memset(LightPwm, 0, sizeof(LightPwm));
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_Delay(1U);
    Dirty = 1U;
}

void BallLight_Hit(uint8_t character)
{
    if (character < 1U || character > 9U)
        return;
    Active[character - 1U] = 1U;
    HitTick[character - 1U] = HAL_GetTick();
    Dirty = 1U;
}

void BallLight_SetMode(uint8_t character, uint8_t color, uint8_t mode)
{
    /* 沿用0x04/0x02帧，Data3改为人物1~9；当前游戏仅用红色0、模式0一秒/1关闭。 */
    if (character < 1U || character > 9U || color != 0U || mode > 1U)
        return;
    if (mode == 0U)
        BallLight_Hit(character);
    else
    {
        Active[character - 1U] = 0U;
        Dirty = 1U;
    }
}

void BallLight_SetBrightness(uint8_t value)
{
    Brightness = value <= 10U ? value : 10U;
    Dirty = 1U;
}

static uint16_t EncodeByte(uint8_t strip, uint16_t offset, uint8_t data)
{
    uint8_t mask;
    for (mask = 0x80U; mask; mask >>= 1U)
        LightPwm[strip][offset++] = (data & mask) ? 63U : 24U;
    return offset;
}

static void Light_DmaIdle(uint8_t strip)
{
    TIM_HandleTypeDef *timer = strip ? &htim1 : &htim3;
    HAL_TIM_PWM_Stop_DMA(timer, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_1, 0U);
    HAL_TIM_PWM_Start(timer, TIM_CHANNEL_1);
    DmaBusy[strip] = 0U;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1)
        return;
    if (htim == &htim3)
        Light_DmaIdle(0U);
    else if (htim == &htim1)
        Light_DmaIdle(1U);
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1 && (htim == &htim3 || htim == &htim1))
    {
        DmaRetry = 1U;
        Light_DmaIdle(htim == &htim1 ? 1U : 0U);
    }
}

void Light_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t i, strip;
    for (i = 0U; i < 9U; i++)
    {
        if (Active[i] && (uint32_t)(now - HitTick[i]) >= 1000U)
        {
            Active[i] = 0U;
            Dirty = 1U;
        }
    }
    if (DmaBusy[0] || DmaBusy[1])
        return;
    if (DmaRetry)
    {
        DmaRetry = 0U;
        Dirty = 1U;
    }
    if (!Dirty)
        return;
    Dirty = 0U;
    for (strip = 0U; strip < 2U; strip++)
    {
        TIM_HandleTypeDef *timer = strip ? &htim1 : &htim3;
        uint16_t offset = 1U;
        /* 每条18颗，两颗一组，两条对应LD1~18/LD19~36，九个人物可独立重叠亮一秒。 */
        for (i = 0U; i < BALL_LED_COUNT; i++)
        {
            offset = EncodeByte(strip, offset, 0U);
            offset = EncodeByte(strip, offset, Active[i / 2U] ? (uint8_t)(255U * Brightness / 10U) : 0U);
            offset = EncodeByte(strip, offset, 0U);
        }
        HAL_TIM_PWM_Stop(timer, TIM_CHANNEL_1);
        __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_1, 0U);
        DmaBusy[strip] = 1U;
        if (HAL_TIM_PWM_Start_DMA(timer, TIM_CHANNEL_1, (uint32_t *)LightPwm[strip], PWM_SIZE) != HAL_OK)
        {
            DmaRetry = 1U;
            Light_DmaIdle(strip);
        }
    }
}
