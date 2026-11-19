#include "MainTask.h"
#include "FlashTask.h"
#include "MainTask.h"
#include "port_event.h"

Flash_HandleTypeDef hflash;
Setting_TypeDef Setting;
extern Event_Handle_t Event;
void ResumeSetting(void)
{
    Setting.Board_Lightness = 5;
    Setting.LightBelt_Lightness = 5;
    Setting.Ctrl_Lightness = 5;
    Setting.InputBallsPerCredit = 1U;
    Setting.PointsPerCard = 5U;
    Setting.WheelSpeed = 80U;
    Setting.KeepPending = 1U;
}

void FlashTask_Init(void)
{
    Flash_InitTypeDef init;
    init.DataSize = sizeof(Setting_TypeDef);
    init.pData = &Setting;
    init.StartAddress = Setting_Addr;

    Flash_Init(&hflash, init);
    hflash.f_Read(&hflash);
    /* 中文注释：按字段校验，旧固件未写入的新字段用默认值，保留有效亮度。 */
    if (Setting.Board_Lightness > 10U) Setting.Board_Lightness = 5U;
    if (Setting.LightBelt_Lightness > 10U) Setting.LightBelt_Lightness = 5U;
    if (Setting.Ctrl_Lightness > 10U) Setting.Ctrl_Lightness = 5U;
    if (Setting.InputBallsPerCredit == 0U || Setting.InputBallsPerCredit > 99U) Setting.InputBallsPerCredit = 1U;
    if (Setting.PointsPerCard == 0U || Setting.PointsPerCard > 999U) Setting.PointsPerCard = 5U;
    if (Setting.WheelSpeed < 10U || Setting.WheelSpeed > 100U) Setting.WheelSpeed = 80U;
    if (Setting.KeepPending > 1U) Setting.KeepPending = 1U;
}

void FlashTask(void)
{
    if (EventGroupCheckBits(&Event, Event_SaveSetting) == true)
    {
        hflash.f_Write(&hflash);
        EventGroupClearBits(&Event, Event_SaveSetting);
    }
}
/* 中文注释：后台退出时在所有执行器暂停状态下保存，返回失败则留在后台。 */
int FlashTask_Save(void)
{
    return hflash.f_Write(&hflash);
}
