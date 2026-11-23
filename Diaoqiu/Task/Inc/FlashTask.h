#ifndef __FLASHTASK_H__
#define __FLASHTASK_H__ 

#include "port_flash.h"

#define Setting_Addr 0x080E0000

typedef struct
{
    uint32_t Board_Lightness;
    uint32_t LightBelt_Lightness;
    uint32_t Ctrl_Lightness;
    uint32_t CoinsPerPlay;
    uint32_t PointsPerCard;
    uint32_t WheelSpeed;
    uint32_t KeepPending;
    uint32_t PlayMode;      /* 0每球、1每局 */
    uint32_t InputMode;     /* 0=PE6投币、1=PE2投珠 */
    uint32_t CoinScoreLink; /* 开启后只存积分，不另存可用币 */
    uint32_t PointsPerCoin;
}Setting_TypeDef;

extern Flash_HandleTypeDef hflash;
extern Setting_TypeDef Setting;

void ResumeSetting(void);
void FlashTask_Init(void);
void FlashTask(void);
int FlashTask_Save(void);
void FlashTask_LoadWallet(uint32_t *coins, uint32_t *score);
int FlashTask_SaveWallet(uint32_t coins, uint32_t score, uint8_t save_setting);

#endif
