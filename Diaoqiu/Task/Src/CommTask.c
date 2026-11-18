#include "CommTask.h"
#include "MesgTask.h"
#include "CtrlTask.h"
#include "KeyTask.h"
#include "MainTask.h"
#include "FlashTask.h"
#include "app_crc.h"
#include "app_list.h"
#include "string.h"
#include "usart.h"

#define Mesg_Head 0xAA
#define Mesg_Tail 0x55

static bool USART_ReceiveMesg_Verify(void *self, void *mesg);

ListHandle_t ResendList, DealList;
static ListNode_t ResendList_buffer[100];
static ListNode_t DealList_buffer[100];

static Mesg_TypeDef MesgTable[256];
static uint8_t rx1_buffer[512];
static uint8_t rx3_buffer[256];
static Mesg_TypeDef Receive1_mesg;
static Mesg_TypeDef Receive3_mesg;

Tx_HandleTypeDef Tx1;
Tx_HandleTypeDef Tx3;
Rx_HandleTypeDef Rx1;
Rx_HandleTypeDef Rx3;

extern Event_Handle_t Mesg_event;
extern Event_Handle_t Event;
extern Motor_Card Card;
extern Motor_Hoolle Motor_Hoolle1;
extern Motor_Hoolle Motor_Hoolle2;
extern Switch_Valve Lock_Valve;
extern ListHandle_t ResendList, DealList;

static bool USART_ReceiveMesg_Verify(void *self, void *mesg)
{
    Rx_HandleTypeDef *rx = (Rx_HandleTypeDef *)self;
    Mesg_TypeDef *Rx_mesg = (Mesg_TypeDef *)mesg;
    uint16_t crc16 = CRC16_calculate(rx->Queue.Buf, 11);
    uint16_t mesg_crc16 = ((uint16_t)Rx_mesg->CRC16_H << 8) | Rx_mesg->CRC16_L;
    return crc16 == mesg_crc16;
}

/* 中文注释：USART1接收下球盘0x05；保留原解包，旧安卓上报由发送入口统一停用。 */
static void USART1_Deal(void *Rx_mesg)
{
    Mesg_TypeDef *mesg = (Mesg_TypeDef *)Rx_mesg;

    if (mesg->Code1 != Ball_to_Board)
        return;

    switch (mesg->Code2)
    {
    case BALL_REPORT_VERSION:
        /* 球盘版本仅在板间协议中接收，安卓0x00由主控VERSION应答。 */
        break;

    case BALL_REPORT_EYE:
        if (mesg->Data4 >= 1U && mesg->Data4 <= 5U)
        {
            Comm_SendMesg_FillData_withResend(
                &Tx1,
                Board_to_Android,
                t_LightEye,
                (uint32_t)mesg->Data4,
                0x00U,
                &ResendList);
        }
        else if (mesg->Data4 == 6U || mesg->Data4 == 7U)
        {
            /*
             * 中文注释：协议表球盘光眼写1~6，但当前原理图实际是PB3~PB7五路+FB1/FB2两路，共7路。
             * ID6/7映射为安卓黄色两组的编号1/2，ExpandCode=0x01。
             */
            Comm_SendMesg_FillData_withResend(
                &Tx1,
                Board_to_Android,
                t_LightEye,
                (uint32_t)(mesg->Data4 - 5U),
                0x01U,
                &ResendList);
        }
        break;

    default:
        break;
    }
}

/* 中文注释：USART3接收通用控台0x03，保持既有功能码2；不启用安卓转发。 */
static void USART3_Deal(void *Rx_mesg)
{
    Mesg_TypeDef *mesg = (Mesg_TypeDef *)Rx_mesg;

    if (mesg->Code1 != Ctrl_to_Board)
        return;

    switch (mesg->Code2)
    {
    case CTRL_REPORT_VERSION:
        /* 中文注释：安卓协议没有独立控台版本上报功能码，仅接收不转发。 */
        break;

    case CTRL_REPORT_BUTTON:
        Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_Button, (uint32_t)mesg->Data4, mesg->ExpandCode);
        break;

    case CTRL_REPORT_SPECIAL_BUTTON:
        Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SpecialButton, (uint32_t)mesg->Data4, mesg->ExpandCode);
        break;

    case CTRL_REPORT_ENCODER:
        /*
         * 中文注释：沿用弹界编码器控制舵机逻辑：
         * 0x00每次减少1°，0x01每次增加1°，并继续按安卓0x0F原值上报；
         * 0x02编码器按下将舵机归中到90°，安卓0x0F未定义按下事件，因此不转发。
         * 有效控制后200ms释放PWM，达到限位后的同向操作不刷新计时。
         */
        if (mesg->ExpandCode == 0x00U)
        {
            Servo1_Decrease();
            Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_Encoder, 0U, 0x00U);
        }
        else if (mesg->ExpandCode == 0x01U)
        {
            Servo1_Increase();
            Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_Encoder, 0U, 0x01U);
        }
        else if (mesg->ExpandCode == 0x02U)
        {
            Servo1_Reset();
        }
        break;

    default:
        break;
    }
}

static uint8_t USART_SendMesg(Tx_HandleTypeDef *Tx, Mesg_TypeDef *mesg)
{
    static uint8_t ID = 0;
    uint8_t data[14];
    uint16_t crc;

    ID++;
    mesg->ResendID = 0;
    mesg->ID = ID;
    MesgTable[ID] = *mesg;
    memcpy(data, mesg, 14);
    crc = CRC16_calculate(data, 11);
    data[11] = (uint8_t)(crc >> 8);
    data[12] = (uint8_t)crc;
    HAL_UART_Transmit(Tx->huart, data, 14, 100);
    return ID;
}

uint8_t Comm_SendMesg_FillData(Tx_HandleTypeDef *Tx, uint8_t code_1, uint8_t code_2, uint32_t data, uint8_t expandCode)
{
    Mesg_TypeDef mesg = {0};
    /* 中文注释：当前没有安卓板，停用旧安卓出口；不发送、不加入重发队列。
     * USART1已接下球盘，禁止把旧安卓上报误发到板间串口。
     * 控台0x02/0x03、球盘0x04/0x05及其功能码2保持原值。
     */
    if (code_1 == Board_to_Android || code_1 == Android_to_Board)
        return 0U;

    mesg.Head = Mesg_Head;
    mesg.Code1 = code_1;
    mesg.Code2 = code_2;
    mesg.Data1 = (uint8_t)(data >> 24);
    mesg.Data2 = (uint8_t)(data >> 16);
    mesg.Data3 = (uint8_t)(data >> 8);
    mesg.Data4 = (uint8_t)data;
    mesg.ACKbyte = 0x00;
    mesg.ExpandCode = expandCode;
    mesg.Tail = Mesg_Tail;
    return USART_SendMesg(Tx, &mesg);
}

uint8_t Comm_SendMesg_FillData_withResend(Tx_HandleTypeDef *Tx, uint8_t code_1, uint8_t code_2, uint32_t data, uint8_t expandCode, ListHandle_t *List)
{
    uint8_t ID;
    Mesg_TypeDef mesg = {0};
    /* 中文注释：当前没有安卓板，停用旧安卓出口；不发送、不加入重发队列。
     * USART1已接下球盘，禁止把旧安卓上报误发到板间串口。
     * 控台0x02/0x03、球盘0x04/0x05及其功能码2保持原值。
     */
    if (code_1 == Board_to_Android || code_1 == Android_to_Board)
        return 0U;

    mesg.Head = Mesg_Head;
    mesg.Code1 = code_1;
    mesg.Code2 = code_2;
    mesg.Data1 = (uint8_t)(data >> 24);
    mesg.Data2 = (uint8_t)(data >> 16);
    mesg.Data3 = (uint8_t)(data >> 8);
    mesg.Data4 = (uint8_t)data;
    mesg.ACKbyte = 0x01;
    mesg.ExpandCode = expandCode;
    mesg.Tail = Mesg_Tail;
    ID = USART_SendMesg(Tx, &mesg);
    List_AddNode(List, ID, HAL_GetTick());
    return ID;
}

static uint8_t USART_ReSendMesg(Tx_HandleTypeDef *Tx, Mesg_TypeDef *mesg)
{
    uint8_t data[14];
    uint16_t crc;

    mesg->ResendID++;
    if (mesg->ResendID > Max_Resend_Times)
        return 1;

    memcpy(data, mesg, 14);
    data[0] = Mesg_Head;
    data[1] = mesg->ResendID;
    crc = CRC16_calculate(data, 11);
    data[11] = (uint8_t)(crc >> 8);
    data[12] = (uint8_t)crc;
    data[13] = Mesg_Tail;
    HAL_UART_Transmit(Tx->huart, data, 14, 100);
    return 0;
}

void Resend_Task(void)
{
    ListNode_t *Current = ResendList.Head;
    uint32_t CurrentTime = HAL_GetTick();

    for (uint8_t i = 0; i < ResendList.NodeCount; i++)
    {
        if (CurrentTime - Current->Value > ResendTrigger_Time)
        {
            USART_ReSendMesg(&Tx1, &(MesgTable[Current->ID]));
            Current->Value = CurrentTime;
            if (MesgTable[Current->ID].ResendID >= Max_Resend_Times)
                List_DeleteNode(&ResendList, Current->ID);
        }
        Current = Current->Next;
    }
}

void MesgDeal_Task(void)
{
    ListNode_t *Current = DealList.Head;
    uint32_t CurrentTime = HAL_GetTick();

    for (uint8_t i = 0; i < DealList.NodeCount; i++)
    {
        if (CurrentTime - Current->Value > MesgDeal_Time)
            List_DeleteNode(&DealList, Current->ID);
        Current = Current->Next;
    }
}

void CommInit(void)
{
    Rx_InitTypeDef Rxinit;
    Tx_InitTypeDef Tx_init;

    List_Create(&ResendList, ResendList_buffer, 100);
    List_Create(&DealList, DealList_buffer, 100);

    /* 中文注释：USART1接下球盘，USART3接通用控台；两路保持原14字节与CRC16。 */
    Rxinit.Frame_Head = Mesg_Head;
    Rxinit.Frame_Tail = Mesg_Tail;
    Rxinit.Mesg_Len = 14U;
    Rxinit.Receive = Rx_Receive;
    Rxinit.Verify = USART_ReceiveMesg_Verify;

    Rxinit.huart = &huart1;
    Rxinit.RingBuf = rx1_buffer;
    Rxinit.RingBuf_Size = sizeof(rx1_buffer);
    Rxinit.Deal = USART1_Deal;
    Communicate_Rx_Init(&Rx1, Rxinit);

    Rxinit.huart = &huart3;
    Rxinit.RingBuf = rx3_buffer;
    Rxinit.RingBuf_Size = sizeof(rx3_buffer);
    Rxinit.Deal = USART3_Deal;
    Communicate_Rx_Init(&Rx3, Rxinit);

    Tx_init.hdma = NULL;
    Tx_init.TxBuf = NULL;
    Tx_init.TxBuf_Size = 0;

    Tx_init.huart = &huart1;
    Communicate_Tx_Init(&Tx1, Tx_init);
    Tx_init.huart = &huart3;
    Communicate_Tx_Init(&Tx3, Tx_init);
}

void CommTask(void)
{
    Rx1.Receive(&Rx1, &Receive1_mesg, 14);
    Rx3.Receive(&Rx3, &Receive3_mesg, 14);
}
