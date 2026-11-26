#include "KeyTask.h"
#include "port_key.h"
#include "MesgTask.h"
#include "CtrlTask.h"
#include "FlashTask.h"
#include "softspi.h"
#include "string.h"
#include "stdio.h"
#define SETTING_BUTTON_COUNT 3U
#define MENU_COUNT 12U
static GPIO_TypeDef *SettingButton_Port[3] = {KeyBoard3_GPIO_Port, KeyBoard2_GPIO_Port, KeyBoard1_GPIO_Port};
static uint32_t SettingButton_Pins[3] = {KeyBoard3_Pin, KeyBoard2_Pin, KeyBoard1_Pin};
static Key_HandleTypeDef setting_button[3];
static Key_HandleTypeDef *setting_button_list[3];
static SoftwareSPI_HandleTypeDef LcdSpi;
static uint8_t MenuItem, MenuEdit, K3Held, SaveError;
static uint8_t LcdCache[64][16];
static uint8_t LcdFrame[64][16];
static uint32_t MenuDraft;

/* 中文注释：参考Wood/port_lcd12864.c的F8/FA前缀及高低半字节，使用本板PA15/PC10/PC12。 */
static void LCD_Write(uint8_t data, uint8_t command)
{
    uint8_t frame[3] = {command ? 0xF8U : 0xFAU, (uint8_t)(data & 0xF0U), (uint8_t)(data << 4)};
    uint32_t remaining = ((SystemCoreClock + 999999U) / 1000000U) * 75U;
    uint32_t period = SysTick->LOAD + 1U;
    uint32_t previous, current, elapsed;
    SoftwareSPI_Transmit(&LcdSpi, frame, 3U);
    /* 中文注释：12864ZK串口例程每帧后等待75us；使用现有HCLK源SysTick，不改时基、不关闭光眼/串口中断。 */
    previous = SysTick->VAL;
    while (remaining != 0U)
    {
        current = SysTick->VAL;
        elapsed = previous >= current ? previous - current : previous + period - current;
        if (elapsed >= remaining)
            break;
        remaining -= elapsed;
        previous = current;
    }
}

static void LCD_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SoftwareSPI_InitTypeDef spi = {0};
    const uint8_t commands[] = {0x30U, 0x0CU, 0x01U, 0x06U, 0x34U};
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
    /* 中文注释：按屏方例程清GDRAM，上半区横址0~7、下半区横址8~15。 */
    for (i = 0U; i < 64U; i++)
    {
        uint8_t col;
        LCD_Write((uint8_t)(0x80U | (i & 31U)), 1U);
        LCD_Write(i < 32U ? 0x80U : 0x88U, 1U);
        for (col = 0U; col < 16U; col++)
            LCD_Write(0U, 0U);
    }
    LCD_Write(0x36U, 1U);
    memset(LcdCache, 0, sizeof(LcdCache));
}

static uint32_t *Menu_Value(void)
{
    switch (MenuItem)
    {
    case 0U: return &Setting.InputMode;
    case 1U: return &Setting.PlayMode;
    case 2U: return &Setting.CoinsPerPlay;
    case 3U: return &Setting.CoinScoreLink;
    case 4U: return &Setting.PointsPerCoin;
    case 5U: return &Setting.PointsPerCard;
    case 6U: return &Setting.WheelSpeed;
    default: return &Setting.KeepPending;
    }
}

static void Menu_Exit(void)
{
    /* 中文注释：退出后台先停止手动转轮，保存失败留在后台时也不能继续旋转。 */
    if (Ctrl_WheelTestBusy())
        Ctrl_PauseOutputs();
    Game_CollectFeedback();
    if (Game_SaveSettings() == 0)
    {
        MenuEdit = 0U;
        Game_SetMenu(0U);
    }
    else
        SaveError = 1U;
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
        uint32_t min = MenuItem == 6U ? 10U : ((MenuItem == 0U || MenuItem == 1U || MenuItem == 3U || MenuItem == 7U) ? 0U : 1U);
        uint32_t max = (MenuItem == 0U || MenuItem == 1U || MenuItem == 3U || MenuItem == 7U) ? 1U : (MenuItem == 2U ? 99U : (MenuItem == 6U ? 100U : 999U));
        /* 中文注释：数值和二选一项目均循环修改；越过最小/最大值回到另一端。 */
        if (down) MenuDraft = MenuDraft > min ? MenuDraft - 1U : max;
        else MenuDraft = MenuDraft < max ? MenuDraft + 1U : min;
    }
}

static void SettingButton_ShortCallback(uint16_t key_id)
{
    if (key_id < 2U)
        Menu_Move(key_id == 1U);
    else if (Game.MenuActive)
    {
        if (Ctrl_WheelTestBusy() && MenuItem != 11U)
            return;
        SaveError = 0U;
        if (MenuItem == 8U)
        {
            Game_Recover((uint8_t)Setting.KeepPending);
            if (Game.Faults) SaveError = 1U;
        }
        else if (MenuItem == 9U || MenuItem == 10U)
            Ctrl_WheelTest(MenuItem == 9U);
        else if (MenuItem == 11U)
            Menu_Exit();
        else if (MenuEdit)
        {
            /* 中文注释：K3短按确认当前项，编辑中的临时值不提前生效。 */
            uint32_t old;
            Game_CollectFeedback();
            old = *Menu_Value();
            *Menu_Value() = MenuDraft;
            if (Game_SaveSettings() == 0)
                MenuEdit = 0U;
            else
            {
                *Menu_Value() = old;
                SaveError = 1U;
            }
        }
        else
        {
            MenuDraft = *Menu_Value();
            MenuEdit = 1U;
        }
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
        MenuItem = 0U;
        MenuEdit = 0U;
        SaveError = 0U;
    }
    else
    {
        /* 退出菜单也走同一事务保存，失败留在后台；未确认的草稿不生效。 */
        Menu_Exit();
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

/* 中文注释：12像素点阵字形由本机宋体生成，按Unicode查找；图形模式可容纳五行。 */
static const struct { uint16_t code; uint8_t width; uint16_t rows[12]; } LcdFont[] = {
    {0x0020U, 6U, {0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U}}, /*   */
    {0x002AU, 6U, {0x0000U,0x0000U,0x0000U,0x2000U,0xA800U,0x7000U,0x7000U,0xA800U,0x2000U,0x0000U,0x0000U,0x0000U}}, /* * */
    {0x002DU, 6U, {0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0xFC00U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U}}, /* - */
    {0x002FU, 6U, {0x0000U,0x0400U,0x0800U,0x0800U,0x1000U,0x1000U,0x2000U,0x2000U,0x4000U,0x4000U,0x8000U,0x0000U}}, /* / */
    {0x0030U, 6U, {0x0000U,0x0000U,0x7000U,0x8800U,0x8800U,0x8800U,0x8800U,0x8800U,0x8800U,0x7000U,0x0000U,0x0000U}}, /* 0 */
    {0x0031U, 6U, {0x0000U,0x0000U,0x2000U,0x6000U,0x2000U,0x2000U,0x2000U,0x2000U,0x2000U,0x7000U,0x0000U,0x0000U}}, /* 1 */
    {0x0032U, 6U, {0x0000U,0x0000U,0x7000U,0x8800U,0x8800U,0x1000U,0x2000U,0x4000U,0x8000U,0xF800U,0x0000U,0x0000U}}, /* 2 */
    {0x0033U, 6U, {0x0000U,0x0000U,0x7000U,0x8800U,0x0800U,0x3000U,0x0800U,0x0800U,0x8800U,0x7000U,0x0000U,0x0000U}}, /* 3 */
    {0x0034U, 6U, {0x0000U,0x0000U,0x1000U,0x3000U,0x3000U,0x5000U,0x9000U,0xF800U,0x1000U,0x3800U,0x0000U,0x0000U}}, /* 4 */
    {0x0035U, 6U, {0x0000U,0x0000U,0xF800U,0x8000U,0x8000U,0xF000U,0x8800U,0x0800U,0x8800U,0x7000U,0x0000U,0x0000U}}, /* 5 */
    {0x0036U, 6U, {0x0000U,0x0000U,0x3000U,0x4800U,0x8000U,0xB000U,0xC800U,0x8800U,0x8800U,0x7000U,0x0000U,0x0000U}}, /* 6 */
    {0x0037U, 6U, {0x0000U,0x0000U,0x7800U,0x0800U,0x1000U,0x1000U,0x2000U,0x2000U,0x2000U,0x2000U,0x0000U,0x0000U}}, /* 7 */
    {0x0038U, 6U, {0x0000U,0x0000U,0x7000U,0x8800U,0x8800U,0x7000U,0x4800U,0x8800U,0x8800U,0x7000U,0x0000U,0x0000U}}, /* 8 */
    {0x0039U, 6U, {0x0000U,0x0000U,0x7000U,0x8800U,0x8800U,0x9800U,0x6800U,0x0800U,0x9000U,0x6000U,0x0000U,0x0000U}}, /* 9 */
    {0x003AU, 6U, {0x0000U,0x0000U,0x0000U,0x0000U,0x2000U,0x0000U,0x0000U,0x0000U,0x0000U,0x2000U,0x0000U,0x0000U}}, /* : */
    {0x003DU, 6U, {0x0000U,0x0000U,0x0000U,0x0000U,0xFC00U,0x0000U,0xFC00U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U}}, /* = */
    {0x003EU, 6U, {0x0000U,0x0000U,0x4000U,0x2000U,0x1000U,0x0800U,0x0800U,0x1000U,0x2000U,0x4000U,0x0000U,0x0000U}}, /* > */
    {0x4E00U, 12U, {0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U,0xFFE0U,0x0000U,0x0000U,0x0000U,0x0000U,0x0000U}}, /* 一 */
    {0x4E2DU, 12U, {0x0400U,0x0400U,0x0400U,0x7FC0U,0x4440U,0x4440U,0x4440U,0x7FC0U,0x4440U,0x0400U,0x0400U,0x0400U}}, /* 中 */
    {0x4E92U, 12U, {0x0000U,0xFFE0U,0x2000U,0x2000U,0x3F80U,0x2080U,0x4080U,0x4080U,0x7F00U,0x0100U,0x0100U,0xFFE0U}}, /* 互 */
    {0x4FDDU, 12U, {0x1000U,0x17C0U,0x2440U,0x2440U,0x67C0U,0xA100U,0x2FE0U,0x2100U,0x2380U,0x2540U,0x2920U,0x2100U}}, /* 保 */
    {0x5151U, 12U, {0x2080U,0x1100U,0x7FC0U,0x4040U,0x4040U,0x4040U,0x7FC0U,0x1100U,0x1100U,0x1120U,0x2120U,0xC0E0U}}, /* 兑 */
    {0x5165U, 12U, {0x3000U,0x0800U,0x0400U,0x0400U,0x0A00U,0x0A00U,0x0A00U,0x1100U,0x1100U,0x2080U,0x4040U,0x8020U}}, /* 入 */
    {0x5173U, 12U, {0x2080U,0x1100U,0x0000U,0x7FC0U,0x0400U,0x0400U,0xFFE0U,0x0400U,0x0A00U,0x1100U,0x2080U,0xC060U}}, /* 关 */
    {0x51FAU, 12U, {0x0400U,0x4440U,0x4440U,0x4440U,0x7FC0U,0x0400U,0x0400U,0x8420U,0x8420U,0x8420U,0xFFE0U,0x0020U}}, /* 出 */
    {0x5206U, 12U, {0x1100U,0x1100U,0x2080U,0x2080U,0x4040U,0xBFA0U,0x0880U,0x0880U,0x1080U,0x1080U,0x2080U,0x4700U}}, /* 分 */
    {0x5361U, 12U, {0x0800U,0x0800U,0x0FC0U,0x0800U,0x0800U,0xFFE0U,0x0800U,0x0E00U,0x0980U,0x0840U,0x0800U,0x0800U}}, /* 卡 */
    {0x53EFU, 12U, {0x0000U,0xFFE0U,0x0040U,0x0040U,0x7E40U,0x4240U,0x4240U,0x4240U,0x7E40U,0x4040U,0x0040U,0x01C0U}}, /* 可 */
    {0x542FU, 12U, {0x0400U,0x0200U,0x3FE0U,0x2020U,0x2020U,0x3FE0U,0x2000U,0x3FE0U,0x3020U,0x5020U,0x5FE0U,0x9020U}}, /* 启 */
    {0x5907U, 12U, {0x1000U,0x3F80U,0x5100U,0x0E00U,0x3180U,0xC060U,0x3F80U,0x2480U,0x3F80U,0x2480U,0x3F80U,0x2080U}}, /* 备 */
    {0x590DU, 12U, {0x3FE0U,0x4000U,0xBFC0U,0x2040U,0x3FC0U,0x2040U,0x3FC0U,0x1000U,0x3FC0U,0x5080U,0x0F00U,0x70E0U}}, /* 复 */
    {0x5931U, 12U, {0x0400U,0x2400U,0x2400U,0x3F80U,0x4400U,0x8400U,0x0400U,0xFFE0U,0x0A00U,0x1100U,0x2080U,0xC060U}}, /* 失 */
    {0x5B58U, 12U, {0x0800U,0x0800U,0xFFE0U,0x1000U,0x27C0U,0x2080U,0x6100U,0xAFE0U,0x2100U,0x2100U,0x2100U,0x2300U}}, /* 存 */
    {0x5C40U, 12U, {0x7FC0U,0x4040U,0x7FC0U,0x4000U,0x7FE0U,0x4020U,0x5FA0U,0x50A0U,0x50A0U,0x5FA0U,0x5020U,0x81C0U}}, /* 局 */
    {0x5E01U, 12U, {0x03E0U,0xFC00U,0x0400U,0x0400U,0x7FC0U,0x4440U,0x4440U,0x4440U,0x4440U,0x4440U,0x44C0U,0x0400U}}, /* 币 */
    {0x5EA6U, 12U, {0x0200U,0x7FE0U,0x4880U,0x7FE0U,0x4880U,0x4F80U,0x4000U,0x5FC0U,0x4840U,0x4480U,0x4300U,0x9CE0U}}, /* 度 */
    {0x5F00U, 12U, {0x0000U,0x7FC0U,0x1100U,0x1100U,0x1100U,0x1100U,0xFFE0U,0x1100U,0x1100U,0x2100U,0x2100U,0x4100U}}, /* 开 */
    {0x5F0FU, 12U, {0x0140U,0x0120U,0x0100U,0xFFE0U,0x0100U,0x0100U,0x7D00U,0x1100U,0x1080U,0x10A0U,0x1E60U,0xE020U}}, /* 式 */
    {0x5F52U, 12U, {0x1000U,0x17E0U,0x9020U,0x9020U,0x9020U,0x93E0U,0x9020U,0x9020U,0x9020U,0x2020U,0x27E0U,0x4020U}}, /* 归 */
    {0x5F85U, 12U, {0x1100U,0x2100U,0x47C0U,0x9100U,0x2FE0U,0x6080U,0xAFE0U,0x2080U,0x2480U,0x2280U,0x2080U,0x2380U}}, /* 待 */
    {0x603BU, 12U, {0x2080U,0x1100U,0x7FC0U,0x4040U,0x4040U,0x4040U,0x7FC0U,0x0000U,0x0440U,0x5220U,0x50A0U,0x8F80U}}, /* 总 */
    {0x6062U, 12U, {0x2400U,0x2400U,0x2FE0U,0xB400U,0xA480U,0xA4A0U,0xAAC0U,0x2A80U,0x2880U,0x2940U,0x3240U,0x2420U}}, /* 恢 */
    {0x620FU, 12U, {0x0140U,0x0120U,0xF900U,0x09E0U,0x8F00U,0x5140U,0x5140U,0x2140U,0x2080U,0x51A0U,0x5260U,0x8420U}}, /* 戏 */
    {0x6295U, 12U, {0x2780U,0x2480U,0x2480U,0xF480U,0x2860U,0x2000U,0x2FC0U,0x3440U,0xE280U,0x2100U,0x2280U,0x6C60U}}, /* 投 */
    {0x6362U, 12U, {0x2200U,0x2380U,0x2480U,0xF900U,0x27C0U,0x2540U,0x3540U,0xE540U,0x2FE0U,0x2280U,0x2440U,0x6820U}}, /* 换 */
    {0x6545U, 12U, {0x2200U,0x2200U,0x2200U,0xFBE0U,0x2440U,0x2240U,0xFA40U,0x8A40U,0x8A80U,0x8900U,0xFA80U,0x8C60U}}, /* 故 */
    {0x6570U, 12U, {0x1100U,0x5500U,0x1100U,0xFDE0U,0x3240U,0x5940U,0x9540U,0x2140U,0xF940U,0x4880U,0x3140U,0xCA20U}}, /* 数 */
    {0x65B9U, 12U, {0x0800U,0x0400U,0xFFE0U,0x1000U,0x1000U,0x1FC0U,0x1040U,0x1040U,0x2040U,0x2040U,0x4040U,0x8380U}}, /* 方 */
    {0x683CU, 12U, {0x2200U,0x23C0U,0x2440U,0xFA80U,0x2100U,0x7280U,0x6C60U,0xA7C0U,0x2440U,0x2440U,0x27C0U,0x2440U}}, /* 格 */
    {0x6A21U, 12U, {0x2280U,0x2FE0U,0x2280U,0xF7C0U,0x2440U,0x67C0U,0x7440U,0xA7C0U,0x2100U,0x2FE0U,0x2280U,0x2C60U}}, /* 模 */
    {0x6B22U, 12U, {0x0200U,0xFA00U,0x0BE0U,0x8C20U,0x5840U,0x5100U,0x2100U,0x2100U,0x5280U,0x5280U,0x8440U,0x0820U}}, /* 欢 */
    {0x6BCFU, 12U, {0x2000U,0x3FE0U,0x4000U,0xBFC0U,0x2440U,0x2240U,0xFFE0U,0x4840U,0x4440U,0x7FE0U,0x0040U,0x0380U}}, /* 每 */
    {0x6E38U, 12U, {0x1100U,0x8900U,0x7DE0U,0x1200U,0x11C0U,0x9C40U,0x5480U,0x17E0U,0x1480U,0x5480U,0x9480U,0x2D80U}}, /* 游 */
    {0x7247U, 12U, {0x0100U,0x2100U,0x2100U,0x2100U,0x3FE0U,0x2000U,0x2000U,0x3F80U,0x2080U,0x4080U,0x4080U,0x8080U}}, /* 片 */
    {0x73A9U, 12U, {0x0000U,0xF7C0U,0x2000U,0x2000U,0x2FE0U,0xF280U,0x2280U,0x2280U,0x2280U,0x34A0U,0xC4A0U,0x0860U}}, /* 玩 */
    {0x73E0U, 12U, {0x0100U,0xE500U,0x47C0U,0x4900U,0x4100U,0xEFE0U,0x4100U,0x4380U,0x4540U,0x6920U,0xC100U,0x0100U}}, /* 珠 */
    {0x7403U, 12U, {0x0140U,0xF120U,0x2100U,0x2FE0U,0x2100U,0xF580U,0x23A0U,0x2340U,0x2540U,0x3920U,0xC100U,0x0300U}}, /* 球 */
    {0x7528U, 12U, {0x0000U,0x7FC0U,0x4440U,0x4440U,0x7FC0U,0x4440U,0x4440U,0x7FC0U,0x4440U,0x4440U,0x4440U,0x81C0U}}, /* 用 */
    {0x7559U, 12U, {0x1800U,0xE3E0U,0xA120U,0x9120U,0xEA60U,0x0000U,0x7FC0U,0x4440U,0x7FC0U,0x4440U,0x7FC0U,0x4040U}}, /* 留 */
    {0x786EU, 12U, {0x0200U,0xF3C0U,0x2440U,0x2080U,0x47E0U,0x7520U,0xD7E0U,0x5520U,0x57E0U,0x7520U,0x5520U,0x0860U}}, /* 确 */
    {0x7F16U, 12U, {0x4100U,0x4FE0U,0x8820U,0xAFE0U,0xC800U,0x4FE0U,0x8AA0U,0xDAA0U,0x0FE0U,0x2AA0U,0xCAA0U,0x0860U}}, /* 编 */
    {0x7F6EU, 12U, {0x7FC0U,0x4A40U,0x7FC0U,0x0400U,0xFFE0U,0x0400U,0x3F80U,0x2080U,0x2080U,0x2480U,0x2480U,0xFFE0U}}, /* 置 */
    {0x884CU, 12U, {0x1000U,0x27C0U,0x4000U,0x9000U,0x2FE0U,0x6080U,0xA080U,0x2080U,0x2080U,0x2080U,0x2080U,0x2380U}}, /* 行 */
    {0x8BA4U, 12U, {0x4100U,0x2100U,0x0100U,0x0100U,0xE100U,0x2100U,0x2280U,0x2280U,0x2A80U,0x3440U,0x2440U,0x0820U}}, /* 认 */
    {0x8BBEU, 12U, {0x4780U,0x2480U,0x0480U,0x0480U,0xE860U,0x2000U,0x2FC0U,0x2440U,0x2280U,0x3100U,0x2280U,0x0C60U}}, /* 设 */
    {0x8BF7U, 12U, {0x8200U,0x5FE0U,0x0200U,0x0FC0U,0xC200U,0x5FE0U,0x4840U,0x4FC0U,0x4840U,0x4FC0U,0x6840U,0x48C0U}}, /* 请 */
    {0x8D25U, 12U, {0x0100U,0xF900U,0x8A00U,0xABE0U,0xAC40U,0xAA40U,0xAA40U,0xAA40U,0xAA80U,0x2100U,0x5280U,0x8C60U}}, /* 败 */
    {0x8F6CU, 12U, {0x4100U,0x4100U,0xEFC0U,0x4200U,0xA200U,0xAFE0U,0xF400U,0x27C0U,0x3040U,0xE680U,0x2180U,0x2040U}}, /* 转 */
    {0x8F6EU, 12U, {0x2100U,0x2100U,0xF280U,0x4440U,0x6820U,0xA400U,0xF440U,0x2580U,0x3600U,0xE420U,0x2420U,0x23E0U}}, /* 轮 */
    {0x8F91U, 12U, {0x27C0U,0x2440U,0xF7C0U,0x4000U,0x6FE0U,0xA440U,0xF7C0U,0x2440U,0x37C0U,0xE460U,0x2FC0U,0x2040U}}, /* 辑 */
    {0x8FCEU, 12U, {0x8400U,0x59E0U,0x1120U,0x1120U,0xD120U,0x5120U,0x5520U,0x5920U,0x5160U,0x4100U,0x4100U,0xBFE0U}}, /* 迎 */
    {0x8FD0U, 12U, {0x4000U,0x27C0U,0x0000U,0x0000U,0xEFE0U,0x2100U,0x2200U,0x2440U,0x2FE0U,0x2020U,0x5000U,0x8FE0U}}, /* 运 */
    {0x9000U, 12U, {0x4FC0U,0x2840U,0x0FC0U,0x0840U,0xEFC0U,0x2A20U,0x2940U,0x2A80U,0x2C40U,0x2840U,0x5000U,0x8FE0U}}, /* 退 */
    {0x901AU, 12U, {0x9F80U,0x4480U,0x0300U,0x1FC0U,0xD240U,0x5FC0U,0x5240U,0x5FC0U,0x5240U,0x52C0U,0x4000U,0xBFE0U}}, /* 通 */
    {0x901FU, 12U, {0x8200U,0x5FE0U,0x0200U,0x1FC0U,0xD240U,0x5240U,0x5FC0U,0x4700U,0x4A80U,0x5240U,0x4200U,0xBFE0U}}, /* 速 */
    {0x95EDU, 12U, {0x4000U,0x27E0U,0x0020U,0x4220U,0x4220U,0x5FA0U,0x4220U,0x4620U,0x4A20U,0x5220U,0x4620U,0x40E0U}}, /* 闭 */
    {0x969CU, 12U, {0x0100U,0xF7C0U,0x9280U,0x9FE0U,0xA440U,0xA7C0U,0x9440U,0x97C0U,0x9100U,0xEFE0U,0x8100U,0x8100U}}, /* 障 */
    {0x96F6U, 12U, {0x7FC0U,0x0400U,0xFFE0U,0xB5A0U,0x0400U,0x75C0U,0x0A00U,0x3580U,0xDF60U,0x0200U,0x1C00U,0x0300U}}, /* 零 */
};

/* 中文注释：图形模式自己绘制12像素字形，不依赖仅支持四行的16像素内置字库。 */
/* 界面文字常量用UTF-8字节转义，避免ARMCC按本机GBK代码页误解析源码。 */
static uint16_t LCD_NextCode(const char **text)
{
    const uint8_t *p = (const uint8_t *)*text;
    uint16_t code = *p++;
    if ((code & 0xF0U) == 0xE0U && p[0] && p[1])
    {
        code = (uint16_t)(((code & 15U) << 12) | ((p[0] & 63U) << 6) | (p[1] & 63U));
        p += 2U;
    }
    *text = (const char *)p;
    return code;
}

static uint8_t LCD_FontIndex(uint16_t code)
{
    uint8_t i;
    for (i = 0U; i < sizeof(LcdFont) / sizeof(LcdFont[0]); i++)
        if (LcdFont[i].code == code)
            return i;
    return 0U;
}

static uint16_t LCD_TextWidth(const char *text)
{
    uint16_t width = 0U;
    while (*text)
        width += LcdFont[LCD_FontIndex(LCD_NextCode(&text))].width;
    return width;
}

static void LCD_DrawText(uint16_t x, uint8_t y, const char *text)
{
    uint8_t index, row, bit;
    while (*text && x < 128U)
    {
        index = LCD_FontIndex(LCD_NextCode(&text));
        for (row = 0U; row < 12U && y + row < 64U; row++)
            for (bit = 0U; bit < LcdFont[index].width && x + bit < 128U; bit++)
                if (LcdFont[index].rows[row] & (0x8000U >> bit))
                    LcdFrame[y + row][(x + bit) / 8U] |= (uint8_t)(0x80U >> ((x + bit) % 8U));
        x += LcdFont[index].width;
    }
}

static void LCD_DrawPair(uint8_t row, const char *label, const char *value)
{
    uint16_t width = LCD_TextWidth(value);
    LCD_DrawText(0U, (uint8_t)(1U + row * 12U), label);
    LCD_DrawText(width < 128U ? 128U - width : 0U, (uint8_t)(1U + row * 12U), value);
}

void Key_LCDTask(void)
{
    static uint16_t next_word;
    static const char *names[MENU_COUNT] = {"\xE6\x8A\x95\xE5\x85\xA5\xE6\x96\xB9\xE5\xBC\x8F", "\xE6\xB8\xB8\xE7\x8E\xA9\xE6\xA8\xA1\xE5\xBC\x8F", "\xE6\xAF\x8F\xE7\x90\x83\xE5\xB8\x81\xE6\x95\xB0", "\xE5\xB8\x81\xE5\x88\x86\xE4\xBA\x92\xE9\x80\x9A", "\xE6\xAF\x8F\xE5\xB8\x81\xE5\x88\x86\xE6\x95\xB0", "\xE6\xAF\x8F\xE5\x8D\xA1\xE5\x88\x86\xE6\x95\xB0", "\xE8\xBD\xAC\xE8\xBD\xAE\xE9\x80\x9F\xE5\xBA\xA6", "\xE4\xBF\x9D\xE7\x95\x99\xE5\xBE\x85\xE5\x87\xBA", "\xE6\x95\x85\xE9\x9A\x9C\xE6\x81\xA2\xE5\xA4\x8D", "\xE8\xBD\xAC\xE8\xBD\xAE\xE5\xBD\x92\xE9\x9B\xB6", "\xE8\xBD\xAC\xE8\xBD\xAE\xE8\xBD\xAC\xE4\xB8\x80\xE6\xA0\xBC", "\xE9\x80\x80\xE5\x87\xBA"};
    uint16_t n;
    uint8_t row, col, item, saved_item;
    uint32_t value;
    char text[24];
    memset(LcdFrame, 0, sizeof(LcdFrame));
    if (Game.MenuActive)
    {
        const char *title = SaveError ? "\xE4\xBF\x9D\xE5\xAD\x98\xE5\xA4\xB1\xE8\xB4\xA5" : "\xE6\xB8\xB8\xE6\x88\x8F\xE8\xAE\xBE\xE7\xBD\xAE";
        LCD_DrawText((128U - LCD_TextWidth(title)) / 2U, 1U, title);
        saved_item = MenuItem;
        for (row = 1U; row < 5U; row++)
        {
            item = (uint8_t)((saved_item / 4U) * 4U + row - 1U);
            if (item >= MENU_COUNT)
                break;
            if (item == saved_item)
                LCD_DrawText(0U, (uint8_t)(1U + row * 12U), MenuEdit ? "*" : ">");
            const char *name = names[item];
            if (item == 2U)
                name = Setting.InputMode ? (Setting.PlayMode ? "\xE6\xAF\x8F\xE5\xB1\x80\xE7\x8F\xA0\xE6\x95\xB0" : "\xE6\xAF\x8F\xE7\x90\x83\xE7\x8F\xA0\xE6\x95\xB0") : (Setting.PlayMode ? "\xE6\xAF\x8F\xE5\xB1\x80\xE5\xB8\x81\xE6\x95\xB0" : "\xE6\xAF\x8F\xE7\x90\x83\xE5\xB8\x81\xE6\x95\xB0");
            else if (item == 4U && Setting.InputMode)
                name = "\xE6\xAF\x8F\xE7\x8F\xA0\xE5\x88\x86\xE6\x95\xB0";
            LCD_DrawText(8U, (uint8_t)(1U + row * 12U), name);
            MenuItem = item;
            value = item == saved_item && MenuEdit ? MenuDraft : *Menu_Value();
            MenuItem = saved_item;
            if (item == 0U)
                strcpy(text, value ? "\xE6\x8A\x95\xE7\x8F\xA0" : "\xE6\x8A\x95\xE5\xB8\x81");
            else if (item == 1U)
                strcpy(text, value ? "\xE6\xAF\x8F\xE5\xB1\x80" : "\xE6\xAF\x8F\xE7\x90\x83");
            else if (item == 3U || item == 7U)
                strcpy(text, value ? "\xE5\xBC\x80\xE5\x90\xAF" : "\xE5\x85\xB3\xE9\x97\xAD");
            else if (item == 9U || item == 10U)
                strcpy(text, Game.Faults ? "\xE6\x95\x85\xE9\x9A\x9C" : (Ctrl_WheelTestBusy() ? "\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD" : "\xE7\xA1\xAE\xE8\xAE\xA4"));
            else if (item == 8U || item == 11U)
                strcpy(text, "\xE7\xA1\xAE\xE8\xAE\xA4");
            else
                sprintf(text, "%lu", (unsigned long)value);
            LCD_DrawText(128U - LCD_TextWidth(text), (uint8_t)(1U + row * 12U), text);
        }
    }
    else
    {
        const char *title = Game.Faults ? "\xE8\xAE\xBE\xE5\xA4\x87\xE6\x95\x85\xE9\x9A\x9C" : "\xE6\xAC\xA2\xE8\xBF\x8E\xE6\xB8\xB8\xE6\x88\x8F";
        LCD_DrawText((128U - LCD_TextWidth(title)) / 2U, 1U, title);
        sprintf(text, "%lu%s1%s", (unsigned long)(Setting.CoinsPerPlay * (Setting.CoinScoreLink ? Setting.PointsPerCoin : 1U)),
            Setting.CoinScoreLink ? "\xE5\x88\x86" : (Setting.InputMode ? "\xE7\x8F\xA0" : "\xE5\xB8\x81"), Setting.PlayMode ? "\xE5\xB1\x80" : "\xE7\x90\x83");
        LCD_DrawPair(1U, Setting.InputMode ? "\xE8\xAF\xB7\xE6\x8A\x95\xE7\x8F\xA0" : "\xE8\xAF\xB7\xE6\x8A\x95\xE5\xB8\x81", text);
        row = 2U;
        if (!Setting.CoinScoreLink)
        {
            sprintf(text, "%lu", (unsigned long)Game.CoinCredits);
            LCD_DrawPair(row++, Setting.InputMode ? "\xE5\x8F\xAF\xE7\x94\xA8\xE7\x8F\xA0\xE6\x95\xB0" : "\xE5\x8F\xAF\xE7\x94\xA8\xE5\xB8\x81\xE6\x95\xB0", text);
        }
        sprintf(text, "%lu", (unsigned long)Game.Score);
        LCD_DrawPair(row++, "\xE6\x80\xBB\xE5\x88\x86\xE6\x95\xB0", text);
        sprintf(text, "%lu", (unsigned long)(Setting.PointsPerCard ? Game.Score / Setting.PointsPerCard : 0U));
        LCD_DrawPair(row, "\xE5\x8F\xAF\xE5\x85\x91\xE6\x8D\xA2\xE5\x8D\xA1\xE7\x89\x87", text);
    }
    /* 每轮至多发送一个16像素字，游戏和电机动作期间也能增量刷新；不关闭中断。 */
    for (n = 0U; n < 512U; n++)
    {
        uint16_t word = next_word++;
        if (next_word == 512U) next_word = 0U;
        row = (uint8_t)(word / 8U);
        col = (uint8_t)((word % 8U) * 2U);
        if (memcmp(&LcdFrame[row][col], &LcdCache[row][col], 2U) == 0)
            continue;
        LCD_Write((uint8_t)(0x80U | (row & 31U)), 1U);
        LCD_Write((uint8_t)(0x80U | (row < 32U ? word % 8U : 8U + word % 8U)), 1U);
        LCD_Write(LcdFrame[row][col], 0U);
        LCD_Write(LcdFrame[row][col + 1U], 0U);
        memcpy(&LcdCache[row][col], &LcdFrame[row][col], 2U);
        break;
    }
}
