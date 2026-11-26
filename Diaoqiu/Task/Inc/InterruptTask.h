#ifndef __INTERRUPTTASK_H__
#define __INTERRUPTTASK_H__
#include "stdint.h"
extern volatile uint16_t HoolleInputPendingCount;
extern volatile uint16_t CoinInputPendingCount;
extern volatile uint16_t BallOutputPendingCount;
extern volatile uint16_t CardOutputPendingCount;
void HoolleInput_FilterInit(void);
void HoolleInput_Scan1ms(void);
void HoolleOutput_FilterReset(void);
#endif
