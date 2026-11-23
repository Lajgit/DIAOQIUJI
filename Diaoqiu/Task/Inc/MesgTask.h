#ifndef __MESGTASK_H__
#define __MESGTASK_H__

#include "port_event.h"

#define GAME_FAULT_BALL 0x01U
#define GAME_FAULT_CARD 0x02U
#define GAME_FAULT_WHEEL 0x04U
#define GAME_FAULT_FLASH 0x08U
#define GAME_SUCCESS_TIME 7200U

typedef struct
{
    uint32_t CoinCredits;       /* 非互通模式的可用币/珠，已确认出球后才扣费 */
    uint32_t Score;             /* 总分，互通时也是唯一的游玩余额 */
    uint32_t CardUnitPoints;    /* 当前出卡批次锁定的每卡分数 */
    uint32_t BallUnitCost;      /* 当前未出球批次锁定的每球/首次入局费用 */
    uint16_t GridMask;          /* 已点亮图案，包含转轮点亮 */
    uint16_t HitMask;           /* 实体九路微动各自已触发一次，不含转轮 */
    uint16_t WheelPending;
    uint8_t LineCount;
    uint8_t FullFlash;          /* 7.2秒成功动画，期间禁止游玩 */
    uint8_t MenuActive;
    uint8_t Faults;
    uint8_t RoundActive;
    uint8_t RoundMode;          /* 本局收费模式快照，后台改模式从下一局生效 */
    uint8_t FeePending;         /* 每局模式的首次出球费用是否仍待确认 */
    uint8_t BallPayPoints;      /* 未出球费用预留使用币还是分 */
    uint8_t WheelBusy;          /* 含触发后的一秒等待和实际转动过程 */
    volatile uint8_t Failing;
    uint32_t FullFlashTick;
    uint32_t WheelTick;
    volatile uint32_t FailTick; /* 1ms中断根据此时间关闭PA0 */
} Game_State_t;

extern Game_State_t Game;

void Game_Init(void);
void Game_CollectFeedback(void);
void Game_CharacterHit(uint8_t character);
void Game_WheelHit(uint8_t character);
void Game_WheelTrigger(void);
void Game_ControlButton(uint8_t button, uint8_t event);
void Game_SetMenu(uint8_t active);
void Game_SetFault(uint8_t fault);
void Game_Recover(uint8_t keep_pending);
void Game_Timer1ms(void);
int Game_SaveSettings(void);
uint8_t Game_CanRun(void);
void Mesg_Task(void);

#endif
