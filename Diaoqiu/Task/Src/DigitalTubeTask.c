#include "DigitalTubeTask.h"

#define DigitalTube_Refresh_Time 25

/* 中文注释：四颗16位驱动共64段，必须发送8字节覆盖SEG1~SEG8。 */
static uint8_t DigitalBuffer[8] = {0x82};
DigitalTube_t DigitalTube;

void DigitalTubeTask_Init(void)
{
    DigitalTube_Init_t Init;

    Init.hspi = &hspi2;
    Init.Buffer = DigitalBuffer;
    Init.bit_num = sizeof(DigitalBuffer);
    Init.LE_GPIO = Tube_RCLK_GPIO_Port;
    Init.LE_Pin = Tube_RCLK_Pin;
    Init.CODE_CA = DIGITAL3BIT_CODE_CA;

    DigitalTube_Init(&DigitalTube, Init);
    DigitalTube.Set_Num(&DigitalTube, 0, 0, 4);
    DigitalTube.Set_Num(&DigitalTube, 4, 0, 4);
    DigitalTube.Refresh(&DigitalTube);

    /* 中文注释：最新网表驱动使能脚直接接地，删除原PB14使能操作。 */
}

/* 中文注释：SEG1~SEG4显示总分，SEG5~SEG8显示可兑换卡片数。
 * 四位显示超过9999时保持9999，仅限制显示，不截断业务中的实际分数。
 */
void DigitalTube_SetTotals(uint32_t score, uint32_t cards)
{
    if (score > 9999U)
        score = 9999U;
    if (cards > 9999U)
        cards = 9999U;
    DigitalTube.Set_Num(&DigitalTube, 0U, score, 4U);
    DigitalTube.Set_Num(&DigitalTube, 4U, cards, 4U);
}

void DigitalTube_Task(void)
{
    static uint32_t time = 0;
    if (HAL_GetTick() - time > DigitalTube_Refresh_Time)
    {
        DigitalTube.Refresh(&DigitalTube);
        time = HAL_GetTick();
    }
}
