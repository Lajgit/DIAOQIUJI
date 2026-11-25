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

extern Event_Handle_t Event;
extern Motor_Card Card;
extern Motor_Hoolle Motor_Hoolle1;
extern Motor_Hoolle Motor_Hoolle2;
extern ListHandle_t ResendList, DealList;

/* 中文注释：参考Wood的停止/音量/模式/播放接口；YC686B独立使用5A帧和正序1021 CRC。 */
#define VOICE_RESPONSE_MS 500U
#define VOICE_RETRY_MAX 3U
#define VOICE_RX_GAP_MS 100U
#define VOICE_FRAME_MAX 64U
#define VOICE_OP_NONE 0U
#define VOICE_OP_STOP 1U
#define VOICE_OP_VOLUME 2U
#define VOICE_OP_MODE 3U
#define VOICE_OP_PLAY 4U
#define VOICE_OP_SWITCH 5U

YC686_State_t YC686;
static RingBuffer_t VoiceRing;
static uint8_t VoiceRingData[128], VoiceRxByte;
static uint8_t VoiceFrame[VOICE_FRAME_MAX], VoiceFrameCount;
static volatile uint8_t VoiceRxReset;
static uint8_t VoiceNotify, VoiceOperation, VoiceStage, VoiceWaiting, VoiceCommand, VoiceRetries;
static uint8_t VoiceParams[2], VoiceParamCount, VoiceVolume, VoiceMode;
static uint16_t VoiceTrack;
static uint32_t VoiceSentTick, VoicePollTick, VoiceRxTick;

static uint16_t YC686_Crc(const uint8_t *data, uint8_t size)
{
    uint16_t crc = 0xFFFFU;
    uint8_t i;
    while (size-- != 0U)
    {
        crc ^= (uint16_t)*data++ << 8;
        for (i = 0U; i < 8U; i++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
    }
    return crc ^ 0xFFFFU;
}

static HAL_StatusTypeDef YC686_Send(uint8_t command, const uint8_t *params, uint8_t count)
{
    uint8_t data[8], i;
    uint16_t crc;
    data[0] = 0x5AU;
    data[1] = count + 1U;
    data[2] = command;
    for (i = 0U; i < count; i++)
        data[3U + i] = params[i];
    crc = YC686_Crc(data, count + 3U);
    data[3U + count] = (uint8_t)(crc >> 8);
    data[4U + count] = (uint8_t)crc;
    data[5U + count] = 0xA5U;
    /* 每轮只发一帧，最多8字节约8.4ms；不关闭中断，不一次阻塞发送四条控制指令。 */
    return HAL_UART_Transmit(&huart2, data, count + 6U, 20U);
}

static void YC686_Init(void)
{
    memset(&YC686, 0, sizeof(YC686));
    YC686.LastAck = 0xFFU;
    YC686.Volume = 0xFFU;
    YC686.Mode = 0xFFU;
    VoiceFrameCount = VoiceNotify = VoiceOperation = VoiceStage = VoiceWaiting = VoiceRetries = 0U;
    VoiceRxReset = 0U;
    VoicePollTick = VoiceRxTick = HAL_GetTick();
    RingBuffer_Init(&VoiceRing, VoiceRingData, sizeof(VoiceRingData));
    if (HAL_UART_Receive_IT(&huart2, &VoiceRxByte, 1U) != HAL_OK)
        VoiceRxReset = 1U;
}

void YC686_ReceiveIRQ(void)
{
    if (VoiceRing.f_WriteByte != NULL && !VoiceRing.f_WriteByte(&VoiceRing, VoiceRxByte))
        VoiceRxReset = 1U;
    if (HAL_UART_Receive_IT(&huart2, &VoiceRxByte, 1U) != HAL_OK)
        VoiceRxReset = 1U;
}

void YC686_ErrorIRQ(void)
{
    /* 中断只标记，主循环重置接收并重新挂单字节中断；不在中断内发送或等待。 */
    VoiceRxReset = 1U;
}

static HAL_StatusTypeDef YC686_Request(uint8_t operation)
{
    if (!YC686.Ready || YC686.Busy)
        return HAL_BUSY;
    VoiceOperation = operation;
    VoiceStage = 0U;
    YC686.Busy = 1U;
    if (operation == VOICE_OP_STOP || operation == VOICE_OP_PLAY || operation == VOICE_OP_SWITCH)
        YC686.StateValid = 0U;
    return HAL_OK;
}

HAL_StatusTypeDef YC686_Stop(void)
{
    return YC686_Request(VOICE_OP_STOP);
}

HAL_StatusTypeDef YC686_SetVol(uint8_t volume)
{
    HAL_StatusTypeDef result;
    if (volume > 31U)
        return HAL_ERROR;
    result = YC686_Request(VOICE_OP_VOLUME);
    if (result == HAL_OK)
        VoiceVolume = volume;
    return result;
}

HAL_StatusTypeDef YC686_SetPlayMode(uint8_t mode)
{
    HAL_StatusTypeDef result;
    if (mode > 5U)
        return HAL_ERROR;
    result = YC686_Request(VOICE_OP_MODE);
    if (result == HAL_OK)
        VoiceMode = mode;
    return result;
}

HAL_StatusTypeDef YC686_PlayMusic(uint16_t track)
{
    HAL_StatusTypeDef result;
    if (track == 0U)
        return HAL_ERROR;
    result = YC686_Request(VOICE_OP_PLAY);
    if (result == HAL_OK)
        VoiceTrack = track;
    return result;
}

HAL_StatusTypeDef YC686_SwitchMusic(uint8_t mode, uint16_t track, uint8_t volume)
{
    HAL_StatusTypeDef result;
    if (mode > 5U || track == 0U || volume > 31U)
        return HAL_ERROR;
    result = YC686_Request(VOICE_OP_SWITCH);
    if (result == HAL_OK)
    {
        VoiceMode = mode;
        VoiceVolume = volume;
        VoiceTrack = track;
    }
    return result;
}

static void YC686_DealFrame(void)
{
    uint8_t command = VoiceFrame[2], length = VoiceFrame[1], value = VoiceFrame[3];
    if (command == 0xA2U && length == 1U)
    {
        /* B版初始化完成后才接收控制；模块重启取消旧事务，不重播已经请求的曲目。 */
        YC686.LinkError = YC686.Busy ? YC686_LINK_RESET : YC686_LINK_OK;
        YC686.LastAck = 0xFFU;
        YC686.OnlineValid = 0U;
        YC686.Ready = 1U;
        YC686.StateValid = 0U;
        YC686.Busy = 0U;
        YC686.Volume = YC686.Mode = 0xFFU;
        VoiceOperation = VOICE_OP_NONE;
        VoiceWaiting = VoiceStage = VoiceRetries = 0U;
        VoiceNotify |= 2U;
        VoicePollTick = HAL_GetTick() - VOICE_RESPONSE_MS;
        YC686.LastFeedbackTick = HAL_GetTick();
        return;
    }
    if (command == 0xA1U && length == 2U && value <= 7U)
    {
        YC686.OnlineValid = 1U;
        YC686.OnlineDevices = value;
        YC686.StateValid = 0U;
        VoiceNotify |= 1U;
        VoicePollTick = HAL_GetTick() - VOICE_RESPONSE_MS;
        YC686.LastFeedbackTick = HAL_GetTick();
        return;
    }
    if (!VoiceWaiting || command != VoiceCommand)
        return;
    if ((command == 0x02U || command == 0x30U) && length == 2U && value <= 2U)
    {
        YC686.PlayState = value;
        YC686.StateValid = 1U;
        if (command == 0x30U)
            YC686.Ready = 1U;
    }
    else if (command == 0x14U && length == 2U && value == VoiceParams[0])
        YC686.Volume = value;
    else if (command == 0x10U && length == 2U && value == VoiceParams[0])
        YC686.Mode = value;
    else if (command == 0x0CU && length == 3U && VoiceFrame[4] <= 2U &&
             (value == 0U || (value >= 0xE0U && value <= 0xE8U)))
    {
        YC686.LastAck = value;
        YC686.PlayState = VoiceFrame[4];
        YC686.StateValid = 1U;
        if (value != 0U)
        {
            VoiceOperation = VOICE_OP_NONE;
            YC686.Busy = 0U;
        }
    }
    else
        return;

    VoiceWaiting = VoiceRetries = 0U;
    YC686.LinkError = YC686_LINK_OK;
    YC686.LastFeedbackTick = VoicePollTick = HAL_GetTick();
    if (command != 0x30U && VoiceOperation != VOICE_OP_NONE)
    {
        if (VoiceOperation == VOICE_OP_SWITCH && VoiceStage < 3U)
            VoiceStage++;
        else
        {
            VoiceOperation = VOICE_OP_NONE;
            YC686.Busy = 0U;
        }
    }
}

static void YC686_ReceiveTask(void)
{
    uint8_t data, size;
    uint16_t crc;
    if (VoiceFrameCount && (uint32_t)(HAL_GetTick() - VoiceRxTick) >= VOICE_RX_GAP_MS)
        VoiceFrameCount = 0U;
    while (VoiceRing.f_ReadByte(&VoiceRing, &data))
    {
        VoiceRxTick = HAL_GetTick();
        VoiceFrame[VoiceFrameCount++] = data;
        while (VoiceFrameCount != 0U)
        {
            if (VoiceFrame[0] != 0x5AU || (VoiceFrameCount >= 2U &&
                (VoiceFrame[1] == 0U || VoiceFrame[1] > VOICE_FRAME_MAX - 5U)))
            {
                VoiceFrameCount--;
                memmove(VoiceFrame, VoiceFrame + 1, VoiceFrameCount);
                continue;
            }
            if (VoiceFrameCount < 2U)
                break;
            size = VoiceFrame[1] + 5U;
            if (VoiceFrameCount < size)
                break;
            crc = YC686_Crc(VoiceFrame, size - 3U);
            if (VoiceFrame[size - 1U] == 0xA5U && VoiceFrame[size - 3U] == (uint8_t)(crc >> 8) &&
                VoiceFrame[size - 2U] == (uint8_t)crc)
            {
                YC686_DealFrame();
                VoiceFrameCount -= size;
                memmove(VoiceFrame, VoiceFrame + size, VoiceFrameCount);
            }
            else
            {
                VoiceFrameCount--;
                memmove(VoiceFrame, VoiceFrame + 1, VoiceFrameCount);
            }
        }
    }
}

static void YC686_Task(void)
{
    uint8_t ack, step;
    uint32_t now = HAL_GetTick();
    if (VoiceRxReset)
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        VoiceRxReset = 0U;
        VoiceRing.f_Clear(&VoiceRing);
        VoiceFrameCount = 0U;
        __set_PRIMASK(primask);
        HAL_UART_AbortReceive(&huart2);
        if (HAL_UART_Receive_IT(&huart2, &VoiceRxByte, 1U) != HAL_OK)
            VoiceRxReset = 1U;
        YC686.LinkError = YC686_LINK_UART;
        YC686.StateValid = 0U;
    }
    YC686_ReceiveTask();
    if (VoiceNotify)
    {
        ack = (VoiceNotify & 2U) ? 0xA2U : 0xA1U;
        if (YC686_Send(ack, NULL, 0U) == HAL_OK)
            VoiceNotify &= (ack == 0xA2U ? (uint8_t)~2U : (uint8_t)~1U);
        else
            YC686.LinkError = YC686_LINK_UART;
        return;
    }
    if (VoiceWaiting)
    {
        if ((uint32_t)(now - VoiceSentTick) < VOICE_RESPONSE_MS)
            return;
        if (VoiceRetries >= VOICE_RETRY_MAX)
        {
            VoiceWaiting = 0U;
            YC686.LinkError = YC686_LINK_TIMEOUT;
            YC686.StateValid = 0U;
            /* 超时撤销就绪并取消本组（包括等待查询时提交的请求），只读探测恢复后再接受控制。 */
            YC686.Ready = 0U;
            VoiceOperation = VOICE_OP_NONE;
            YC686.Busy = 0U;
            VoicePollTick = now;
            return;
        }
        VoiceRetries++;
    }
    else
    {
        VoiceParamCount = 0U;
        if (VoiceOperation != VOICE_OP_NONE)
        {
            step = VoiceOperation == VOICE_OP_SWITCH ? VoiceStage : VoiceOperation - 1U;
            if (step == 0U)
                VoiceCommand = 0x02U;
            else if (step == 1U)
            {
                VoiceCommand = 0x14U;
                VoiceParams[0] = VoiceVolume;
                VoiceParamCount = 1U;
            }
            else if (step == 2U)
            {
                VoiceCommand = 0x10U;
                VoiceParams[0] = VoiceMode;
                VoiceParamCount = 1U;
            }
            else
            {
                VoiceCommand = 0x0CU;
                VoiceParams[0] = (uint8_t)(VoiceTrack >> 8);
                VoiceParams[1] = (uint8_t)VoiceTrack;
                VoiceParamCount = 2U;
                YC686.LastAck = 0xFFU;
            }
        }
        /* MCU单独复位可能错过A2；只用无副作用的30查询探测，以合法应答确认就绪，不猜固定启动时长。 */
        else if ((uint32_t)(now - VoicePollTick) >= VOICE_RESPONSE_MS)
            VoiceCommand = 0x30U;
        else
            return;
        VoiceRetries = 0U;
        VoiceWaiting = 1U;
    }
    if (YC686_Send(VoiceCommand, VoiceParams, VoiceParamCount) != HAL_OK)
        YC686.LinkError = YC686_LINK_UART;
    VoiceSentTick = HAL_GetTick();
}

static bool USART_ReceiveMesg_Verify(void *self, void *mesg)
{
    Rx_HandleTypeDef *rx = (Rx_HandleTypeDef *)self;
    Mesg_TypeDef *Rx_mesg = (Mesg_TypeDef *)mesg;
    uint16_t crc16 = CRC16_calculate(rx->Queue.Buf, 11);
    uint16_t mesg_crc16 = ((uint16_t)Rx_mesg->CRC16_H << 8) | Rx_mesg->CRC16_L;
    return crc16 == mesg_crc16;
}

/* 中文注释：USART1连接下球盘，九路人物与两路转轮触发直接交给本地游戏。 */
static void USART1_Deal(void *Rx_mesg)
{
    Mesg_TypeDef *mesg = (Mesg_TypeDef *)Rx_mesg;
    if (mesg->Code1 != Ball_to_Board || mesg->Code2 != BALL_REPORT_EYE)
        return;
    if (mesg->Data4 >= 1U && mesg->Data4 <= 9U)
    {
        if (mesg->ExpandCode == KEY_EVENT_SHORT)
            Game_CharacterHit(mesg->Data4);
        else if (mesg->ExpandCode == KEY_EVENT_LONG)
            Game_CharacterHeld(mesg->Data4);
    }
    else if (mesg->ExpandCode == KEY_EVENT_SHORT && (mesg->Data4 == 10U || mesg->Data4 == 11U))
        Game_WheelTrigger();
}

/* 中文注释：保持通用控台0x02/0x03功能码，拍拍1出珠、拍拍2兑换；不再转发安卓。 */
static void USART3_Deal(void *Rx_mesg)
{
    Mesg_TypeDef *mesg = (Mesg_TypeDef *)Rx_mesg;
    if (mesg->Code1 == Ctrl_to_Board && mesg->Code2 == CTRL_REPORT_BUTTON)
        Game_ControlButton(mesg->Data4, mesg->ExpandCode);
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

    YC686_Init();
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

void Comm_ClearGameInput(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* 中文注释：禁玩结束时丢弃期间残留的输入/半帧，不把旧按键或微动带入新局。 */
    if (Rx1.Handle.RingBuf.f_Clear) Rx1.Handle.RingBuf.f_Clear(&Rx1.Handle.RingBuf);
    if (Rx3.Handle.RingBuf.f_Clear) Rx3.Handle.RingBuf.f_Clear(&Rx3.Handle.RingBuf);
    Rx1.State = Rx3.State = WAIT_HEAD;
    Rx1.Queue.Index = Rx3.Queue.Index = 0U;
    __set_PRIMASK(primask);
}

void CommTask(void)
{
    Rx1.Receive(&Rx1, &Receive1_mesg, 14);
    Rx3.Receive(&Rx3, &Receive3_mesg, 14);
    YC686_Task();
}
