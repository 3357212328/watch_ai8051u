/*
 * watch_plane.c - "Plane" mini-game: sideways-scrolling shooter.
 *
 * Keil C251 (C89) for the AI8051U OLED_UI watch project.
 *
 * The OLED is 128 wide x 64 high (landscape), which does not suit a
 * classic vertical-shooter scroll, so the game is laid out sideways:
 * the player ship sits at the LEFT edge (x fixed, moves UP/DOWN with
 * the Up/Down keys or the encoder) and fires straight RIGHT; enemies
 * enter at the right edge and fly LEFT toward the player. Fire is
 * automatic (never needs a key); holding Enter adds manual burst
 * fire. Score is top left, best score in the middle, lives top right.
 * Back exits on the release edge; losing all lives shows the GAME
 * OVER screen (Enter release or Up press = restart, Back release =
 * exit).
 *
 * GAME FEEL (this revision):
 *   - three enemy behaviours, unlocked by score: straight flyers,
 *     wave riders (triangle-wave climb/dive, phase per ship) and fast
 *     divers that spawn level with the player ship and rush in at
 *     3 px/tick with a speed streak behind them.
 *   - kills leave a three-frame spark burst instead of popping out of
 *     existence; the player's own hit bursts too.
 *   - the spawn cadence tightens with the score (14/11/8/6 ticks).
 *   - high score lives in xdata and is exposed through
 *     Watch_Plane_GetHighScore() / Watch_Plane_SetHighScore() so the
 *     power module can persist it later. No EEPROM access here.
 *
 * House rules honoured:
 *   - C89 only: block-top declarations, slash-slash comments banned.
 *   - All gameplay state lives in xdata; sprite bitmaps and string
 *     literals live in code flash; nothing touches edata/ISR files.
 *   - Uses ONLY the existing display primitives (OLED_Clear /
 *     OLED_DrawLine / OLED_Printf family / OLED_Update) plus a small
 *     private OR-blitter into the public OLED_DisplayBuf, in the
 *     same sprite format as watch_icons.c (column bytes, bit0 = top
 *     row, page-major). No new primitives, no WatchMode access.
 *   - Every input source (USBCtrl_TakeEnter/Back/Up/Down/TakeEncoder
 *     and Encoder_Get) is drained on every tick even when unused.
 *   - Edge trackers are parked against the LIVE key state both here
 *     in Open() and after every tick, so no phantom edges fire.
 */
#include "oled.h"            /* primitives + OLED_DisplayBuf via oled_driver.h */
#include "oled_ui.h"         /* ColorMode, OLED_UI_Brightness settings */
#include "oled_ui_driver.h"  /* active-low key readers, Encoder_Get */
#include "usb_ctrl.h"        /* virtual console one-shot pulses */
#include "watch_plane.h"

/* Prototypes for the persistence hooks (declared here because the
 * shared header is owned by the power-module pass; the definitions
 * below are non-static so that pass can call them). */
unsigned char Watch_Plane_GetHighScore(void);
void Watch_Plane_SetHighScore(unsigned char value);

/* Layout constants */
#define PL_SHIP_X       (6)      /* ship x is fixed at the left */
#define PL_SHIP_W       (8)
#define PL_SHIP_H       (10)
#define PL_Y_MIN        (10)     /* ship y movement range */
#define PL_Y_MAX        (50)
#define PL_ENEMY_X0     (121)    /* enemies enter at the right edge */
#define PL_SCORE_MAX    (255)    /* HUD shows up to 3 digits */
#define PL_BULLET_MAX   (5)      /* simultaneous player bullets */
#define PL_ENEMY_MAX    (4)      /* simultaneous enemies */
#define PL_FX_MAX       (3)      /* simultaneous kill bursts */
#define PL_BULLET_SPD   (4)      /* px per tick */
#define PL_BULLET_LEN   (3)      /* px drawn length */

/* Enemy behaviour ids (stored per slot) */
#define PL_E_STRAIGHT   (0)      /* level flight, 1..2 px per tick */
#define PL_E_WAVE       (1)      /* triangle-wave climb/dive */
#define PL_E_DIVE       (2)      /* spawns level with the ship, 3 px */

/* Score gates for the behaviour mix and the spawn cadence */
#define PL_GATE_WAVE    (15)
#define PL_GATE_DIVE    (40)
#define PL_GATE_FAST    (80)

/* Sprite bitmaps: column bytes, bit0 = top row, page-major format
 * (matches Image_watch in watch_icons.c). OR-blitted, so holes in
 * the glyphs stay transparent and whatever is behind survives. */

/* Player ship 8x10 (nose points right, wings/tail at the left) */
static const uint8_t code PlShipImg[16] = {
    0x78,0x30,0x30,0xB6,0xFF,0xFF,0x78,0x78,
    0x00,0x00,0x00,0x01,0x03,0x03,0x00,0x00
};

/* Enemy invader 6x7 */
static const uint8_t code PlEnemyImg[6] = {
    0x08,0x55,0x3E,0x3E,0x55,0x08
};

/* Gameplay state */
static unsigned char xdata PlShipY;
static unsigned char xdata PlLives;
static unsigned char xdata PlScore;
static unsigned char xdata PlHigh;        /* best score, survives a restart */
static unsigned char xdata PlInvuln;
static unsigned char xdata PlOver;        /* 1 = GAME OVER screen */
static unsigned char xdata PlFireTimer;   /* autofire period */
static unsigned char xdata PlBurstCool;   /* Enter-held burst cooldown */
static unsigned char xdata PlSpawnTimer;
static unsigned char xdata PlRndSeed;
static unsigned long xdata PlTickCnt;     /* parity for blink */

/* Bullet pool: x = left/tail pixel, moves right */
static unsigned char xdata PlBX[PL_BULLET_MAX];
static unsigned char xdata PlBY[PL_BULLET_MAX];
static unsigned char xdata PlBLive[PL_BULLET_MAX];

/* Enemy pool */
static unsigned char xdata PlEX[PL_ENEMY_MAX];
static unsigned char xdata PlEY[PL_ENEMY_MAX];
static unsigned char xdata PlESpd[PL_ENEMY_MAX];  /* 1..3 px per tick */
static unsigned char xdata PlETyp[PL_ENEMY_MAX];  /* PL_E_* */
static unsigned char xdata PlEPh[PL_ENEMY_MAX];   /* wave phase */
static unsigned char xdata PlELive[PL_ENEMY_MAX];

/* Kill-burst pool: centre pixel + remaining life in ticks */
static unsigned char xdata PlFxX[PL_FX_MAX];
static unsigned char xdata PlFxY[PL_FX_MAX];
static unsigned char xdata PlFxT[PL_FX_MAX];

/* Edge trackers (Back exit is a RELEASE edge; on the GAME OVER
 * screen Enter release and Up press decide restart vs exit) */
static unsigned char xdata PlLastEnter;
static unsigned char xdata PlLastBack;
static unsigned char xdata PlLastUp;

/*
 * @brief OR-blit a sprite into the public display buffer.
 *        img layout: page*W + column, each byte bit r = row r of the
 *        page. Handles a sub-page vertical offset with spill into the
 *        next page. Rejects anything that would leave the 128x64
 *        buffer instead of wrapping around it.
 */
static void PlBlit(const uint8_t code *img, unsigned char x, unsigned char y,
                   unsigned char w, unsigned char h)
{
    unsigned char first, last, shift, p, pg, c, i, b;

    if ((unsigned char)(x + w - 1) > 127)
    {
        return;
    }
    if ((unsigned char)(y + h - 1) > 63)
    {
        return;
    }
    first = (unsigned char)(y >> 3);
    last = (unsigned char)((y + h - 1) >> 3);
    shift = (unsigned char)(y & 7);
    i = 0;
    for (p = 0; p <= (unsigned char)(last - first); p++)
    {
        pg = (unsigned char)(first + p);
        for (c = 0; c < w; c++)
        {
            b = img[i];
            i++;
            OLED_DisplayBuf[pg][x + c] |= (unsigned char)(b << shift);
            /* spill only when there is a next page to receive it */
            if (shift != 0 && pg < 7)
            {
                OLED_DisplayBuf[pg + 1][x + c] |= (unsigned char)(b >> (8 - shift));
            }
        }
    }
}

/*
 * @brief LCG pseudo-random, self-implemented (seed*7+5, bijective
 *        forever because 7 is odd), plain-C89 replacement for rand().
 */
static unsigned char Pl_Rnd(void)
{
    PlRndSeed = (unsigned char)((PlRndSeed * 7U) + 5U);
    return PlRndSeed;
}

/*
 * @brief AABB overlap test for two rectangles.
 */
static unsigned char Pl_Hit(unsigned char ax, unsigned char ay,
                            unsigned char aw, unsigned char ah,
                            unsigned char bx, unsigned char by,
                            unsigned char bw, unsigned char bh)
{
    if (ax < (unsigned char)(bx + bw) && bx < (unsigned char)(ax + aw) &&
        ay < (unsigned char)(by + bh) && by < (unsigned char)(ay + ah))
    {
        return 1;
    }
    return 0;
}

/*
 * @brief Start a kill burst at a pixel. Reuses the oldest slot when
 *        the tiny pool is full, so a busy frame never loses the
 *        explosion nearest the action.
 */
static void Pl_Boom(unsigned char x, unsigned char y)
{
    unsigned char i;

    for (i = 0; i < PL_FX_MAX; i++)
    {
        if (PlFxT[i] == 0)
        {
            PlFxX[i] = x;
            PlFxY[i] = y;
            PlFxT[i] = 6;
            return;
        }
    }
    PlFxX[0] = x;
    PlFxY[0] = y;
    PlFxT[0] = 6;
}

/*
 * @brief Fire one bullet from the ship nose if a slot is free.
 */
static void Pl_SpawnBullet(void)
{
    unsigned char i;

    for (i = 0; i < PL_BULLET_MAX; i++)
    {
        if (PlBLive[i] == 0)
        {
            PlBLive[i] = 1;
            PlBX[i] = (unsigned char)(PL_SHIP_X + PL_SHIP_W);
            PlBY[i] = (unsigned char)(PlShipY + (PL_SHIP_H / 2));
            return;
        }
    }
}

/*
 * @brief Spawn one enemy at the right edge. The behaviour mix opens
 *        up with the score: straight flyers first, then wave riders,
 *        then fast divers that enter level with the player ship.
 */
static void Pl_SpawnEnemy(void)
{
    unsigned char i, r, t;

    for (i = 0; i < PL_ENEMY_MAX; i++)
    {
        if (PlELive[i] == 0)
        {
            r = Pl_Rnd();
            t = PL_E_STRAIGHT;
            if (PlScore >= PL_GATE_DIVE && (r & 0x03) == 0)
            {
                t = PL_E_DIVE;
            }
            else if (PlScore >= PL_GATE_WAVE && (r & 0x03) == 1)
            {
                t = PL_E_WAVE;
            }
            PlELive[i] = 1;
            PlEX[i] = PL_ENEMY_X0;
            PlETyp[i] = t;
            PlEPh[i] = (unsigned char)((r >> 2) & 0x0F);
            if (t == PL_E_DIVE)
            {
                PlEY[i] = PlShipY;                 /* straight at the ship */
                PlESpd[i] = 3;
            }
            else
            {
                PlEY[i] = (unsigned char)(11U + (r % 42U));   /* y 11..52 */
                PlESpd[i] = (unsigned char)(1U + (Pl_Rnd() & 1U));
            }
            return;
        }
    }
}

/*
 * @brief Wipe every gameplay static (pool slots, counters, score).
 *        The high score deliberately survives.
 */
static void Pl_NewGame(void)
{
    unsigned char i;

    PlShipY = 30;
    PlLives = 3;
    PlScore = 0;
    PlInvuln = 30;    /* short spawn-protection blink */
    PlOver = 0;
    PlFireTimer = 3;
    PlBurstCool = 0;
    PlSpawnTimer = 14;
    for (i = 0; i < PL_BULLET_MAX; i++)
    {
        PlBLive[i] = 0;
    }
    for (i = 0; i < PL_ENEMY_MAX; i++)
    {
        PlELive[i] = 0;
    }
    for (i = 0; i < PL_FX_MAX; i++)
    {
        PlFxT[i] = 0;
    }
    /* joggle the LCG so each fresh run plays differently */
    Pl_Rnd();
    Pl_Rnd();
}

/*
 * @brief (Re)start the mini-game: reset state and park all edge
 *        trackers against the LIVE key state.
 */
void Watch_Plane_Open(void)
{
    PlLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    PlLastBack = (unsigned char)(Key_GetBackStatus() == 0);
    PlLastUp = (unsigned char)(Key_GetUpStatus() == 0);
    PlTickCnt = 0;
    PlRndSeed = 0x5AU;
    Pl_NewGame();
}

/*
 * @brief One 20 ms tick: drains every virtual source + the encoder,
 *        samples the keys, advances the simulation. Returns 1 when
 *        the user requests exit (Back release edge).
 */
unsigned char Watch_Plane_Tick(void)
{
    unsigned char vEnter, vBack, vUp, vDown;
    unsigned char pEnter, pBack, pUp, pDown;
    unsigned char i, j, r, exitReq, spd, ph;
    short enc;
    int mv, ny;

    /* Drain EVERY virtual source + the encoder, each tick, always */
    vEnter = USBCtrl_TakeEnter();
    vBack = USBCtrl_TakeBack();
    vUp = USBCtrl_TakeUp();
    vDown = USBCtrl_TakeDown();
    enc = Encoder_Get();
    enc = (short)(enc + USBCtrl_TakeEncoder());

    /* Sample the physical keys (active low readers) */
    pEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    pBack = (unsigned char)(Key_GetBackStatus() == 0);
    pUp = (unsigned char)(Key_GetUpStatus() == 0);
    pDown = (unsigned char)(Key_GetDownStatus() == 0);

    /* GAME OVER screen: after a short settle time, Enter release or
     * Up press = play again; Back release = exit immediately (a
     * virtual Back pulse always exits, settle or not). */
    if (PlOver != 0)
    {
        r = 0;
        if ((PlLastBack != 0) && (pBack == 0))
        {
            return 1;
        }
        if (vBack != 0)
        {
            return 1;
        }
        if (PlSpawnTimer != 0)
        {
            PlSpawnTimer--;
            PlLastEnter = pEnter;
            PlLastBack = pBack;
            PlLastUp = pUp;
            PlTickCnt++;
            return 0;
        }
        if ((PlLastEnter != 0) && (pEnter == 0))
        {
            r = 1;
        }
        if ((pUp != 0) && (PlLastUp == 0))
        {
            r = 1;
        }
        PlLastEnter = pEnter;
        PlLastBack = pBack;
        PlLastUp = pUp;
        PlTickCnt++;
        if (r != 0)
        {
            Pl_NewGame();
        }
        return 0;
    }

    /* Player movement: physical HELD keys and virtual pulses both
     * step 2 px per tick; encoder detents nudge 1 px each. */
    mv = 0;
    if (pUp != 0)
    {
        mv -= 2;
    }
    if (vUp != 0)
    {
        mv -= 2;
    }
    if (pDown != 0)
    {
        mv += 2;
    }
    if (vDown != 0)
    {
        mv += 2;
    }
    mv += enc;
    ny = (int)PlShipY + mv;
    if (ny < PL_Y_MIN)
    {
        ny = PL_Y_MIN;
    }
    if (ny > PL_Y_MAX)
    {
        ny = PL_Y_MAX;
    }
    PlShipY = (unsigned char)ny;

    /* Fire is AUTOMATIC every 3 ticks; holding Enter adds optional
     * manual burst fire on a 4-tick cooldown. A virtual Enter pulse
     * is honoured as one press-and-release cycle (one burst chance). */
    if (PlFireTimer == 0)
    {
        PlFireTimer = 3;
        Pl_SpawnBullet();
    }
    else
    {
        PlFireTimer--;
    }
    if (pEnter != 0 || vEnter != 0)
    {
        if (PlBurstCool == 0)
        {
            PlBurstCool = 4;
            Pl_SpawnBullet();
        }
        else
        {
            PlBurstCool--;
        }
    }

    /* Age every kill burst one tick (6 ticks = three 2-tick frames) */
    for (i = 0; i < PL_FX_MAX; i++)
    {
        if (PlFxT[i] != 0)
        {
            PlFxT[i]--;
        }
    }

    /* Bullets fly right 4 px per tick, cull past the right edge. */
    for (i = 0; i < PL_BULLET_MAX; i++)
    {
        if (PlBLive[i] == 0)
        {
            continue;
        }
        r = (unsigned char)(PlBX[i] + PL_BULLET_SPD);
        if (r > (unsigned char)(127U + 1U - PL_BULLET_LEN))
        {
            PlBLive[i] = 0;
        }
        else
        {
            PlBX[i] = r;
        }
    }

    /* Enemies march left toward the player side; cull at the wrap.
     * Wave riders weave up and down on their own phase, divers hold
     * their entry height and simply close in fast. */
    for (i = 0; i < PL_ENEMY_MAX; i++)
    {
        if (PlELive[i] == 0)
        {
            continue;
        }
        spd = PlESpd[i];
        if (PlEX[i] < spd)
        {
            PlELive[i] = 0;
            continue;
        }
        PlEX[i] = (unsigned char)(PlEX[i] - spd);
        if (PlETyp[i] == PL_E_WAVE)
        {
            ph = (unsigned char)((PlEPh[i] + 1U) & 0x0F);
            PlEPh[i] = ph;
            if (ph < 8)
            {
                if (PlEY[i] > 11)
                {
                    PlEY[i]--;
                }
            }
            else
            {
                if (PlEY[i] < 52)
                {
                    PlEY[i]++;
                }
            }
        }
    }

    /* Spawning cadence accelerates with the score: 14/11/8/6 ticks. */
    if (PlSpawnTimer == 0)
    {
        Pl_SpawnEnemy();
        if (PlScore >= PL_GATE_FAST)
        {
            PlSpawnTimer = 6;
        }
        else if (PlScore >= PL_GATE_DIVE)
        {
            PlSpawnTimer = 8;
        }
        else if (PlScore >= PL_GATE_WAVE)
        {
            PlSpawnTimer = 11;
        }
        else
        {
            PlSpawnTimer = 14;
        }
    }
    else
    {
        PlSpawnTimer--;
    }

    /* Bullet vs enemy (2 px inset on the enemy body box). A kill
     * leaves a spark burst where the hull used to be. */
    for (i = 0; i < PL_BULLET_MAX; i++)
    {
        if (PlBLive[i] == 0)
        {
            continue;
        }
        for (j = 0; j < PL_ENEMY_MAX; j++)
        {
            if (PlELive[j] == 0)
            {
                continue;
            }
            if (Pl_Hit(PlBX[i], PlBY[i], 3, 1,
                       (unsigned char)(PlEX[j] + 2), (unsigned char)(PlEY[j] + 2),
                       2, 3) != 0)
            {
                PlBLive[i] = 0;
                PlELive[j] = 0;
                Pl_Boom((unsigned char)(PlEX[j] + 3),
                        (unsigned char)(PlEY[j] + 3));
                if (PlScore < PL_SCORE_MAX)
                {
                    PlScore++;
                }
                if (PlScore > PlHigh)
                {
                    PlHigh = PlScore;
                }
            }
            if (PlBLive[i] == 0)
            {
                break;
            }
        }
    }

    /* Enemy vs player: both are destroyed, one life lost and a
     * brief 20-tick invulnerable blink begins. */
    if (PlInvuln != 0)
    {
        PlInvuln--;
    }
    else
    {
        for (j = 0; j < PL_ENEMY_MAX; j++)
        {
            if (PlELive[j] == 0)
            {
                continue;
            }
            if (Pl_Hit((unsigned char)(PL_SHIP_X + 2), (unsigned char)(PlShipY + 2), 4, 6,
                       (unsigned char)(PlEX[j] + 2), (unsigned char)(PlEY[j] + 2),
                       2, 3) != 0)
            {
                Pl_Boom((unsigned char)(PlEX[j] + 3),
                        (unsigned char)(PlEY[j] + 3));
                PlELive[j] = 0;
                PlInvuln = 20;
                PlLives--;
                if (PlLives == 0)
                {
                    PlOver = 1;
                    PlSpawnTimer = 25;   /* settle time before the
                                            restart edges arm, so the
                                            GAME OVER text is seen
                                            even if keys were held */
                }
                break;
            }
        }
    }

    /* Back exit only on the RELEASE edge (hold Back, then let go);
     * trackers parked LIVE. A virtual Back pulse counts as one full
     * press+release cycle, so console users can exit too. */
    exitReq = 0;
    if ((PlLastBack != 0) && (pBack == 0))
    {
        exitReq = 1;
    }
    if (vBack != 0)
    {
        exitReq = 1;
    }
    PlLastEnter = pEnter;
    PlLastBack = pBack;
    PlLastUp = pUp;
    PlTickCnt++;
    return exitReq;
}

/*
 * @brief Full-screen draw with exactly one OLED_Update at the end.
 */
void Watch_Plane_Render(void)
{
    unsigned char i, t, r;
    int cx, cy;

    /* Match the face/stopwatch pages: honor the menu's color and
     * brightness settings before the frame paint. */
    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();

    /* GAME OVER layout: big title, score line, best line, key hints */
    if (PlOver != 0)
    {
        OLED_Printf(28, 8, OLED_8X16_HALF, (char code *)"GAME OVER");
        OLED_Printf(40, 26, OLED_6X8_HALF, (char code *)"SCORE %d", (int)PlScore);
        OLED_Printf(40, 36, OLED_6X8_HALF, (char code *)"HI %d", (int)PlHigh);
        OLED_Printf(19, 48, OLED_6X8_HALF, (char code *)"E:AGAIN  B:EXIT");
        OLED_Update();
        return;
    }

    /* HUD: score left, best middle, lives right (all 6x8) */
    OLED_Printf(0, 0, OLED_6X8_HALF, (char code *)"SC:%d", (int)PlScore);
    OLED_Printf(52, 0, OLED_6X8_HALF, (char code *)"HI:%d", (int)PlHigh);
    OLED_Printf(110, 0, OLED_6X8_HALF, (char code *)"L:%d", (int)PlLives);

    /* Player ship (skipped on odd ticks during the invulnerable
     * blink after a hit) */
    if (PlInvuln == 0 || (PlTickCnt & 1UL) == 0UL)
    {
        PlBlit(PlShipImg, PL_SHIP_X, PlShipY, PL_SHIP_W, PL_SHIP_H);
    }

    /* Bullets: 3 px horizontal dashes travelling right */
    for (i = 0; i < PL_BULLET_MAX; i++)
    {
        if (PlBLive[i] == 0)
        {
            continue;
        }
        OLED_DrawLine((int16_t)(PlBX[i] + 2), (int16_t)PlBY[i],
                      (int16_t)PlBX[i], (int16_t)PlBY[i]);
    }

    /* Enemies; divers drag a short speed streak behind them */
    for (i = 0; i < PL_ENEMY_MAX; i++)
    {
        if (PlELive[i] == 0)
        {
            continue;
        }
        PlBlit(PlEnemyImg, PlEX[i], PlEY[i], 6, 7);
        if (PlETyp[i] == PL_E_DIVE)
        {
            OLED_DrawLine((int16_t)(PlEX[i] + 7), (int16_t)(PlEY[i] + 3),
                          (int16_t)(PlEX[i] + 10), (int16_t)(PlEY[i] + 3));
        }
    }

    /* Kill bursts: a three-frame X that grows from 3 to 7 px wide */
    for (i = 0; i < PL_FX_MAX; i++)
    {
        if (PlFxT[i] == 0)
        {
            continue;
        }
        t = PlFxT[i];
        if (t >= 5)
        {
            r = 1;
        }
        else if (t >= 3)
        {
            r = 2;
        }
        else
        {
            r = 3;
        }
        cx = (int)PlFxX[i];
        cy = (int)PlFxY[i];
        OLED_DrawLine((int16_t)(cx - (int)r), (int16_t)(cy - (int)r),
                      (int16_t)(cx + (int)r), (int16_t)(cy + (int)r));
        OLED_DrawLine((int16_t)(cx - (int)r), (int16_t)(cy + (int)r),
                      (int16_t)(cx + (int)r), (int16_t)(cy - (int)r));
    }

    OLED_Update();
}

/* ---------------------------------------------------------------- */
/* High-score persistence hooks (no EEPROM access in this module).  */
/* ---------------------------------------------------------------- */
unsigned char Watch_Plane_GetHighScore(void)
{
    return PlHigh;
}

void Watch_Plane_SetHighScore(unsigned char value)
{
    PlHigh = value;
}
