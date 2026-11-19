#ifndef __MESGTASK_H__
#define __MESGTASK_H__

#include "port_event.h"

#define GAME_FAULT_BALL 0x01U
#define GAME_FAULT_CARD 0x02U
#define GAME_FAULT_WHEEL 0x04U

typedef struct
{
    uint32_t BallCredits;
    uint32_t InputRemainder;
    uint32_t Score;
    uint32_t CardUnitPoints;
    uint16_t GridMask;
    uint16_t WheelPending;
    uint8_t LineCount;
    uint8_t FullFlash;
    uint8_t MenuActive;
    uint8_t Faults;
    uint32_t FullFlashTick;
} Game_State_t;

extern Game_State_t Game;

void Game_Init(void);
void Game_CharacterHit(uint8_t character);
void Game_WheelTrigger(void);
void Game_ControlButton(uint8_t button, uint8_t event);
void Game_SetMenu(uint8_t active);
void Game_SetFault(uint8_t fault);
void Game_Recover(uint8_t keep_pending);
uint8_t Game_CanRun(void);
void Mesg_Task(void);

#endif
