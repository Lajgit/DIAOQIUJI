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
extern Motor_Hoolle Motor_Hoolle1;
extern Motor_Card Card;
extern Scene_t Scene;

Game_State_t Game;
/* 中文注释：仅三横三竖，人物按效果图从左到右、从上到下编号1~9。 */
static const uint16_t GridLines[6] = {0x007U, 0x038U, 0x1C0U, 0x049U, 0x092U, 0x124U};
static const uint16_t LinePoints[7] = {0U, 20U, 50U, 100U, 180U, 300U, 500U};
static uint16_t Button1Pending;
static uint8_t RedeemPending;
static uint32_t LastCreditDisplay;
static uint32_t LastCreditDisplayTick;

/* 中文注释：首轮Flash按001~007复制，真实曲号仍须上机试听确认；单曲一次，音量8。 */
#define GAME_MUSIC_COIN 1U
#define GAME_MUSIC_START 2U
#define GAME_MUSIC_CHARACTER 3U
#define GAME_MUSIC_LINE 4U
#define GAME_MUSIC_FULL 5U
#define GAME_MUSIC_REDEEM 6U
#define GAME_MUSIC_FAULT 7U
#define GAME_MUSIC_BIT(id) ((uint8_t)(1U << ((id) - 1U)))
static uint8_t MusicPending;
static uint8_t CurrentMusic;

static void Game_QueueMusic(uint8_t music)
{
    /* 同类提示合并，故障期间不积压普通提示；这里只记事件，不在临界区发送串口。 */
    if (music == CurrentMusic || (Game.Faults != 0U && music != GAME_MUSIC_FAULT))
        return;
    if (music == GAME_MUSIC_FULL)
        MusicPending &= (uint8_t)~(GAME_MUSIC_BIT(GAME_MUSIC_CHARACTER) | GAME_MUSIC_BIT(GAME_MUSIC_LINE));
    else if (music == GAME_MUSIC_LINE)
        MusicPending &= (uint8_t)~GAME_MUSIC_BIT(GAME_MUSIC_CHARACTER);
    MusicPending |= GAME_MUSIC_BIT(music);
}

static void Game_MusicTask(void)
{
    static const uint8_t Priority[7] = {GAME_MUSIC_FAULT, GAME_MUSIC_FULL, GAME_MUSIC_LINE,
        GAME_MUSIC_REDEEM, GAME_MUSIC_COIN, GAME_MUSIC_START, GAME_MUSIC_CHARACTER};
    uint8_t i, music;
    if (!YC686.Ready || YC686.Busy)
        return;
    /* Busy只是命令事务，B款须查询到有效停止状态才算播放结束。故障可抢占普通提示。 */
    if (CurrentMusic != 0U && YC686.StateValid && YC686.PlayState == 0U)
        CurrentMusic = 0U;
    if (CurrentMusic != 0U && (MusicPending & GAME_MUSIC_BIT(GAME_MUSIC_FAULT)) == 0U)
        return;
    for (i = 0U; i < 7U; i++)
    {
        music = Priority[i];
        if ((MusicPending & GAME_MUSIC_BIT(music)) != 0U)
        {
            /* HAL_OK表示已接受；交给原驱动应答推进，不自动重播超时或缺失的曲目。 */
            if (YC686_SwitchMusic(4U, music, 8U) == HAL_OK)
            {
                MusicPending &= (uint8_t)~GAME_MUSIC_BIT(music);
                CurrentMusic = music;
            }
            return;
        }
    }
}

static void Game_CollectFeedback(void)
{
    uint16_t inputs, balls, cards, remaining_cards, coins;
    uint32_t previous_credits = Game.BallCredits;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    coins = CoinInputPendingCount;
    CoinInputPendingCount = 0U;
    inputs = HoolleInputPendingCount;
    balls = BallOutputPendingCount;
    cards = CardOutputPendingCount;
    remaining_cards = Card.Card_num;
    HoolleInputPendingCount = 0U;
    BallOutputPendingCount = 0U;
    CardOutputPendingCount = 0U;
    __set_PRIMASK(primask);

    /* 中文注释：PE2只累计投珠，不加总分；不足一份兑换比例的余珠继续保留。 */
    Game.InputRemainder += inputs;
    if (Setting.InputBallsPerCredit != 0U)
    {
        uint32_t credits = Game.InputRemainder / Setting.InputBallsPerCredit;
        Game.InputRemainder %= Setting.InputBallsPerCredit;
        if (credits <= UINT32_MAX - Game.BallCredits)
            Game.BallCredits += credits;
        else
            Game.BallCredits = UINT32_MAX;
    }
    Game.BallCredits = balls <= Game.BallCredits ? Game.BallCredits - balls : 0U;

    /* PE6投币只提示，不改变PE2投珠换余额的既定规则。余额0变正仅提示一次开始。 */
    if (coins != 0U)
        Game_QueueMusic(GAME_MUSIC_COIN);
    if (previous_credits == 0U && Game.BallCredits != 0U)
        Game_QueueMusic(GAME_MUSIC_START);

    /* 中文注释：兑换单价在本次出卡开始时锁定，实际PE5反馈后才扣分。 */
    while (cards-- != 0U && Game.CardUnitPoints != 0U)
    {
        if (Game.Score >= Game.CardUnitPoints)
            Game.Score -= Game.CardUnitPoints;
        else
            Game.Score = 0U;
    }
    if (remaining_cards == 0U)
        Game.CardUnitPoints = 0U;
}

uint8_t Game_CanRun(void)
{
    return Game.MenuActive == 0U && Game.Faults == 0U;
}

void Game_Init(void)
{
    memset(&Game, 0, sizeof(Game));
    MusicPending = CurrentMusic = 0U;
    Button1Pending = 0U;
    RedeemPending = 0U;
    LastCreditDisplay = UINT32_MAX;
    LastCreditDisplayTick = HAL_GetTick();
    Light_SetGrid(0U, 0U, 0U);
    DigitalTube_SetTotals(0U, 0U);
}

void Game_CharacterHit(uint8_t character)
{
    uint8_t count = 0U, i, previous_lines = Game.LineCount;
    uint16_t bit;
    if (character < 1U || character > 9U || Game.FullFlash != 0U)
        return;
    bit = (uint16_t)(1U << (character - 1U));
    if ((Game.GridMask & bit) != 0U)
        return;
    Game.GridMask |= bit;
    for (i = 0U; i < 6U; i++)
        if ((Game.GridMask & GridLines[i]) == GridLines[i])
            count++;
    /* 中文注释：按档位补差，满盘本轮总计500分，不再追加第二次500。 */
    if (count > Game.LineCount)
    {
        uint32_t added = LinePoints[count] - LinePoints[Game.LineCount];
        Game.Score = added <= UINT32_MAX - Game.Score ? Game.Score + added : UINT32_MAX;
        Game.LineCount = count;
    }
    if (Game.GridMask == 0x1FFU)
    {
        Game.FullFlash = 1U;
        Game.FullFlashTick = HAL_GetTick();
    }
    Light_SetGrid(Game.GridMask, Game.LineCount, Game.FullFlash);
    /* 同一次新点亮只选最高奖励提示，转轮命中也复用此入口。 */
    Game_QueueMusic(Game.FullFlash ? GAME_MUSIC_FULL :
        (count > previous_lines ? GAME_MUSIC_LINE : GAME_MUSIC_CHARACTER));
}

void Game_WheelTrigger(void)
{
    if (Game.WheelPending < UINT16_MAX)
        Game.WheelPending++;
}

void Game_ControlButton(uint8_t button, uint8_t event)
{
    if (event != KEY_EVENT_SHORT || !Game_CanRun())
        return;
    if (button == 1U && Button1Pending < UINT16_MAX)
        Button1Pending++;
    else if (button == 2U)
        RedeemPending = 1U;
}

void Game_SetMenu(uint8_t active)
{
    Game.MenuActive = active != 0U;
    Button1Pending = 0U;
    RedeemPending = 0U;
    if (Game.MenuActive)
        Ctrl_PauseOutputs();
    else if (Game_CanRun())
        Ctrl_ResumeOutputs();
}

void Game_SetFault(uint8_t fault)
{
    if ((fault & (uint8_t)~Game.Faults) != 0U)
    {
        /* 新故障清掉普通待播提示，相同锁存故障不循环播报。 */
        MusicPending = 0U;
        Game_QueueMusic(GAME_MUSIC_FAULT);
    }
    Game.Faults |= fault;
    Button1Pending = 0U;
    RedeemPending = 0U;
    Ctrl_PauseOutputs();
}

void Game_Recover(uint8_t keep_pending)
{
    /* 中文注释：先结清已发生的物理反馈，取消任务不退回已出的珠、不重复扣卡分。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    Game_CollectFeedback();
    if (!keep_pending)
    {
        Motor_Hoolle1.Hoolle_num = 0U;
        Card.Card_num = 0U;
        Game.CardUnitPoints = 0U;
        Game.WheelPending = 0U;
    }
    Game.Faults = 0U;
    MusicPending &= (uint8_t)~GAME_MUSIC_BIT(GAME_MUSIC_FAULT);
    Ctrl_ResetFaults();
    __set_PRIMASK(primask);
    /* 后台内只解除故障，不运行电机；退出后台后再恢复保留的未出任务。 */
    if (Game_CanRun())
        Ctrl_ResumeOutputs();
}

void Mesg_Task(void)
{
    uint32_t now = HAL_GetTick();
    Game_CollectFeedback();
    if (Game.FullFlash && (uint32_t)(now - Game.FullFlashTick) >= 3000U)
    {
        Game.FullFlash = 0U;
        Game.GridMask = 0U;
        Game.LineCount = 0U;
        Light_SetGrid(0U, 0U, 0U);
    }
    if (Game_CanRun())
    {
        /* 中文注释：待出数量预留余额，只有PE3确认成功后实际扣珠。 */
        while (Button1Pending != 0U)
        {
            Button1Pending--;
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            /* 反馈中断可能刚发生而尚未结算，也必须计入余额预留，防止多出一珠。 */
            if ((uint32_t)Motor_Hoolle1.Hoolle_num + BallOutputPendingCount < Game.BallCredits && Motor_Hoolle1.Hoolle_num < UINT16_MAX)
                Hoolle_Output(&Motor_Hoolle1, 1U);
            __set_PRIMASK(primask);
        }
        if (RedeemPending)
        {
            RedeemPending = 0U;
            if (Card.Card_num == 0U && Game.CardUnitPoints == 0U && Setting.PointsPerCard != 0U)
            {
                uint32_t cards = Game.Score / Setting.PointsPerCard;
                if (cards > UINT16_MAX)
                    cards = UINT16_MAX;
                if (cards != 0U)
                {
                    Game.CardUnitPoints = Setting.PointsPerCard;
                    Card_Output(&Card, (uint16_t)cards);
                    Game_QueueMusic(GAME_MUSIC_REDEEM);
                }
            }
        }
    }
    Scene = Game.MenuActive ? SettingScene : (Game.BallCredits != 0U && Game.Faults == 0U ? PlayingScene : IdleScene);
    DigitalTube_SetTotals(Game.Score, Setting.PointsPerCard != 0U ? Game.Score / Setting.PointsPerCard : 0U);
    /* 中文注释：控台只显示可出珠余额；定期重刷以覆盖控台晚启动的情形。 */
    if (Game.BallCredits != LastCreditDisplay || (uint32_t)(now - LastCreditDisplayTick) >= 1000U)
    {
        Comm_SendMesg_FillData(&Tx3, Board_to_Ctrl, CTRL_CMD_DIGITAL_TUBE, Game.BallCredits, 0U);
        LastCreditDisplay = Game.BallCredits;
        LastCreditDisplayTick = now;
    }
    Game_MusicTask();
}
