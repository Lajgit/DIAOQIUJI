#include "KeyTask.h"
#include "CommTask.h"
#include "port_key.h"

#define SETTING_BUTTON_COUNT 3U

/*
 * 中文注释：后台设置按键按原理图K1/K2/K3编号：
 * K1=PB6，K2=PB7，K3=PD0，对外协议编号依次为0x01~0x03。
 */
static GPIO_TypeDef *SettingButton_Port[SETTING_BUTTON_COUNT] = {
    KeyBoard3_GPIO_Port,
    KeyBoard2_GPIO_Port,
    KeyBoard1_GPIO_Port,
};
static uint32_t SettingButton_Pins[SETTING_BUTTON_COUNT] = {
    KeyBoard3_Pin,
    KeyBoard2_Pin,
    KeyBoard1_Pin,
};

static Key_HandleTypeDef setting_button[SETTING_BUTTON_COUNT];
static Key_HandleTypeDef *setting_button_list[SETTING_BUTTON_COUNT];

extern Tx_HandleTypeDef Tx1;

/*==============================后台设置按键==============================*/
static void SettingButton_ShortCallback(uint16_t key_id)
{
    /* 中文注释：0x04=设置按键，Data4=编号，ExpandCode=短按。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SettingButton, key_id + 1U, KEY_EVENT_SHORT);
}

static void SettingButton_LongCallback(uint16_t key_id)
{
    /* 中文注释：ExpandCode=长按。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SettingButton, key_id + 1U, KEY_EVENT_LONG);
}

static void SettingButton_ReleaseCallback(uint16_t key_id)
{
    /* 中文注释：ExpandCode=松开。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SettingButton, key_id + 1U, KEY_EVENT_RELEASE);
}

static void SettingButton_Init(void)
{
    Key_InitTypeDef init;

    for (uint16_t i = 0; i < SETTING_BUTTON_COUNT; i++)
    {
        init.short_callback = SettingButton_ShortCallback;
        init.long_callback = SettingButton_LongCallback;
        init.release_callback = SettingButton_ReleaseCallback;
        init.debounce_time = KEY_DEBOUNCE_TIME;
        init.longpress_time = KEY_LONG_PRESS_TIME;
        init.trigger_frequnecy = 1;
        init.trigger_level = GPIO_PIN_RESET;
        init.key_id = i;
        init.port = SettingButton_Port[i];
        init.pin = SettingButton_Pins[i];

        Key_Init(&setting_button[i], init);
        setting_button_list[i] = &setting_button[i];
    }
}

void KeyAll_Init(void)
{
    SettingButton_Init();
}

void Key_Task(void)
{
    Key_Scan(setting_button_list, SETTING_BUTTON_COUNT);
}
