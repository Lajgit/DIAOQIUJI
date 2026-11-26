#ifndef __LIGHTTASK_H__
#define __LIGHTTASK_H__
#include "stdint.h"
void LightTask_Init(void);
void LightTask(void);
void Light_SetGrid(uint16_t mask, uint8_t lines, uint8_t flash);
void Light_SetIdle(uint8_t idle);
#endif
