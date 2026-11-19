#include "MainTask.h"
#include "CommTask.h"
#include "InterruptTask.h"
#include "KeyTask.h"
#include "LightTask.h"
#include "port_event.h"
#include "iwdg.h"

Event_Handle_t Event;

void System_Reset(void)
{
    __disable_irq();
    HAL_NVIC_SystemReset();
}

void Main_Init(void)
{
    /* 中文注释：初始化板间通信、九个人物和两路转轮输入、两条18颗RGB灯带。 */
    CommInit();
    KeyAll_Init();
    Light_Init();
}

void Main_Task(void)
{
    CommTask();
    HAL_IWDG_Refresh(&hiwdg);
    Key_Task();
    HAL_IWDG_Refresh(&hiwdg);
    Light_Task();
    HAL_IWDG_Refresh(&hiwdg);
    HAL_IWDG_Refresh(&hiwdg);
}
