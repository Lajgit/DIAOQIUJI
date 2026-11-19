#include "KeyTask.h"
#include "port_key.h"
#include "MesgTask.h"
#include "CtrlTask.h"
#include "FlashTask.h"
#include "softspi.h"
#include "string.h"
#include "stdio.h"
#define SETTING_BUTTON_COUNT 3U
#define MENU_COUNT 5U
static GPIO_TypeDef *SettingButton_Port[3] = {KeyBoard3_GPIO_Port, KeyBoard2_GPIO_Port, KeyBoard1_GPIO_Port};
static uint32_t SettingButton_Pins[3] = {KeyBoard3_Pin, KeyBoard2_Pin, KeyBoard1_Pin};
static Key_HandleTypeDef setting_button[3];
static Key_HandleTypeDef *setting_button_list[3];
static SoftwareSPI_HandleTypeDef LcdSpi;
static uint8_t MenuItem, MenuEdit, K3Held, SaveError;
static uint8_t LcdCache[4][16];

/* 中文注释：参考Wood/port_lcd12864.c的F8/FA前缀及高低半字节，使用本板PA15/PC10/PC12。 */
static void LCD_Write(uint8_t data, uint8_t command)
{
    uint8_t frame[3] = {command ? 0xF8U : 0xFAU, (uint8_t)(data & 0xF0U), (uint8_t)(data << 4)};
    SoftwareSPI_Transmit(&LcdSpi, frame, 3U);
}

static void LCD_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SoftwareSPI_InitTypeDef spi = {0};
    const uint8_t commands[] = {0x30U, 0x0CU, 0x01U, 0x06U, 0x34U, 0x30U};
    uint8_t i;
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, LCD_SCK_Pin | LCD_MOSI_Pin, GPIO_PIN_RESET);
    gpio.Pin = LCD_CS_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LCD_CS_GPIO_Port, &gpio);
    gpio.Pin = LCD_SCK_Pin | LCD_MOSI_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);
    spi.CS_Port = LCD_CS_GPIO_Port; spi.CS_Pin = LCD_CS_Pin;
    spi.CLK_Port = LCD_SCK_GPIO_Port; spi.CLK_Pin = LCD_SCK_Pin;
    spi.SDA_Port = LCD_MOSI_GPIO_Port; spi.SDA_Pin = LCD_MOSI_Pin;
    spi.CLK_CPOL = 0U; spi.CS_Level = GPIO_PIN_SET;
    spi.DelayTick = 500U;
    SoftwareSPI_Init(&LcdSpi, spi);
    HAL_Delay(50U);
    for (i = 0U; i < sizeof(commands); i++)
    {
        LCD_Write(commands[i], 1U);
        HAL_Delay(i == 2U ? 15U : 5U);
    }
    memset(LcdCache, 0, sizeof(LcdCache));
}

static uint32_t *Menu_Value(void)
{
    switch (MenuItem)
    {
    case 0U: return &Setting.InputBallsPerCredit;
    case 1U: return &Setting.PointsPerCard;
    case 2U: return &Setting.WheelSpeed;
    default: return &Setting.KeepPending;
    }
}

static void Menu_Move(uint8_t down)
{
    if (!Game.MenuActive)
        return;
    SaveError = 0U;
    if (!MenuEdit)
        MenuItem = (uint8_t)((MenuItem + (down ? 1U : MENU_COUNT - 1U)) % MENU_COUNT);
    else
    {
        uint32_t *value = Menu_Value();
        uint32_t min = MenuItem == 2U ? 10U : (MenuItem == 3U ? 0U : 1U);
        uint32_t max = MenuItem == 0U ? 99U : (MenuItem == 1U ? 999U : (MenuItem == 2U ? 100U : 1U));
        if (down && *value > min) (*value)--;
        if (!down && *value < max) (*value)++;
    }
}

static void SettingButton_ShortCallback(uint16_t key_id)
{
    if (key_id < 2U)
        Menu_Move(key_id == 1U);
    else if (Game.MenuActive)
    {
        SaveError = 0U;
        if (MenuItem == 4U)
            Game_Recover((uint8_t)Setting.KeepPending);
        else
            MenuEdit = !MenuEdit;
    }
}

static void SettingButton_LongCallback(uint16_t key_id)
{
    if (key_id < 2U)
    {
        Menu_Move(key_id == 1U);
        return;
    }
    if (K3Held)
        return;
    K3Held = 1U;
    if (!Game.MenuActive)
    {
        Game_SetMenu(1U);
        MenuEdit = 0U;
        SaveError = 0U;
    }
    else
    {
        /* 保存期间执行器保持暂停，失败时停留后台，不能带着未保存设置重启出珠。 */
        if (FlashTask_Save() == 0)
        {
            MenuEdit = 0U;
            Game_SetMenu(0U);
        }
        else
            SaveError = 1U;
    }
}

static void SettingButton_ReleaseCallback(uint16_t key_id)
{
    if (key_id == 2U)
        K3Held = 0U;
}

void KeyAll_Init(void)
{
    Key_InitTypeDef init;
    uint16_t i;
    for (i = 0U; i < SETTING_BUTTON_COUNT; i++)
    {
        init.short_callback = SettingButton_ShortCallback;
        init.long_callback = SettingButton_LongCallback;
        init.release_callback = SettingButton_ReleaseCallback;
        init.debounce_time = KEY_DEBOUNCE_TIME;
        init.longpress_time = KEY_LONG_PRESS_TIME;
        init.trigger_frequnecy = KEY_LONG_TRIGGER_FREQUENCY;
        init.trigger_level = GPIO_PIN_RESET;
        init.key_id = i;
        init.port = SettingButton_Port[i];
        init.pin = SettingButton_Pins[i];
        Key_Init(&setting_button[i], init);
        setting_button_list[i] = &setting_button[i];
    }
    LCD_Init();
}

void Key_Task(void)
{
    Key_Scan(setting_button_list, SETTING_BUTTON_COUNT);
}

/* 把ASCII转为参考屏使用的全角字，中文常量用GBK字节，避免UTF-8被液晶误识别。 */
static void LCD_RowText(uint8_t row[16], const char *text)
{
    uint8_t pos = 0U;
    memset(row, 0xA1, 16U);
    while (*text && pos < 16U)
    {
        uint8_t ch = (uint8_t)*text++;
        if (ch < 0x80U)
        {
            row[pos++] = 0xA3U;
            row[pos++] = (uint8_t)(ch + 0x80U);
        }
        else
        {
            row[pos++] = ch;
            if (*text && pos < 16U)
                row[pos++] = (uint8_t)*text++;
        }
    }
}

void Key_LCDTask(void)
{
    static const uint8_t address[4] = {0x80U, 0x90U, 0x88U, 0x98U};
    static const char *names[5] = {"\xCD\xB6\xD6\xE9\xB1\xC8\xC0\xFD", "\xC3\xBF\xBF\xA8\xB7\xD6\xCA\xFD", "\xD7\xAA\xC2\xD6\xCB\xD9\xB6\xC8", "\xB1\xA3\xC1\xF4\xB4\xFD\xB3\xF6", "\xB9\xCA\xD5\xCF\xBB\xD6\xB8\xB4"};
    uint8_t rows[4][16], row, col;
    char text[16];
    /* 电机动作期间不整屏阻塞刷新，光眼中断始终打开；设置时所有执行器已暂停。 */
    if (!Ctrl_OutputsIdle())
        return;
    if (Game.MenuActive)
    {
        LCD_RowText(rows[0], SaveError ? "\xB1\xA3\xB4\xE6\xCA\xA7\xB0\xDC\xC7\xEB\xD6\xD8\xCA\xD4" : "\xB5\xF4\xD6\xE9\xBB\xFA\xC9\xE8\xD6\xC3");
        LCD_RowText(rows[1], names[MenuItem]);
        if (MenuItem == 4U)
            LCD_RowText(rows[2], "\xB6\xCC\xB0\xB4\xC8\xB7\xC8\xCF\xBB\xD6\xB8\xB4");
        else
        {
            sprintf(text, "%s%lu", MenuEdit ? ">" : "=", (unsigned long)*Menu_Value());
            LCD_RowText(rows[2], text);
        }
        LCD_RowText(rows[3], MenuEdit ? "\xC9\xCF\xCF\xC2\xD0\xDE\xB8\xC4\xC8\xB7\xC8\xCF\xB4\xE6" : "\xB3\xA4\xB0\xB4\xB1\xA3\xB4\xE6\xCD\xCB\xB3\xF6");
    }
    else
    {
        LCD_RowText(rows[0], Game.Faults ? "\xB9\xCA\xD5\xCF\xC7\xEB\xBD\xF8\xC8\xEB\xC9\xE8\xD6\xC3" : "\xB5\xF4\xD6\xE9\xBB\xFA\xD4\xCB\xD0\xD0");
        sprintf(text, "B%lu", (unsigned long)Game.BallCredits);
        LCD_RowText(rows[1], text);
        sprintf(text, "S%lu", (unsigned long)Game.Score);
        LCD_RowText(rows[2], text);
        sprintf(text, "F%u", (unsigned int)Game.Faults);
        LCD_RowText(rows[3], Game.Faults ? text : "\xB3\xA4\xB0\xB4\xC8\xFD\xBD\xF8\xC8\xEB\xC9\xE8\xD6\xC3");
    }
    /* 每次只更新一行，缩短对主循环的占用；未变化的行不发送。 */
    for (row = 0U; row < 4U; row++)
    {
        if (memcmp(rows[row], LcdCache[row], 16U) == 0)
            continue;
        LCD_Write(address[row], 1U);
        for (col = 0U; col < 16U; col++)
            LCD_Write(rows[row][col], 0U);
        memcpy(LcdCache[row], rows[row], 16U);
        break;
    }
}
