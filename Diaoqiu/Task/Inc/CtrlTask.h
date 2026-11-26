#ifndef __CTRLTASK_H__
#define __CTRLTASK_H__
#include "port_device.h"
#define HoolleMotorTimeout_time 5000U /* 中文注释：出珠正转5秒未确认通过才反转重试。 */
#define WheelMotorTimeout_time 3000U /* 转轮无光眼沿超时保持3秒，不跟随出珠参数。 */
#define CardMotorTimeout_time 3000U
#define HoolleMotorReverse_Time 300U
#define HoolleMotorRetry_Times 2U
#define HoolleMotor_Speed 100U /* 中文注释：出珠电机速度满档，摆动及转轮速度独立。 */
#define HoolleMotor2_Speed 80U
#define HoolleMotor_Dir 0U

typedef struct
{
    motor_t Motor;
    volatile uint16_t Hoolle_num;
    volatile uint8_t RetryCount;
} Motor_Hoolle;
typedef struct
{
    switch_t Switch;
    volatile uint16_t Card_num;
} Motor_Card;
void Device_Init(void);
void CtrlTask(void);
void Hoolle_Output(Motor_Hoolle *Motor, uint16_t num);
void Card_Output(Motor_Card *Card, uint16_t num);
void Ctrl_PauseOutputs(void);
void Ctrl_ResumeOutputs(void);
void Ctrl_ResetFaults(void);
uint8_t Ctrl_OutputsIdle(void);
void Ctrl_WheelAngleIRQ(void);
void Ctrl_WheelResetIRQ(void);
uint8_t Ctrl_WheelTest(uint8_t home);
uint8_t Ctrl_WheelTestBusy(void);
#endif
