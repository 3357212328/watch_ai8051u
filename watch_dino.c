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
 * High score hooks (for the power/settings pass that persists them;
 * this file never touches EEPROM):
 *   unsigned int Watch_Dino_GetHighScore(void);
 *   void         Watch_Dino_SetHighScore(unsigned int value);
 *
 * INPUT CONTRACT, applied to ALL SIX inputs every tick (see Tick):
 *   Every take/sink is drained unconditionally each 20 ms tick:
 *   USBCtrl_TakeEnter / TakeBack / TakeUp / TakeDown / TakeEncoder and
 *   Encoder_Get. Identical-key pulses sent within one 20 ms window
 *   collapse (the take functions are one-shot latches), and draining
 *   them all stops virtual pulses from leaking into any other mode.
 *   Physical keys are sampled live, active low (0 = pressed), with
 *   per-tick edge trackers like watch_face. Jump fires on the Up/Enter
 *   PRESS edge; the EXIT request fires only on the Back RELEASE edge.
 *   Down is a HOLD input: held on the ground it crouches the dino under
 *   the high pterodactyl. Virtual users: 'w'/'e' pulses jump (full
 *   hop), 'b' pulse exits, a Down pulse ducks for one tick.
 *
 * GAME FEEL (this revision):
 *   - variable jump: the impulse is fixed, but while Up/Enter stays
 *     held for up to DINO_HOLD_TICKS ticks after takeoff gravity is
 *     reduced. Tap = short hop, hold = high jump, never a double jump
 *     (the impulse only arms while the feet are on the ground).
 *   - speed steps on score (1..4 px/tick) and the spawn gap widens in
 *     pixels as speed rises, so the time between obstacles shrinks.
 *   - obstacle set: tall cactus, short cactus, low ptero (jump) and
 *     high ptero (duck or a held jump). Pteros are score-gated and
 *     flap through a two-frame cycle.
 *   - two-frame run cycle, a crouch sprite and a distinct crash pose.
 *   - ground dashes scroll at world speed (phase = DinoGnd mod 32).
 *   - score top right, high score top left, night palette inversion
 *     every 512 points (one OLED_Reverse call, no extra state).
 */
#include "AI8051U.H"
#include "DEF.H"
#include "watch_dino.h"
#include "oled.h"
#include "oled_driver.h"
#include "oled_ui.h"
#include "oled_ui_driver.h"
#include "usb_ctrl.h"

/* Prototypes for the persistence hooks (declared here because the
 * shared header is owned by the power-module pass; the definitions
 * below are non-static so that pass can call them). */
unsigned int Watch_Dino_GetHighScore(void);
void Watch_Dino_SetHighScore(unsigned int value);

/* ---------------------------------------------------------------- */
/* Game constants                                                   */
/* ---------------------------------------------------------------- */
#define DINO_STATE_RUN         0   /* playing                                 */
#define DINO_STATE_OVER        1   /* crash happened, card shown              */

#define DINO_TOP_GROUND        42  /* dino sprite top row while standing      */
#define DINO_TOP_GROUND8       336 /* same in 1/8-px fixed point (42 * 8)     */
#define DINO_JUMP_V            26  /* jump impulse, 1/8 px per tick (3.25 px) */
#define DINO_G_BASE            3   /* gravity, 1/8 px per tick^2              */
#define DINO_G_LIGHT           1   /* gravity while the hop is still held     */
#define DINO_HOLD_TICKS        7   /* ticks of light gravity after takeoff    */
#define DINO_X                 4   /* dino sprite left edge                   */
#define DINO_W                 10  /* dino sprite width  (matches bitmap)     */
#define DINO_H                 14  /* dino sprite height (matches bitmap)     */
#define DINO_DUCK_TOP          48  /* crouched sprite top row                 */
#define DINO_DUCK_H            8   /* crouched sprite height                  */
#define DINO_HIT_X0            5   /* dino AABB, one pixel shaved all round   */
#define DINO_HIT_X1            13
#define DINO_HIT_DY0           1
#define DINO_HIT_DY1           12
#define DINO_DUCK_DY1          6   /* crouched AABB bottom offset             */

#define DINO_MAX_OBS           4   /* obstacle slots                          */
#define DINO_FIRST_GAP         55  /* clear runway (px) before first obstacle */
#define DINO_GAP_MIN           30U /* spec gap range: 30..70, min grows w/spd */
#define DINO_GAP_MAX           70U
#define DINO_SPEED_CAP         4   /* max world scroll, px per tick           */
#define DINO_PTERO_LO          120U/* score gate: low ptero (jump it)         */
#define DINO_PTERO_HI          400U/* score gate: high ptero (duck/hold jump) */
#define DINO_LINE_Y            56  /* ground line row                         */
#define DINO_GND_WRAP          128 /* one full screen = one world crossing    */
#define DINO_SCORE_MAX         9999U
#define DINO_NIGHT_SHIFT       9   /* invert the palette every 512 points     */

/* ---------------------------------------------------------------- */
/* Sprite bitmaps: page-column format, bit0 = top row of each 8-row */
/* page, identical to Image_watch in watch_icons.c. Drawn with the  */
/* existing generic primitive OLED_ShowImage (arbitrary W/H, page   */
/* 8 px rows, y offset shifting + clipping) - no new primitives.    */
/* ---------------------------------------------------------------- */

/* 10x14 dino, run frame 1 (front leg planted, facing right) */
static const uint8_t code DinoImgRun1[DINO_W * 2] = {
    /* page 0 (rows 0..7): body, neck, head with eye gap */
    0xE0,0x80,0x00,0xC0,0xDF,0xFB,0xFF,0xFF,0x9E,0x00,
    /* page 1 (rows 8..13): belly, front leg down, back leg tucked */
    0x00,0x01,0x02,0x07,0x3F,0x0F,0x03,0x01,0x00,0x00
};

/* 10x14 dino, run frame 2 (legs swapped) */
static const uint8_t code DinoImgRun2[DINO_W * 2] = {
    0xE0,0x80,0x00,0xC0,0xDF,0xFB,0xFF,0xFF,0x9E,0x00,
    0x00,0x01,0x02,0x03,0x0F,0x07,0x3F,0x01,0x00,0x00
};

/* 10x14 crash pose: eye filled in, legs splayed flat */
static const uint8_t code DinoImgCrash[DINO_W * 2] = {
    0xE0,0x80,0x00,0xC0,0xDF,0xFF,0xFF,0xFF,0x9E,0x00,
    0x00,0x01,0x02,0x3F,0x0F,0x07,0x0F,0x3F,0x00,0x00
};

/* 10x8 crouched dino (top row 48, head low and forward) */
static const uint8_t code DinoImgDuck[DINO_W] = {
    0x30,0x38,0xB8,0xF8,0x3C,0x3E,0xFB,0x9F,0x1E,0x00
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

/* 6x6 pterodactyl, wings up / wings down (two-frame flap) */
static const uint8_t code DinoObPteroA[6] = {
    0x04,0x06,0x0F,0x1F,0x1E,0x0C
};
static const uint8_t code DinoObPteroB[6] = {
    0x10,0x18,0x1E,0x1F,0x0F,0x06
};

/* Per-type obstacle geometry: y = sprite top. Types 0/1 are the
 * cacti, 2 = low ptero (jump it), 3 = high ptero (duck or hold-jump). */
static const uint8_t code DinoObW[4] = {3, 4, 6, 6};
static const uint8_t code DinoObH[4] = {10, 6, 6, 6};
static const uint8_t code DinoObY[4] = {46, 50, 46, 38};
static const uint8_t code * code DinoObImg[2] = {
    DinoObCactusTall, DinoObCactusShort
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
static unsigned char xdata DinoHold;       /* light-gravity ticks left    */
static unsigned char xdata DinoUsbHold;    /* virtual jump = full hop     */
static unsigned char xdata DinoDuck;       /* 1 = crouched on the ground  */

/* Per-tick edge trackers for the physical keys; parked with live pin
 * samples in Open()/Init so a key held across the transition cannot
 * fire a phantom edge (same semantics as watch_face.c).             */
static unsigned char xdata DinoLastEnter;  /* 1 = key was held last tick  */
static unsigned char xdata DinoLastBack;
static unsigned char xdata DinoLastUp;
static unsigned char xdata DinoLastDown;

static unsigned int  xdata DinoScore;      /* +1 per 4 ticks, caps at 9999 */
static unsigned int  xdata DinoHigh;       /* best score, NOT reset here   */
static unsigned char xdata DinoTickCnt;    /* score subdiv + anim clock    */

static unsigned char xdata DinoScroll;     /* world scroll, 1..4 px/tick   */
static unsigned char xdata DinoGnd;        /* px accumulator, wraps at 128,
                                              low 5 bits scroll the ground */
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
 *        "AGAIN" path restart clean. Trackers and the high score are
 *        NOT touched here (trackers stay live across a restart;
 *        Open() parks them, the high score outlives the run).
 */
static void Dino_Reset(void)
{
    unsigned char i;

    DinoState    = DINO_STATE_RUN;
    DinoY8       = DINO_TOP_GROUND8;
    DinoVy8      = 0;
    DinoOnGround = 1;
    DinoHold     = 0;
    DinoUsbHold  = 0;
    DinoDuck     = 0;

    DinoScore    = 0;
    DinoTickCnt  = 0;

    DinoScroll   = 1;
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
 *        Dino hitbox shaves 1 px all round for forgiving crashes; the
 *        crouched box ends DINO_DUCK_DY1 instead of DINO_HIT_DY1.
 */
static unsigned char Dino_CheckHit(unsigned char idx)
{
    unsigned char t;
    int ox;
    int dyTop;
    int dyBot;
    int obX1, obY0, obY1;

    ox    = DinoObX[idx];
    t     = DinoObT[idx];
    dyTop = (int)(DinoY8 >> 3);
    dyBot = dyTop + (int)((DinoDuck != 0) ? DINO_DUCK_DY1 : DINO_HIT_DY1);

    obX1 = ox + (int)DinoObW[t] - 1;
    obY0 = (int)DinoObY[t];
    obY1 = obY0 + (int)DinoObH[t] - 1;

    if (obX1 < DINO_HIT_X0)                        { return 0; }
    if (ox  > (int)DINO_HIT_X1)                    { return 0; }
    if (obY1 < dyTop + (int)DINO_HIT_DY0)          { return 0; }
    if (obY0 > dyBot)                              { return 0; }
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
    /* xdata is not guaranteed cleared by the startup code: keep a
     * garbage high score from leaking onto the HUD before the power
     * module has restored the stored value. */
    if (DinoHigh > DINO_SCORE_MAX)
    {
        DinoHigh = 0;
    }
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
    unsigned char i, t, g;
    unsigned char jumpNow, duckNow;

    /* --- 1) Drain every virtual input first (see file header).
     * Identical pulses inside one 20 ms window collapse here. --- */
    vEnter = USBCtrl_TakeEnter();
    vBack  = USBCtrl_TakeBack();
    vUp    = USBCtrl_TakeUp();
    vDown  = USBCtrl_TakeDown();
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
        duckNow = (unsigned char)((sDown != 0) || (vDown != 0));

        /* --- jump: Up/Enter press edges and 'w'/'e' pulses. The
         * impulse arms only with both feet on the ground, so holding
         * the key can never double jump. --- */
        jumpNow = (unsigned char)(upPress || (vUp != 0) ||
                                  entPress || (vEnter != 0));
        if (jumpNow && DinoOnGround)
        {
            DinoVy8      = -DINO_JUMP_V;
            DinoOnGround = 0;
            DinoDuck     = 0;
            DinoHold     = DINO_HOLD_TICKS;
            DinoUsbHold  = (unsigned char)((vUp != 0) || (vEnter != 0));
        }

        /* --- crouch only exists on the ground; it cancels on takeoff --- */
        if (DinoOnGround != 0)
        {
            DinoDuck = duckNow;
        }

        /* --- physics in 1/8-px fixed point. The fixed impulse plus a
         * bounded light-gravity window makes the arc predictable:
         * tap = short hop, hold = high jump. Release ends the window
         * immediately, and falling always uses the base gravity. --- */
        if (DinoOnGround == 0)
        {
            g = DINO_G_BASE;
            if (DinoHold != 0)
            {
                if (sUp != 0 || sEnter != 0 || DinoUsbHold != 0)
                {
                    g = DINO_G_LIGHT;
                    DinoHold--;
                }
                else
                {
                    DinoHold = 0;
                }
            }
            DinoVy8 += (int)g;
            DinoY8  += DinoVy8;
            if (DinoY8 >= DINO_TOP_GROUND8)
            {
                DinoY8       = DINO_TOP_GROUND8;
                DinoVy8      = 0;
                DinoOnGround = 1;
                DinoHold     = 0;
                DinoUsbHold  = 0;
                DinoDuck     = duckNow;   /* stay crouched if still held */
            }
        }

        /* --- score: +1 per 4 ticks (also the run-animation clock) --- */
        DinoTickCnt++;
        if ((DinoTickCnt & 0x03) == 0 && DinoScore < DINO_SCORE_MAX)
        {
            DinoScore++;
        }

        /* --- speed steps on score: 1 px/tick up to the cap --- */
        DinoScroll = (unsigned char)(1U + (DinoScore >> 8));
        if (DinoScroll > DINO_SPEED_CAP)
        {
            DinoScroll = DINO_SPEED_CAP;
        }

        /* --- world scroll (the low 5 bits phase the ground dashes) --- */
        DinoGnd = (unsigned char)(DinoGnd + DinoScroll);
        if (DinoGnd >= DINO_GND_WRAP)
        {
            DinoGnd = (unsigned char)(DinoGnd - DINO_GND_WRAP);
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

        /* --- spawn: gap 30+rand(0..31)+4*spd px, capped, so the time
         * between obstacles shrinks as the world speeds up. Type mix
         * opens up with the score: pteros first appear at 120, the
         * head-height one at 400. --- */
        DinoSpawnDist = (unsigned char)(DinoSpawnDist + DinoScroll);
        if (DinoSpawnDist >= DinoNextGap)
        {
            unsigned char gap, got;

            t   = Dino_Rand();
            gap = (unsigned char)(DINO_GAP_MIN +
                                  (unsigned int)((t >> 3) & 0x1F) +
                                  ((unsigned int)DinoScroll << 2));
            if (gap > (unsigned char)DINO_GAP_MAX)
            {
                gap = (unsigned char)DINO_GAP_MAX;
            }
            DinoNextGap = gap;

            t = Dino_Rand();
            if (DinoScore >= DINO_PTERO_HI && (t & 0x07) == 0)
            {
                t = 3;   /* high ptero: duck or a held jump */
            }
            else if (DinoScore >= DINO_PTERO_LO && (t & 0x03) == 0)
            {
                t = 2;   /* low ptero: jump it */
            }
            else
            {
                t = (unsigned char)((t & 0x10) != 0 ? 1 : 0);
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
                if (DinoScore > DinoHigh)
                {
                    DinoHigh = DinoScore;
                }
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
    unsigned char i, t, dy, fr;
    int x;
    const uint8_t code *img;

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();

    if (DinoState != DINO_STATE_RUN)
    {
        /* Game over: crash pose, final score, best, restart/exit hints */
        OLED_ShowString(28, 8, (char code *)"GAME OVER", OLED_8X16_HALF);
        OLED_ShowString(34, 26, (char code *)"SCORE", OLED_6X8_HALF);
        OLED_ShowNum(70, 26, DinoScore, 4, OLED_6X8_HALF);
        OLED_ShowString(34, 38, (char code *)"HI", OLED_6X8_HALF);
        OLED_ShowNum(52, 38, DinoHigh, 4, OLED_6X8_HALF);
        OLED_ShowString(19, 50, (char code *)"E:AGAIN  B:EXIT", OLED_6X8_HALF);
        OLED_ShowImage(DINO_X, (int16_t)(DinoY8 >> 3), (uint16_t)DINO_W,
                       (uint16_t)DINO_H, DinoImgCrash);
        OLED_Update();
        return;
    }

    /* High score top left, score top right (both 4 zero-padded digits) */
    OLED_ShowString(0, 0, (char code *)"HI:", OLED_6X8_HALF);
    OLED_ShowNum(18, 0, DinoHigh, 4, OLED_6X8_HALF);
    OLED_ShowString(86, 0, (char code *)"SC:", OLED_6X8_HALF);
    OLED_ShowNum(104, 0, DinoScore, 4, OLED_6X8_HALF);

    /* Ground line */
    OLED_DrawRectangle(0, DINO_LINE_Y, 128, 1, 1);

    /* Scrolling ground texture: dashes on a 32 px period, phased by
     * the world accumulator (128 is a multiple of 32, so the wrap is
     * seamless). Two staggered rows read as gravel rushing past. */
    for (i = 0; i < 5; i++)
    {
        x = (int)(i * 32 + 4) - (int)(DinoGnd & 0x1F);
        OLED_DrawLine((int16_t)x, 59, (int16_t)(x + 3), 59);
    }
    for (i = 0; i < 4; i++)
    {
        x = (int)(i * 32 + 22) - (int)(DinoGnd & 0x1F);
        OLED_DrawLine((int16_t)x, 61, (int16_t)(x + 2), 61);
    }

    /* Obstacles, right to left. Pteros flap on the score clock. */
    fr = (unsigned char)((DinoTickCnt >> 2) & 0x01);
    for (i = 0; i < DINO_MAX_OBS; i++)
    {
        if (DinoObUsed[i] == 0)
        {
            continue;
        }
        t = DinoObT[i];
        if (t < 2)
        {
            img = DinoObImg[t];
        }
        else
        {
            img = (fr != 0) ? DinoObPteroB : DinoObPteroA;
        }
        OLED_ShowImage((int16_t)DinoObX[i], (int16_t)DinoObY[t],
                       (uint16_t)DinoObW[t], (uint16_t)DinoObH[t], img);
    }

    /* Dino: crash pose is handled above, crouch while Down is held,
     * otherwise the two-frame run cycle (frame 1 while airborne). */
    if (DinoDuck != 0)
    {
        OLED_ShowImage(DINO_X, (int16_t)DINO_DUCK_TOP, (uint16_t)DINO_W,
                       (uint16_t)DINO_DUCK_H, DinoImgDuck);
    }
    else
    {
        dy = (unsigned char)(DinoY8 >> 3);
        if (DinoOnGround == 0)
        {
            fr = 0;
        }
        OLED_ShowImage(DINO_X, (int16_t)dy, (uint16_t)DINO_W,
                       (uint16_t)DINO_H,
                       (fr != 0) ? DinoImgRun2 : DinoImgRun1);
    }

    /* Night palette flip on score milestones: one call, no state. */
    if (((DinoScore >> DINO_NIGHT_SHIFT) & 0x01U) != 0)
    {
        OLED_Reverse();
    }

    OLED_Update();
}

/* ---------------------------------------------------------------- */
/* High-score persistence hooks (no EEPROM access in this module).  */
/* ---------------------------------------------------------------- */
unsigned int Watch_Dino_GetHighScore(void)
{
    return DinoHigh;
}

void Watch_Dino_SetHighScore(unsigned int value)
{
    DinoHigh = (value > DINO_SCORE_MAX) ? DINO_SCORE_MAX : value;
}
