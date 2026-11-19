#ifndef __LIGHTTASK_H__
#define __LIGHTTASK_H__
#include "stdint.h"
void Light_Init(void);
void Light_Task(void);
void BallLight_Hit(uint8_t character);
void BallLight_SetMode(uint8_t character, uint8_t color, uint8_t mode);
void BallLight_SetBrightness(uint8_t brightness);
#endif
