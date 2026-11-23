/* watch_dino.c - Chrome-style dino runner mini-game for the AI8051U
 * OLED watch (Keil C251, strict C89). 128x64 monochrome, 20 ms world.
 *
 * Integration contract (handled by the app later, NOT here):
 *   Watch_Dino_Open()    (re)start  -> resets state, parks edge trackers
 *   Watch_Dino_Tick()    every 20ms -> returns 1 when user asks to leave
 *   Watch_Dino_Render()  every main-loop pass -> full-screen paint
 * This module NEVER changes the app WatchMode and never includes
 * watch_face.h.
 *
 * INPUT CONTRACT, applied to ALL SIX inputs every tick (see Tick):
 *   Every take/sink is drained unconditionally each 20 ms tick:
 *   USBCtrl_TakeEnter / TakeBack / TakeUp / TakeDown / TakeEncoder and
 *   Encoder_Get. Identical-key pulses sent within one 20 ms window
 *   collapse (the take functions are one-shot latches), and draining
 *   them all stops virtual pulses from leaking into any other mode.
 *   Physical keys are sampled live, active low (0 = pressed), with
 *   per-tick edge trackers like watch_face. Jump fires on Up/Enter
 *   PRESS edge; the EXIT request fires only on the Back RELEASE edge.
 *   Virtual users: 'w'/'e' pulses jump, 'b' pulse exits. Down and the
 *   encoder are drained and intentionally ignored (duck not built,
 *   code budget).
 */
#include "AI8051U.H"
#include "DEF.H"
#include "watch_dino.h"
#include "oled.h"
#include "oled_driver.h"
#include "oled_ui.h"
#include "oled_ui_driver.h"
#include "usb_ctrl.h"

/* ---------------------------------------------------------------- */
/* Game constants                                                   */
/* ---------------------------------------------------------------- */
#define DINO_STATE_RUN         0   /* playing                                 */
#define DINO_STATE_OVER        1   /* crash happened, card shown              */

#define DINO_TOP_GROUND        42  /* dino sprite top row while standing      */
#define DINO_TOP_GROUND8       336 /* same in 1/8-px fixed point (42 * 8)     */
#define DINO_JUMP_V            24  /* jump impulse, 1/8 px per tick (3 px)    */
#define DINO_G_BASE            2   /* gravity, 1/8 px per tick^2 (0.25 px)    */
#define DINO_G_HELD            1   /* reduced gravity while hop is held       */
#define DINO_X                 4   /* dino sprite left edge                   */
#define DINO_W                 10  /* dino sprite width  (matches bitmap)     */
#define DINO_H                 14  /* dino sprite height (matches bitmap)     */
#define DINO_HIT_X0            5   /* dino AABB, one pixel shaved all round   */
#define DINO_HIT_X1            13
#define DINO_HIT_DY0           1
#define DINO_HIT_DY1           12

#define DINO_MAX_OBS           4   /* obstacle slots                          */
#define DINO_FIRST_GAP         55  /* clear runway (px) before first obstacle */
#define DINO_GAP_MIN           30U /* spec gap range: 30..70, min grows w/spd */
#define DINO_GAP_MAX           70U
#define DINO_CROSS_SPEEDUP     40  /* every 40 full 128 px crossings: +speed  */
#define DINO_PTERO_AFTER       6   /* pterodactyls only after 6 crossings     */
#define DINO_LINE_Y            56  /* ground line row                         */
#define DINO_GND_WRAP          128 /* one full screen = one world crossing    */

/* ---------------------------------------------------------------- */
/* Sprite bitmaps: page-column format, bit0 = top row of each 8-row */
/* page, identical to Image_watch in watch_icons.c. Drawn with the  */
/* existing generic primitive OLED_ShowImage (arbitrary W/H, page   */
/* 8 px rows, y offset shifting + clipping) - no new primitives.    */
/* ---------------------------------------------------------------- */

/* 10x14 dino, facing right (tail left, head right) */
static const uint8_t code DinoImgDino[DINO_W * 2] = {
    /* page 0 (rows 0..7): body, neck, head with eye gap */
    0xE0,0x80,0x00,0xC0,0xDF,0xFB,0xFF,0xFF,0x9E,0x00,
    /* page 1 (rows 8..13): belly, split legs, feet */
    0x00,0x01,0x02,0x27,0x3F,0x0F,0x33,0x21,0x00,0x00
};

/* 3x10 tall cactus */
static const uint8_t code DinoObCactusTall[6] = {
    0x44,0xFF,0x4C,
    0x00,0x03,0x00
};

/* 4x6 short cactus */
static const uint8_t code DinoObCactusShort[4] = {
    0x04,0x3F,0x3F,0x04
};

/* 6x6 pterodactyl (flight rows only; hitbox 6 px high, jumpable) */
static const uint8_t code DinoObPtero[6] = {
    0x04,0x06,0x0F,0x1F,0x1E,0x0C
};

/* Per-type obstacle geometry: y = sprite top (feet on ground line)  */
static const uint8_t code DinoObW[3] = {3, 4, 6};
static const uint8_t code DinoObH[3] = {10, 6, 6};
static const uint8_t code DinoObY[3] = {46, 50, 42};
static const uint8_t code * code DinoObImg[3] = {
    DinoObCactusTall, DinoObCactusShort, DinoObPtero
};

/* ---------------------------------------------------------------- */
/* Game state: everything lives in xdata per storage policy.        */
/* No ISR hooks, no edata, no dynamic allocation in this module.    */
/* ---------------------------------------------------------------- */
static unsigned char xdata DinoState;      /* DINO_STATE_RUN / _OVER      */
static unsigned char xdata DinoOverTimer;  /* GAME OVER settle 20ms ticks */
static int          xdata DinoY8;          /* dino top, 1/8 px units      */
static int          xdata DinoVy8;         /* velocity, 1/8 px per tick   */
static unsigned char xdata DinoOnGround;   /* 1 = resting on ground       */
static unsigned char xdata DinoJumpHold;   /* physical key still held     */
static unsigned char xdata DinoUsbHold;    /* virtual jump = full hop     */

/* Per-tick edge trackers for the physical keys; parked with live pin
 * samples in Open()/Init so a key held across the transition cannot
 * fire a phantom edge (same semantics as watch_face.c).             */
static unsigned char xdata DinoLastEnter;  /* 1 = key was held last tick  */
static unsigned char xdata DinoLastBack;
static unsigned char xdata DinoLastUp;
static unsigned char xdata DinoLastDown;

static unsigned int  xdata DinoScore;      /* +1 per 4 ticks, caps at 9999 */
static unsigned char xdata DinoTickCnt;    /* score subdiv counter         */

static unsigned char xdata DinoScroll;     /* world scroll, 1..4 px/tick   */
static unsigned char xdata DinoCrossCnt;   /* crossings since speed change */
static unsigned int  xdata DinoCrossTotal; /* full-screen crossings total  */
static unsigned char xdata DinoGnd;        /* px accumulator, wraps at 128,
                                              low 3 bits scroll the ground */
static unsigned char xdata DinoSpawnDist;  /* px scrolled since last spawn */
static unsigned char xdata DinoNextGap;    /* px to next obstacle spawn    */

static unsigned int  xdata DinoLcg;        /* 16-bit LCG: full 65536 cycle */

static int           xdata DinoObX[DINO_MAX_OBS];
static unsigned char xdata DinoObT[DINO_MAX_OBS];
static unsigned char xdata DinoObUsed[DINO_MAX_OBS];

/* ---------------------------------------------------------------- */
/* 16-bit LCG "x = x*7 + 5 style" but full period: a = 25173
 * (a == 1 mod 4), c = 13849 (odd) -> 65536 cycle mod 2^16. Only the
 * high byte feeds the game (low LCG bits alternate too quickly).   */
/* ---------------------------------------------------------------- */
static unsigned char Dino_Rand(void)
{
    DinoLcg = (unsigned int)(DinoLcg * 25173U + 13849U);
    return (unsigned char)(DinoLcg >> 8);
}

/*
 * @brief Gameplay reset: every gameplay-relevant static goes back to
 *        its entry value here, so both Open() and the game-over
 *        "AGAIN" path restart clean. Trackers are NOT touched here
 *        (they must stay live across a restart); Open() parks them.
 */
static void Dino_Reset(void)
{
    unsigned char i;

    DinoState    = DINO_STATE_RUN;
    DinoY8       = DINO_TOP_GROUND8;
    DinoVy8      = 0;
    DinoOnGround = 1;
    DinoJumpHold = 0;
    DinoUsbHold  = 0;

    DinoScore    = 0;
    DinoTickCnt  = 0;

    DinoScroll   = 1;
    DinoCrossCnt = 0;
    DinoCrossTotal = 0;
    DinoGnd      = 0;
    DinoSpawnDist = 0;
    DinoNextGap  = DINO_FIRST_GAP;

    for (i = 0; i < DINO_MAX_OBS; i++)
    {
        DinoObX[i]    = 0;
        DinoObT[i]    = 0;
        DinoObUsed[i] = 0;
    }
}

/*
 * @brief AABB test of the dino hitbox against one obstacle slot.
 *        Dino hitbox shaves 1 px all round for forgiving crashes.
 */
static unsigned char Dino_CheckHit(unsigned char idx)
{
    unsigned char t;
    int ox;
    int dyTop;
    int obX1, obY0, obY1;

    ox    = DinoObX[idx];
    t     = DinoObT[idx];
    dyTop = (int)(DinoY8 >> 3);

    obX1 = ox + (int)DinoObW[t] - 1;
    obY0 = (int)DinoObY[t];
    obY1 = obY0 + (int)DinoObH[t] - 1;

    if (obX1 < DINO_HIT_X0)                        { return 0; }
    if (ox  > (int)DINO_HIT_X1)                    { return 0; }
    if (obY1 < dyTop + (int)DINO_HIT_DY0)          { return 0; }
    if (obY0 > dyTop + (int)DINO_HIT_DY1)          { return 0; }
    return 1;
}

/*
 * @brief (Re)start the mini-game: reset state and park all key edge
 *        trackers with the LIVE pin samples so keys held while the
 *        game opens cannot fire phantom press/release edges.
 */
void Watch_Dino_Open(void)
{
    DinoLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    DinoLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    DinoLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    DinoLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
    Dino_Reset();
}

/*
 * @brief 20 ms world tick: drain ALL virtual inputs, sample keys,
 *        run physics/obstacles/score, handle restart and exit.
 *        Returns 1 when the user asks to leave the mini-game.
 */
unsigned char Watch_Dino_Tick(void)
{
    unsigned char exitReq;
    unsigned char sEnter, sBack, sUp, sDown;   /* 1 = held this tick   */
    unsigned char vEnter, vBack, vUp, vDown;   /* one-shot USB pulses  */
    short vEnc;                                /* USBCtrl_TakeEncoder  */
    short gEnc;                                /* Encoder_Get          */
    unsigned char entPress, entRel;
    unsigned char bckRel, upPress;
    unsigned char i, t;
    unsigned char jumpNow;

    /* --- 1) Drain every virtual input first (see file header).
     * Identical pulses inside one 20 ms window collapse here.
     * Down and both encoder sinks are drained and ignored. --- */
    vEnter = USBCtrl_TakeEnter();
    vBack  = USBCtrl_TakeBack();
    vUp    = USBCtrl_TakeUp();
    vDown  = USBCtrl_TakeDown();   /* drained, deliberately unused (no duck) */
    vEnc   = USBCtrl_TakeEncoder();/* drained, deliberately unused           */
    gEnc   = Encoder_Get();        /* drained, deliberately unused           */

    /* --- 2) Live pin samples --- */
    sEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    sBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    sUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    sDown  = (unsigned char)(Key_GetDownStatus()  == 0);

    /* --- 3) Edges relative to last tick's samples --- */
    entPress = (unsigned char)(sEnter != 0 && DinoLastEnter == 0);
    entRel   = (unsigned char)(sEnter == 0 && DinoLastEnter != 0);
    bckRel   = (unsigned char)(sBack  == 0 && DinoLastBack  != 0);
    upPress  = (unsigned char)(sUp    != 0 && DinoLastUp    == 0);

    exitReq = (unsigned char)((vBack != 0) || bckRel);

    if (DinoState == DINO_STATE_RUN)
    {
        /* --- jump: Up/Enter press edges and 'w'/'e' pulses. While
         * airborne further input is ignored until ground contact. --- */
        jumpNow = (unsigned char)(upPress || (vUp != 0) ||
                                  entPress || (vEnter != 0));
        if (jumpNow && DinoOnGround)
        {
            DinoVy8      = -DINO_JUMP_V;
            DinoOnGround = 0;
            DinoJumpHold = (unsigned char)(sUp || sEnter);
            DinoUsbHold  = (unsigned char)((vUp != 0) || (vEnter != 0));
        }

        /* --- physics in 1/8-px fixed point. Holding Up/Enter lowers
         * the rising gravity (long hop); tap = short hop; falling
         * always uses the base gravity. --- */
        if (DinoOnGround == 0)
        {
            unsigned char g;

            if (DinoJumpHold != 0 && sUp == 0 && sEnter == 0)
            {
                DinoJumpHold = 0;
            }
            g = DINO_G_BASE;
            if (DinoVy8 < 0 && (DinoJumpHold != 0 || DinoUsbHold != 0))
            {
                g = DINO_G_HELD;
            }
            DinoVy8 += (int)g;
            DinoY8  += DinoVy8;
            if (DinoY8 >= DINO_TOP_GROUND8)
            {
                DinoY8       = DINO_TOP_GROUND8;
                DinoVy8      = 0;
                DinoOnGround = 1;
                DinoJumpHold = 0;
                DinoUsbHold  = 0;
            }
        }

        /* --- score: +1 per 4 ticks --- */
        DinoTickCnt++;
        if ((DinoTickCnt & 0x03) == 0 && DinoScore < 9999U)
        {
            DinoScore++;
        }

        /* --- world scroll + speed-up every 40 full crossings --- */
        DinoGnd = (unsigned char)(DinoGnd + DinoScroll);
        if (DinoGnd >= DINO_GND_WRAP)
        {
            DinoGnd = (unsigned char)(DinoGnd - DINO_GND_WRAP);
            DinoCrossCnt++;
            DinoCrossTotal++;
            if (DinoCrossCnt >= DINO_CROSS_SPEEDUP)
            {
                DinoCrossCnt = 0;
                if (DinoScroll < 4)
                {
                    DinoScroll++;
                }
            }
        }

        /* --- obstacles: move left, purge off-screen --- */
        for (i = 0; i < DINO_MAX_OBS; i++)
        {
            if (DinoObUsed[i] == 0)
            {
                continue;
            }
            DinoObX[i] -= (int)DinoScroll;
            if (DinoObX[i] < -8)
            {
                DinoObUsed[i] = 0;
            }
        }

        /* --- spawn: random gap 30+4*spd..70 px (min grows with
         * speed so every jump stays possible), type 25 % ptero after
         * 6 crossings, else tall/short cactus. --- */
        DinoSpawnDist = (unsigned char)(DinoSpawnDist + DinoScroll);
        if (DinoSpawnDist >= DinoNextGap)
        {
            unsigned char gap, got;

            t    = Dino_Rand();
            gap  = (unsigned char)(DINO_GAP_MIN + (unsigned int)((t >> 2) & 0x1F)
                                   + ((unsigned int)DinoScroll << 2));
            if (gap > (unsigned char)DINO_GAP_MAX)
            {
                gap = (unsigned char)DINO_GAP_MAX;
            }
            DinoNextGap = gap;

            t = Dino_Rand();
            if (DinoCrossTotal >= DINO_PTERO_AFTER && ((t >> 2) & 0x03) == 0)
            {
                t = 2;   /* pterodactyl */
            }
            else
            {
                t = Dino_Rand();
                t = (unsigned char)((((unsigned int)t >> 2) & 0x01) != 0 ? 1 : 0);
            }
            got = 0;
            for (i = 0; i < DINO_MAX_OBS; i++)
            {
                if (DinoObUsed[i] == 0)
                {
                    DinoObUsed[i] = 1;
                    DinoObX[i]    = DINO_GND_WRAP;   /* just off right edge */
                    DinoObT[i]    = t;
                    got = 1;
                    break;
                }
            }
            /* Only consume the spawn distance on success: with the pool
             * full the distance is kept so the next tick retries. */
            if (got != 0) { DinoSpawnDist = 0; }
        }

        /* --- collision: instant game over, restart edges stay silent
         * for a 25-tick settle window (same pattern as PlSpawnTimer =
         * 25 in watch_plane.c, so keys held in the crash frame are
         * filtered out before the restart path arms). --- */
        for (i = 0; i < DINO_MAX_OBS; i++)
        {
            if (DinoObUsed[i] != 0 && Dino_CheckHit(i))
            {
                DinoState = DINO_STATE_OVER;
                DinoOverTimer = 25;
                break;
            }
        }
    }
    else
    {
        /* Game over card: Enter pulse/release or Up press restarts,
        but only once the settle timer is drained; virtual pulses are
        drained every tick anyway and are ignored while settling. */
        if (DinoOverTimer != 0)
        {
            DinoOverTimer--;
        }
        else if (vEnter != 0 || vUp != 0 || entRel || upPress)
        {
            Dino_Reset();
        }
    }

    /* --- trackers follow the live samples every tick (both states) --- */
    DinoLastEnter = sEnter;
    DinoLastBack  = sBack;
    DinoLastUp    = sUp;
    DinoLastDown  = sDown;

    return exitReq;
}

/*
 * @brief Full-screen render, one pass per frame, ending with exactly
 *        one full-screen OLED_Update (the framework's periodic update
 *        is not guaranteed visible in this mode).
 */
void Watch_Dino_Render(void)
{
    unsigned char i, t, dy;
    int x;

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();

    if (DinoState != DINO_STATE_RUN)
    {
        /* Game over: big card, final score, restart/exit hints */
        OLED_ShowString(28, 18, (char code *)"GAME OVER", OLED_8X16_HALF);
        OLED_ShowString(37, 44, (char code *)"SCORE", OLED_6X8_HALF);
        OLED_ShowNum(70, 44, DinoScore, 4, OLED_6X8_HALF);
        OLED_ShowString(19, 56, (char code *)"E:AGAIN  B:EXIT", OLED_6X8_HALF);
        OLED_Update();
        return;
    }

    /* Score, top right: "SC:" + 4 zero-padded digits ends at x=128 */
    OLED_ShowString(86, 0, (char code *)"SC:", OLED_6X8_HALF);
    OLED_ShowNum(104, 0, DinoScore, 4, OLED_6X8_HALF);

    /* Ground line with the notch under the standing dino */
    OLED_DrawRectangle(0, DINO_LINE_Y, 128, 1, 1);
    OLED_ClearArea(2, DINO_LINE_Y, 12, 1);

    /* Scrolling ground texture: the static prime-stride pattern
     * shifted by the world phase (DinoGnd & 7; wraps stay seamless
     * because 128 == 0 mod 8). Intentionally x-random-ish. */
    for (i = 0; i < 10; i++)
    {
        x = (int)(i * 13 + 5) - (int)(DinoGnd & 0x07);
        OLED_DrawPoint((int16_t)(x & 0x007F), 54);
    }
    for (i = 0; i < 8; i++)
    {
        x = (int)(i * 17 + 9) - (int)(DinoGnd & 0x07);
        OLED_DrawPoint((int16_t)(x & 0x007F), 59);
    }

    /* Obstacles, right to left */
    for (i = 0; i < DINO_MAX_OBS; i++)
    {
        if (DinoObUsed[i] == 0)
        {
            continue;
        }
        t = DinoObT[i];
        OLED_ShowImage((int16_t)DinoObX[i], (int16_t)DinoObY[t],
                       (uint16_t)DinoObW[t], (uint16_t)DinoObH[t],
                       DinoObImg[t]);
    }

    /* Dino, feet on the ground line when not jumping */
    dy = (unsigned char)(DinoY8 >> 3);
    OLED_ShowImage(DINO_X, (int16_t)dy, (uint16_t)DINO_W,
                   (uint16_t)DINO_H, DinoImgDino);

    OLED_Update();
}
