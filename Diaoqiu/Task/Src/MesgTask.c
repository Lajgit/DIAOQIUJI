#include "MainTask.h"
#include "MesgTask.h"
#include "CommTask.h"
#include "CtrlTask.h"
#include "FlashTask.h"
#include "InterruptTask.h"
#include "DigitalTubeTask.h"
#include "LightTask.h"
#include "string.h"

extern Tx_HandleTypeDef Tx3;
extern Tx_HandleTypeDef Tx1;
extern Motor_Hoolle Motor_Hoolle1;
extern Motor_Card Card;
extern Scene_t Scene;

Game_State_t Game;
/* 中文注释：人物按效果图1~9排列，只算三横三竖；新档位逐项独立奖励。 */
static const uint16_t GridLines[6] = {0x007U, 0x038U, 0x1C0U, 0x049U, 0x092U, 0x124U};
static const uint16_t LinePoints[7] = {0U, 20U, 50U, 100U, 180U, 300U, 500U};
static uint16_t Button1Pending;
static uint8_t RedeemPending;
static volatile uint8_t ValveActive;
static volatile uint32_t ValveTick;
static uint8_t IdleReturnPending;
static uint32_t LastCreditDisplay, LastCreditDisplayTick;
static uint8_t LastDisplayMode;

/* 中文注释：001~008全为无人声合成提示音，真实Flash曲号仍须试听核对。 */
#define GAME_MUSIC_COIN 1U
#define GAME_MUSIC_START 2U
#define GAME_MUSIC_CHARACTER 3U
#define GAME_MUSIC_LINE 4U
#define GAME_MUSIC_FULL 5U
#define GAME_MUSIC_REDEEM 6U
#define GAME_MUSIC_FAULT 7U
#define GAME_MUSIC_FAIL 8U
#define GAME_MUSIC_BIT(id) ((uint8_t)(1U << ((id) - 1U)))
static uint8_t MusicPending, CurrentMusic;

static void Game_QueueMusic(uint8_t music)
{
    if (music == CurrentMusic || (Game.Faults != 0U && music != GAME_MUSIC_FAULT))
        return;
    if (music == GAME_MUSIC_FAIL || music == GAME_MUSIC_FULL)
        MusicPending &= GAME_MUSIC_BIT(GAME_MUSIC_FAULT);
    else if (music == GAME_MUSIC_LINE)
        MusicPending &= (uint8_t)~GAME_MUSIC_BIT(GAME_MUSIC_CHARACTER);
    MusicPending |= GAME_MUSIC_BIT(music);
}

static void Game_MusicTask(void)
{
    static const uint8_t Priority[8] = {GAME_MUSIC_FAULT, GAME_MUSIC_FAIL, GAME_MUSIC_FULL,
        GAME_MUSIC_LINE, GAME_MUSIC_REDEEM, GAME_MUSIC_COIN, GAME_MUSIC_START, GAME_MUSIC_CHARACTER};
    uint8_t i, music;
    if (!YC686.Ready || YC686.Busy)
        return;
    if (CurrentMusic != 0U && YC686.StateValid && YC686.PlayState == 0U)
        CurrentMusic = 0U;
    /* Busy只是命令事务；普通提示等实际停止，成功/失败/故障可抢占普通提示。 */
    if (CurrentMusic != 0U && (MusicPending & (GAME_MUSIC_BIT(GAME_MUSIC_FAULT) |
        GAME_MUSIC_BIT(GAME_MUSIC_FAIL) | GAME_MUSIC_BIT(GAME_MUSIC_FULL))) == 0U)
        return;
    for (i = 0U; i < 8U; i++)
    {
        music = Priority[i];
        if ((MusicPending & GAME_MUSIC_BIT(music)) != 0U)
        {
            if (YC686_SwitchMusic(4U, music, 8U) == HAL_OK)
            {
                MusicPending &= (uint8_t)~GAME_MUSIC_BIT(music);
                CurrentMusic = music;
            }
            return;
        }
    }
}

static void Game_Add(uint32_t *value, uint64_t added)
{
    *value = added <= UINT32_MAX - *value ? *value + (uint32_t)added : UINT32_MAX;
}

void Game_CollectFeedback(void)
{
    uint16_t inputs, balls, cards, remaining_cards, remaining_balls, coins;
    uint32_t units;
    uint64_t charge;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    coins = CoinInputPendingCount;
    inputs = HoolleInputPendingCount;
    balls = BallOutputPendingCount;
    cards = CardOutputPendingCount;
    remaining_cards = Card.Card_num;
    remaining_balls = Motor_Hoolle1.Hoolle_num;
    CoinInputPendingCount = HoolleInputPendingCount = 0U;
    BallOutputPendingCount = CardOutputPendingCount = 0U;
    __set_PRIMASK(primask);
    /* 中文注释：只结算选定的投入源；互通时只加总分，不再同时增加可用币。 */
    units = Setting.InputMode ? inputs : coins;
    if (units != 0U)
    {
        if (Setting.CoinScoreLink)
            Game_Add(&Game.Score, (uint64_t)units * Setting.PointsPerCoin);
        else
            Game_Add(&Game.CoinCredits, units);
        Game_QueueMusic(GAME_MUSIC_COIN);
    }
    /* 出球前预留费用，PE3确认后才扣：每局首颗一次，每球逐颗一次。 */
    if (balls != 0U && Game.BallUnitCost != 0U)
    {
        units = Game.RoundMode ? Game.FeePending : balls;
        charge = (uint64_t)units * Game.BallUnitCost;
        if (Game.BallPayPoints)
            Game.Score = charge <= Game.Score ? Game.Score - (uint32_t)charge : 0U;
        else
            Game.CoinCredits = charge <= Game.CoinCredits ? Game.CoinCredits - (uint32_t)charge : 0U;
        if (Game.RoundMode) Game.FeePending = 0U;
    }
    if (remaining_balls == 0U)
        Game.BallUnitCost = 0U;
    charge = (uint64_t)cards * Game.CardUnitPoints;
    Game.Score = charge <= Game.Score ? Game.Score - (uint32_t)charge : 0U;
    if (remaining_cards == 0U)
        Game.CardUnitPoints = 0U;
}

uint8_t Game_CanRun(void)
{
    return !Game.MenuActive && !Game.Faults && !Game.Failing && !Game.FullFlash;
}

static uint8_t Game_CanPlay(void)
{
    return Game_CanRun() && !Game.WheelBusy;
}

static int Game_Persist(void)
{
    if ((Game.Faults & GAME_FAULT_FLASH) != 0U)
        return 1;
    if (FlashTask_SaveWallet(Game.CoinCredits, Game.Score, 0U) == 0)
        return 0;
    Game_SetFault(GAME_FAULT_FLASH);
    return 1;
}

static void Game_ClearLowerLights(void)
{
    uint8_t character;
    /* 中文注释：逐组沿用原模式1关闭命令，不增加帧类型或猜测新的广播编号。 */
    for (character = 1U; character <= 9U; character++)
        Comm_SendMesg_FillData(&Tx1, Board_to_Ball, BALL_CMD_RGB_MODE, (uint32_t)character << 8, 1U);
}

void Game_Init(void)
{
    memset(&Game, 0, sizeof(Game));
    FlashTask_LoadWallet(&Game.CoinCredits, &Game.Score);
    MusicPending = CurrentMusic = 0U;
    Button1Pending = RedeemPending = ValveActive = IdleReturnPending = 0U;
    LastCreditDisplay = UINT32_MAX;
    LastDisplayMode = 0xFFU;
    LastCreditDisplayTick = HAL_GetTick();
    HAL_GPIO_WritePin(LockOutput_GPIO_Port, LockOutput_Pin, GPIO_PIN_RESET);
    Light_SetGrid(0U, 0U, 0U);
    Light_SetIdle(1U);
    Game_ClearLowerLights();
    DigitalTube_SetTotals(Game.Score, Setting.PointsPerCard ? Game.Score / Setting.PointsPerCard : 0U);
}

static void Game_EndRound(uint8_t success)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* 先结清物理反馈再取消未出的球；未确认的预留费用不扣，卡片未出任务保留。 */
    Game_CollectFeedback();
    Ctrl_PauseOutputs();
    Motor_Hoolle1.Hoolle_num = 0U;
    Game.BallUnitCost = 0U;
    Game.FeePending = 0U;
    Game.WheelPending = 0U;
    Game.WheelBusy = 0U;
    Game.RoundActive = 0U;
    Button1Pending = RedeemPending = 0U;
    Ctrl_ResetFaults();
    ValveTick = HAL_GetTick();
    if (success)
    {
        Game.FullFlash = 1U;
        Game.FullFlashTick = ValveTick;
    }
    else
    {
        Game.GridMask = Game.HitMask = 0U;
        Game.LineCount = 0U;
        Game.FailTick = ValveTick;
        Game.Failing = 1U;
    }
    /* 中文注释：失败和满盘成功均驱动PA0一秒；独立阀计时不改变成功动画/失败状态。 */
    ValveActive = 1U;
    HAL_GPIO_WritePin(LockOutput_GPIO_Port, LockOutput_Pin, GPIO_PIN_SET);
    __set_PRIMASK(primask);
    /* 失败先保持原清图案效果，10秒未重开再转全白待机；成功动画保持原规则。 */
    IdleReturnPending = success ? 0U : 1U;
    Light_SetIdle(0U);
    Light_SetGrid(Game.GridMask, Game.LineCount, Game.FullFlash);
    if (!success)
        Game_ClearLowerLights();
    Game_QueueMusic(success ? GAME_MUSIC_FULL : GAME_MUSIC_FAIL);
}

static void Game_LightCharacter(uint8_t character)
{
    uint8_t count = 0U, i;
    uint16_t bit = (uint16_t)(1U << (character - 1U));
    uint32_t added = 0U;
    if ((Game.GridMask & bit) != 0U)
        return;
    Game.GridMask |= bit;
    /* 首次有效点亮统一同步下板；重复人物不重新闪，转轮命中也走同一入口。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Ball, BALL_CMD_RGB_MODE, (uint32_t)character << 8, 0U);
    for (i = 0U; i < 6U; i++)
        if ((Game.GridMask & GridLines[i]) == GridLines[i]) count++;
    /* 中文注释：独立累加每个跨过的档位；四线直达六线仍加300+500，整盘1150。 */
    for (i = (uint8_t)(Game.LineCount + 1U); i <= count; i++)
        added += LinePoints[i];
    Game.LineCount = count;
    Game_Add(&Game.Score, added);
    if (Game.GridMask == 0x1FFU)
        Game_EndRound(1U);
    else
    {
        Light_SetGrid(Game.GridMask, count, 0U);
        Game_QueueMusic(added ? GAME_MUSIC_LINE : GAME_MUSIC_CHARACTER);
    }
}

void Game_CharacterHit(uint8_t character)
{
    uint16_t bit;
    if (!Game_CanPlay() || !Game.RoundActive || character < 1U || character > 9U)
        return;
    bit = (uint16_t)(1U << (character - 1U));
    /* 实体微动命中次数独立于转轮点亮；同一实体第二次才判负。 */
    if ((Game.HitMask & bit) != 0U)
    {
        Game_EndRound(0U);
        return;
    }
    Game.HitMask |= bit;
    Game_LightCharacter(character);
}

void Game_CharacterHeld(uint8_t character)
{
    /* 中文注释：人物微动持续低700ms直接判负，不补普通命中/计分；转轮禁玩期间也处理卡住。 */
    if (Game_CanRun() && Game.RoundActive && character >= 1U && character <= 9U)
        Game_EndRound(0U);
}

void Game_WheelHit(uint8_t character)
{
    Game.WheelBusy = 0U;
    if (Game_CanRun() && Game.RoundActive && character >= 1U && character <= 9U)
        Game_LightCharacter(character);
    Comm_ClearGameInput();
    if (Game_CanRun()) Ctrl_ResumeOutputs();
}

void Game_WheelTrigger(void)
{
    if (!Game_CanPlay() || !Game.RoundActive || Game.GridMask == 0x1FFU)
        return;
    Game.WheelTick = HAL_GetTick();
    Game.WheelBusy = 1U;
    Game.WheelPending = 1U;
    /* 一秒等待和转动期间，其他输出暂停，只有转轮任务可以运行。 */
    Ctrl_PauseOutputs();
    Ctrl_ResumeOutputs();
}

void Game_ControlButton(uint8_t button, uint8_t event)
{
    if (event != KEY_EVENT_SHORT || !Game_CanPlay()) return;
    if (button == 1U && Button1Pending < UINT16_MAX) Button1Pending++;
    else if (button == 2U) RedeemPending = 1U;
}

void Game_SetMenu(uint8_t active)
{
    Game.MenuActive = active != 0U;
    Button1Pending = RedeemPending = 0U;
    if (Game.MenuActive) Ctrl_PauseOutputs();
    else if (Game_CanRun())
    {
        Comm_ClearGameInput();
        Ctrl_ResumeOutputs();
    }
}

void Game_SetFault(uint8_t fault)
{
    if ((fault & (uint8_t)~Game.Faults) != 0U)
    {
        MusicPending = 0U;
        Game_QueueMusic(GAME_MUSIC_FAULT);
    }
    Game.Faults |= fault;
    Button1Pending = RedeemPending = 0U;
    Ctrl_PauseOutputs();
}

void Game_Recover(uint8_t keep_pending)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    Game_CollectFeedback();
    if (!keep_pending)
    {
        Motor_Hoolle1.Hoolle_num = Card.Card_num = 0U;
        Game.CardUnitPoints = Game.BallUnitCost = 0U;
        Game.WheelPending = Game.WheelBusy = 0U;
    }
    else if (Game.WheelBusy)
        Game.WheelTick = HAL_GetTick();
    Ctrl_ResetFaults();
    __set_PRIMASK(primask);
    /* Flash失败只在后台明确恢复时重试，不能在中断/临界区里写Flash。 */
    if (FlashTask_SaveWallet(Game.CoinCredits, Game.Score, 0U) != 0)
    {
        Game_SetFault(GAME_FAULT_FLASH);
        return;
    }
    Game.Faults = 0U;
    MusicPending &= (uint8_t)~GAME_MUSIC_BIT(GAME_MUSIC_FAULT);
    Comm_ClearGameInput();
    if (Game_CanRun()) Ctrl_ResumeOutputs();
}

int Game_SaveSettings(void)
{
    uint32_t coins = Game.CoinCredits, score = Game.Score;
    uint64_t converted;
    if (Game_Persist() != 0) return 1;
    if (Setting.CoinScoreLink && coins != 0U)
    {
        converted = (uint64_t)score + (uint64_t)coins * Setting.PointsPerCoin;
        if (converted > UINT32_MAX) return 1;
        score = (uint32_t)converted;
        coins = 0U;
    }
    if (FlashTask_SaveWallet(coins, score, 1U) != 0) return 1;
    /* 开启互通时一并转换既有币和待出球的费用预留，避免隐藏余额或重复收费。 */
    if (Game.CoinCredits != coins && Game.BallUnitCost && !Game.BallPayPoints)
    {
        Game.BallUnitCost *= Setting.PointsPerCoin;
        Game.BallPayPoints = 1U;
    }
    Game.CoinCredits = coins;
    Game.Score = score;
    HoolleInput_FilterInit();
    return 0;
}

void Game_Timer1ms(void)
{
    /* 中文注释：PA0一秒释放在既有1ms中断内关断，LCD刷新不会延长释放时间。 */
    if (ValveActive && (uint32_t)(HAL_GetTick() - ValveTick) >= 1000U)
    {
        HAL_GPIO_WritePin(LockOutput_GPIO_Port, LockOutput_Pin, GPIO_PIN_RESET);
        ValveActive = 0U;
    }
}

static uint64_t Game_BallReserved(void)
{
    uint32_t pending = (uint32_t)Motor_Hoolle1.Hoolle_num + BallOutputPendingCount;
    return (uint64_t)(Game.RoundMode ? (pending != 0U && Game.FeePending) : pending) * Game.BallUnitCost;
}

static void Game_RequestBall(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t pending, cost;
    uint64_t required, card_reserved;
    uint8_t mode, fee, points;
    __disable_irq();
    pending = (uint32_t)Motor_Hoolle1.Hoolle_num + BallOutputPendingCount;
    mode = Game.RoundActive ? Game.RoundMode : (uint8_t)Setting.PlayMode;
    fee = Game.RoundActive ? Game.FeePending : 1U;
    points = (uint8_t)Setting.CoinScoreLink;
    cost = Setting.CoinsPerPlay * (points ? Setting.PointsPerCoin : 1U);
    if (pending && Game.BallUnitCost)
    {
        /* 同一未完成批次锁定价格；切换币/分支付后先完成旧批次，再收新请求。 */
        if (points != Game.BallPayPoints) goto finish;
        cost = Game.BallUnitCost;
    }
    required = (uint64_t)(mode ? fee : pending + 1U) * cost;
    card_reserved = (uint64_t)((uint32_t)Card.Card_num + CardOutputPendingCount) * Game.CardUnitPoints;
    if (Motor_Hoolle1.Hoolle_num == UINT16_MAX ||
        (points ? required + card_reserved > Game.Score : required > Game.CoinCredits))
        goto finish;
    if (!Game.RoundActive)
    {
        Game.RoundActive = 1U;
        Game.RoundMode = mode;
        Game.FeePending = mode;
        IdleReturnPending = 0U;
        Light_SetIdle(0U);
        Game_QueueMusic(GAME_MUSIC_START);
    }
    if (!pending)
    {
        Game.BallUnitCost = mode && !Game.FeePending ? 0U : cost;
        Game.BallPayPoints = points;
    }
    Hoolle_Output(&Motor_Hoolle1, 1U);
finish:
    __set_PRIMASK(primask);
}

void Mesg_Task(void)
{
    uint32_t now = HAL_GetTick(), display;
    Game_CollectFeedback();
    if (Game.Failing && (uint32_t)(now - Game.FailTick) >= 1000U)
    {
        HAL_GPIO_WritePin(LockOutput_GPIO_Port, LockOutput_Pin, GPIO_PIN_RESET);
        ValveActive = Game.Failing = 0U;
        MusicPending &= (uint8_t)~GAME_MUSIC_BIT(GAME_MUSIC_FAIL);
        Comm_ClearGameInput();
        if (Game_CanRun()) Ctrl_ResumeOutputs();
    }
    if (Game.FullFlash && (uint32_t)(now - Game.FullFlashTick) >= GAME_SUCCESS_TIME)
    {
        Game.FullFlash = 0U;
        MusicPending &= (uint8_t)~GAME_MUSIC_BIT(GAME_MUSIC_FULL);
        Game.GridMask = Game.HitMask = 0U;
        Game.LineCount = 0U;
        Light_SetGrid(0U, 0U, 0U);
        Game_ClearLowerLights();
        Comm_ClearGameInput();
        if (Game_CanRun()) Ctrl_ResumeOutputs();
    }
    if (IdleReturnPending && !Game.RoundActive && (uint32_t)(now - Game.FailTick) >= 10000U)
    {
        /* 中文注释：只恢复上板LD1~LD85白光，不改币分/故障/后台和下板已清空的灯。 */
        IdleReturnPending = 0U;
        Light_SetIdle(1U);
    }
    if (Game_Persist() == 0 && Game_CanPlay())
    {
        while (Button1Pending != 0U)
        {
            Button1Pending--;
            Game_RequestBall();
        }
        if (RedeemPending)
        {
            uint64_t reserved = Game.BallPayPoints ? Game_BallReserved() : 0U;
            uint32_t available = reserved <= Game.Score ? Game.Score - (uint32_t)reserved : 0U;
            RedeemPending = 0U;
            if (!Card.Card_num && !Game.CardUnitPoints && Setting.PointsPerCard)
            {
                uint32_t cards = available / Setting.PointsPerCard;
                if (cards > UINT16_MAX) cards = UINT16_MAX;
                if (cards)
                {
                    Game.CardUnitPoints = Setting.PointsPerCard;
                    Card_Output(&Card, (uint16_t)cards);
                    Game_QueueMusic(GAME_MUSIC_REDEEM);
                }
            }
        }
    }
    Scene = Game.MenuActive ? SettingScene : (Game.RoundActive && !Game.Faults ? PlayingScene : IdleScene);
    DigitalTube_SetTotals(Game.Score, Setting.PointsPerCard ? Game.Score / Setting.PointsPerCard : 0U);
    display = Setting.CoinScoreLink ? Game.Score : Game.CoinCredits;
    if (display != LastCreditDisplay || LastDisplayMode != Setting.CoinScoreLink || (uint32_t)(now - LastCreditDisplayTick) >= 1000U)
    {
        Comm_SendMesg_FillData(&Tx3, Board_to_Ctrl, CTRL_CMD_DIGITAL_TUBE, display, 0U);
        LastCreditDisplay = display;
        LastCreditDisplayTick = now;
        LastDisplayMode = (uint8_t)Setting.CoinScoreLink;
    }
    Game_MusicTask();
}
