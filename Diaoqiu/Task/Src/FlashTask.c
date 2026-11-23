#include "MainTask.h"
#include "FlashTask.h"
#include "MesgTask.h"
#include "CtrlTask.h"
#include "port_event.h"
#include "string.h"

Flash_HandleTypeDef hflash;
Setting_TypeDef Setting;
extern Event_Handle_t Event;

/* 中文注释：DIAOQIU仅SD升级，原OTA缓存Sector9/10不再使用，作为两份追加日志。
 * APP结束于0x080A0000之前，旧设置Sector11只读迁移；SD升级不擦除这些数据区。 */
#define WALLET_MAGIC 0x44515732U
#define WALLET_COMMIT 0xC1EAA55AU
#define WALLET_BANK_SIZE 0x20000U
typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    Setting_TypeDef setting;
    uint32_t coins;
    uint32_t score;
    uint32_t crc;
    uint32_t commit;
} Wallet_Record_t;
static const uint32_t WalletAddress[2] = {0x080A0000U, 0x080C0000U};
static uint16_t WalletNext[2];
static uint8_t WalletBank, WalletValid, WalletEraseError;
static uint32_t WalletSequence, WalletCoins, WalletScore;
static Setting_TypeDef WalletSetting;

static uint32_t Wallet_Crc(const Wallet_Record_t *record)
{
    const uint8_t *data = (const uint8_t *)record;
    uint32_t crc = 0xFFFFFFFFU, i;
    uint8_t bit;
    for (i = 0U; i < sizeof(Wallet_Record_t) - 8U; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
    }
    return crc ^ 0xFFFFFFFFU;
}

static uint8_t Wallet_SettingValid(const Setting_TypeDef *setting)
{
    return setting->Board_Lightness <= 10U && setting->LightBelt_Lightness <= 10U &&
        setting->Ctrl_Lightness <= 10U && setting->CoinsPerPlay >= 1U && setting->CoinsPerPlay <= 99U &&
        setting->PointsPerCard >= 1U && setting->PointsPerCard <= 999U &&
        setting->WheelSpeed >= 10U && setting->WheelSpeed <= 100U && setting->KeepPending <= 1U &&
        setting->PlayMode <= 1U && setting->InputMode <= 1U && setting->CoinScoreLink <= 1U &&
        setting->PointsPerCoin >= 1U && setting->PointsPerCoin <= 999U;
}

static int Wallet_Erase(uint8_t bank)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error;
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = bank ? FLASH_SECTOR_10 : FLASH_SECTOR_9;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASH_Unlock() != HAL_OK)
        return 1;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 1;
    }
    HAL_FLASH_Lock();
    WalletNext[bank] = 0U;
    return 0;
}

void ResumeSetting(void)
{
    Setting.Board_Lightness = 5U;
    Setting.LightBelt_Lightness = 5U;
    Setting.Ctrl_Lightness = 5U;
    Setting.CoinsPerPlay = 5U;
    Setting.PointsPerCard = 5U;
    Setting.WheelSpeed = 80U;
    Setting.KeepPending = 1U;
    Setting.PlayMode = 1U;
    Setting.InputMode = 0U;
    Setting.CoinScoreLink = 0U;
    Setting.PointsPerCoin = 1U;
}

void FlashTask_Init(void)
{
    Flash_InitTypeDef init;
    Setting_TypeDef legacy;
    uint8_t bank;
    uint16_t slot;
    Wallet_Record_t record;
    WalletValid = WalletBank = WalletEraseError = 0U;
    WalletCoins = WalletScore = WalletSequence = 0U;
    WalletNext[0] = WalletNext[1] = 0U;
    init.DataSize = sizeof(Setting_TypeDef);
    init.pData = &Setting;
    init.StartAddress = Setting_Addr;
    Flash_Init(&hflash, init);
    memset(&Setting, 0xFF, sizeof(Setting));
    hflash.f_Read(&hflash);
    legacy = Setting;
    ResumeSetting();
    /* 旧七字段仅迁移亮度、卡价、轮速和保留项，旧投珠比例不当成新的游玩价格。 */
    if (legacy.Board_Lightness <= 10U) Setting.Board_Lightness = legacy.Board_Lightness;
    if (legacy.LightBelt_Lightness <= 10U) Setting.LightBelt_Lightness = legacy.LightBelt_Lightness;
    if (legacy.Ctrl_Lightness <= 10U) Setting.Ctrl_Lightness = legacy.Ctrl_Lightness;
    if (legacy.PointsPerCard >= 1U && legacy.PointsPerCard <= 999U) Setting.PointsPerCard = legacy.PointsPerCard;
    if (legacy.WheelSpeed >= 10U && legacy.WheelSpeed <= 100U) Setting.WheelSpeed = legacy.WheelSpeed;
    if (legacy.KeepPending <= 1U) Setting.KeepPending = legacy.KeepPending;
    for (bank = 0U; bank < 2U; bank++)
    {
        for (slot = 0U; slot < WALLET_BANK_SIZE / sizeof(record); slot++)
        {
            uint32_t i;
            const uint32_t *words;
            memcpy(&record, (const void *)(WalletAddress[bank] + slot * sizeof(record)), sizeof(record));
            words = (const uint32_t *)&record;
            for (i = 0U; i < sizeof(record) / 4U; i++)
                if (words[i] != UINT32_MAX)
                {
                    WalletNext[bank] = slot + 1U;
                    break;
                }
            if (record.magic != WALLET_MAGIC || record.commit != WALLET_COMMIT ||
                record.crc != Wallet_Crc(&record) || !Wallet_SettingValid(&record.setting) ||
                (record.setting.CoinScoreLink && record.coins != 0U))
                continue;
            if (!WalletValid || (int32_t)(record.sequence - WalletSequence) > 0)
            {
                WalletValid = 1U;
                WalletBank = bank;
                WalletSequence = record.sequence;
                Setting = record.setting;
                WalletCoins = record.coins;
                WalletScore = record.score;
            }
        }
    }
    WalletSetting = Setting;
    /* 至少留一个可写位置；擦除另一份时，最后完整记录始终保留在原扇区。 */
    if (WalletNext[WalletBank] >= WALLET_BANK_SIZE / sizeof(record))
    {
        bank = WalletValid ? (uint8_t)(WalletBank ^ 1U) : WalletBank;
        if (WalletNext[bank] >= WALLET_BANK_SIZE / sizeof(record))
            Wallet_Erase(bank);
    }
}

void FlashTask_LoadWallet(uint32_t *coins, uint32_t *score)
{
    *coins = WalletCoins;
    *score = WalletScore;
}

int FlashTask_SaveWallet(uint32_t coins, uint32_t score, uint8_t save_setting)
{
    Wallet_Record_t record;
    uint32_t address, i;
    uint8_t bank = WalletBank;
    const uint32_t *words = (const uint32_t *)&record;
    record.setting = save_setting ? Setting : WalletSetting;
    if (!Wallet_SettingValid(&record.setting) || (record.setting.CoinScoreLink && coins != 0U))
        return 1;
    /* 整理失败后不在每轮主循环反复擦除；仅后台明确恢复时重试一次。 */
    if (WalletEraseError)
    {
        if (!Game.MenuActive || Wallet_Erase((uint8_t)(WalletBank ^ 1U)) != 0)
            return 1;
        WalletEraseError = 0U;
    }
    if (WalletValid && coins == WalletCoins && score == WalletScore &&
        memcmp(&record.setting, &WalletSetting, sizeof(Setting)) == 0)
        return 0;
    if (WalletNext[bank] >= WALLET_BANK_SIZE / sizeof(record))
        bank ^= 1U;
    if (WalletNext[bank] >= WALLET_BANK_SIZE / sizeof(record))
        return 1;
    record.magic = WALLET_MAGIC;
    record.sequence = WalletSequence + 1U;
    record.coins = coins;
    record.score = score;
    record.crc = Wallet_Crc(&record);
    record.commit = WALLET_COMMIT;
    address = WalletAddress[bank] + WalletNext[bank] * sizeof(record);
    if (HAL_FLASH_Unlock() != HAL_OK)
        return 1;
    WalletNext[bank]++;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    /* 中文注释：数据和CRC先写，最后一个字才提交；断电留下的半条记录不生效。 */
    for (i = 0U; i < sizeof(record) / 4U; i++)
        if (words[i] != UINT32_MAX && HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i * 4U, words[i]) != HAL_OK)
            break;
    HAL_FLASH_Lock();
    if (memcmp((const void *)address, &record, sizeof(record)) != 0)
        return 1;
    WalletValid = 1U;
    WalletBank = bank;
    WalletSequence = record.sequence;
    WalletSetting = record.setting;
    WalletCoins = coins;
    WalletScore = score;
    return 0;
}

int FlashTask_Save(void)
{
    return FlashTask_SaveWallet(WalletCoins, WalletScore, 1U);
}

void FlashTask(void)
{
    uint8_t other = (uint8_t)(WalletBank ^ 1U);
    /* 不在游戏、转轮、失败释放或成功动画中擦扇区；后台暂停后也可整理旧日志。 */
    if (WalletValid && !WalletEraseError && WalletNext[other] != 0U && Ctrl_OutputsIdle() &&
        !Game.Failing && !Game.FullFlash && (Game.MenuActive || (!Game.RoundActive && !Game.WheelBusy)))
    {
        if (Wallet_Erase(other) != 0)
        {
            WalletEraseError = 1U;
            Game_SetFault(GAME_FAULT_FLASH);
        }
    }
    if (EventGroupCheckBits(&Event, Event_SaveSetting) == true)
    {
        if (FlashTask_Save() == 0)
            EventGroupClearBits(&Event, Event_SaveSetting);
    }
}
