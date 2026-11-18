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
    /* 中文注释：仅初始化本板现有通信、旧光眼任务与RGB1驱动；无数码管及状态灯。 */
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
