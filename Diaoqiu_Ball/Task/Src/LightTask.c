#include "LightTask.h"
#include "tim.h"
#include "string.h"
#define BALL_LED_COUNT 18U
#define BALL_FLASH_INTERVAL_MS 100U
#define BALL_FLASH_PHASES 6U
#define RESET_SLOTS 256U
#define PWM_SIZE (1U + BALL_LED_COUNT * 24U + RESET_SLOTS)
static uint16_t LightPwm[2][PWM_SIZE];
static volatile uint8_t DmaBusy[2], DmaRetry;
static uint8_t Active[9], Dirty = 1U, Brightness = 5U;
static uint8_t FlashPhase[9];
static uint32_t HitTick[9];

void Light_Init(void)
{
    memset(LightPwm, 0, sizeof(LightPwm));
    memset(Active, 0, sizeof(Active));
    memset(FlashPhase, 0, sizeof(FlashPhase));
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_Delay(1U);
    Dirty = 1U;
}

void BallLight_Hit(uint8_t character)
{
    if (character < 1U || character > 9U || Active[character - 1U])
        return;
    Active[character - 1U] = 1U;
    FlashPhase[character - 1U] = 0U;
    HitTick[character - 1U] = HAL_GetTick();
    Dirty = 1U;
}

void BallLight_SetMode(uint8_t character, uint8_t color, uint8_t mode)
{
    /* 中文注释：沿用0x04/0x02、人物1~9/红色0；模式0首次闪3次后常亮，模式1关闭。 */
    if (character < 1U || character > 9U || color != 0U || mode > 1U)
        return;
    if (mode == 0U)
        BallLight_Hit(character);
    else
    {
        Active[character - 1U] = 0U;
        FlashPhase[character - 1U] = 0U;
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
        if (Active[i] && FlashPhase[i] < BALL_FLASH_PHASES)
        {
            uint32_t phase = (uint32_t)(now - HitTick[i]) / BALL_FLASH_INTERVAL_MS;
            uint8_t next = phase < BALL_FLASH_PHASES ? (uint8_t)phase : BALL_FLASH_PHASES;
            if (FlashPhase[i] != next)
            {
                FlashPhase[i] = next;
                Dirty = 1U;
            }
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
        /* 中文注释：每组两条各2颗，100ms亮/100ms灭共3次，600ms后保持红亮直到上板清除。 */
        for (i = 0U; i < BALL_LED_COUNT; i++)
        {
            offset = EncodeByte(strip, offset, 0U);
            offset = EncodeByte(strip, offset, Active[i / 2U] && !(FlashPhase[i / 2U] & 1U) ?
                (uint8_t)(255U * Brightness / 10U) : 0U);
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
