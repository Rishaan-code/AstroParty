// Lab9Main.c
// Runs on MSPM0G3507
// Astro Party - Version 1
// Rishaan Malik and Abhinav Chodisetti
// ECE319K Lab 9
//
// Hardware:
//   PA24  Player 1 Rotate (clockwise)   active-low, pull-up
//   PA25  Player 1 Shoot                active-low, pull-up
//   PA26  Player 2 Rotate (clockwise)   active-low, pull-up
//   PA27  Player 2 Shoot                active-low, pull-up
//   PB18  Slide pot (ADC1 ch5) volume
//   PB0-4 5-bit DAC (sound output)
//   ST7735 HiLetGo 128x160 LCD (INITR_BLACKTAB)
//
// Game:
//   Two ships auto-move in the direction they face (8 directions, 45 deg steps).
//   Rotate button turns ship 45 deg clockwise each press.
//   Shoot button fires a bullet in the direction the ship faces.
//   Bullet hitting the other ship ends the round.
//   First to 3 rounds wins the match.

#include <stdio.h>
#include <stdint.h>
#include <ti/devices/msp/msp.h>
#include "../inc/ST7735.h"
#include "../inc/Clock.h"
#include "../inc/LaunchPad.h"
#include "../inc/TExaS.h"
#include "../inc/Timer.h"
#include "../inc/ADC1.h"
#include "../inc/DAC5.h"
#include "Switch.h"
#include "Sound.h"

// PLL 
void PLL_Init(void){
  Clock_Init80MHz(0);
}

// Screen dimensions 
#define SCRW  128
#define SCRH  160


#define COL_BLACK   0x0000
#define COL_WHITE   0xFFFF
#define COL_GRAY    0x8410
#define COL_YELLOW  0xFFE0
// True cyan (light blue): R=0 G=63 B=31 in 565 = 0x07FF
#define COL_CYAN    0x07FF
// True orange: R=31 G=40 B=0 in 565 = 0xFD00
#define COL_ORANGE  0xFD00

//Direction system
// 8 directions, 0-7, clockwise starting East double check tho
//   0=E  1=SE  2=S  3=SW  4=W  5=NW  6=N  7=NE
// DirDX/DirDY: ship movement per frame (pixels)
// TipDX/TipDY: offset from center to nose tip

#define SHIP_R  6    // ship bounding radius in pixels i think

#define NUM_DIRS 16

static const int8_t DirDX[16] = {  2,  2,  1,  1,  0, -1, -1, -2, -2, -2, -1, -1,  0,  1,  1,  2 };
static const int8_t DirDY[16] = {  0,  1,  1,  2,  2,  2,  1,  1,  0, -1, -1, -2, -2, -2, -1, -1 };
static const int8_t TipDX[16] = {  4,  3,  3,  2,  0, -2, -3, -3, -4, -3, -3, -2,  0,  2,  3,  3 };
static const int8_t TipDY[16] = {  0,  2,  2,  3,  4,  3,  2,  2,  0, -2, -2, -3, -4, -3, -2, -2 };

// 0 = English, 1 = Chinese
uint8_t Language = 0;

// Current round number (1, 2, 3...)
uint8_t RoundNumber = 0;

#define BSPEED  3    // bullet speed multiplier
#define TOP_Y   11   // top of play area (below score bar)

// Structs 
typedef struct {
  int16_t  x,  y;    // current position
  int16_t  px, py;   // previous position (for erase)
  uint8_t  dir;      // 0-7
  uint8_t  alive;
  uint16_t color;
} Ship_t;

typedef struct {
  int16_t  x,  y;
  int16_t  px, py;
  int8_t   dx, dy;   // velocity (pixels/frame)
  uint8_t  alive;
  uint8_t  owner;    // 1 or 2
} Bullet_t;

#define MAX_BULLETS 4

// Powerup types
#define PWR_NONE    0
#define PWR_SHIELD  1
#define PWR_FAST    2
#define PWR_SLOW    3

// Asteroid state
typedef struct {
  int16_t  x, y;       // position
  int16_t  px, py;     // previous position
  int8_t   dx, dy;     // velocity
  uint8_t  alive;
} Asteroid_t;

// Powerup state
typedef struct {
  int16_t  x, y;
  uint8_t  type;       // PWR_SHIELD, PWR_FAST, PWR_SLOW
  uint8_t  alive;
} Powerup_t;

// Player powerup status
typedef struct {
  uint8_t  type;       // active powerup
  uint8_t  shield_hits; // hits remaining if shielded
} PlayerStatus_t;


// Global state
// S1/S2 are reserved register names in the MSPM0 headers, so we use Ship1/Ship2
volatile Ship_t   Ship1, Ship2;
volatile Bullet_t Bullets[MAX_BULLETS];
volatile uint8_t  RoundWins1 = 0;
volatile uint8_t  RoundWins2 = 0;
volatile uint8_t  Semaphore  = 0;
volatile uint8_t  RoundOver  = 0;
volatile uint32_t LastButtons = 0;

volatile Asteroid_t  Asteroid;
volatile Powerup_t   Pup;
volatile PlayerStatus_t Stat1, Stat2;
volatile uint32_t    FrameCount = 0;   // counts 30Hz ticks
#define ASTEROID_R  5                  // asteroid radius
#define ASTEROID_SPAWN_FRAMES  1800    // 60 seconds * 30Hz

// ── TExaS hook ─────────────────────────────────────────────────────────────
uint8_t TExaS_LaunchPadLogicPB27PB26(void){
  return (0x80 | ((GPIOB->DOUT31_0 >> 26) & 0x03));
}

// ── Draw primitives ────────────────────────────────────────────────────────

// Filled circle — local variable names avoid any macro conflicts
static void DrawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t col){
  int16_t ddx, ddy, ppx, ppy;
  for(ddy = -r; ddy <= r; ddy++){
    for(ddx = -r; ddx <= r; ddx++){
      if(ddx*ddx + ddy*ddy <= r*r){
        ppx = cx + ddx;
        ppy = cy + ddy;
        if(ppx >= 0 && ppx < SCRW && ppy >= 0 && ppy < SCRH)
          ST7735_DrawPixel(ppx, ppy, col);
      }
    }
  }
}

typedef struct {
  int16_t x, y, w, h;
} Wall_t;

// Map 0: Two vertical pillars
static const Wall_t Map0[] = {
  { 30,  40, 10, 50 },   // left pillar
  { 88,  70, 10, 50 },   // right pillar
};
#define MAP0_COUNT 2

// Map 1: Center cross
static const Wall_t Map1[] = {
  { 54,  30, 20, 10 },   // top block
  { 54, 120, 20, 10 },   // bottom block
  { 20,  75, 30, 10 },   // left block
  { 78,  75, 30, 10 },   // right block
};
#define MAP1_COUNT 4


// Map 2: Diagonal corners
// Map 2: Diagonal corners (moved away from spawn points)
static const Wall_t Map2[] = {
  { 15,  35, 25, 12 },   // top-left    (moved down from y=20 to y=35)
  { 88,  20, 25, 12 },   // top-right
  { 15, 128, 25, 12 },   // bottom-left
  { 88, 112, 25, 12 },   // bottom-right (moved up from y=128 to y=112)
  { 52,  75, 24, 12 },   // center
};
#define MAP2_COUNT 5

static void ShowLanguageSelect(void){
  ST7735_FillScreen(COL_BLACK);
  ST7735_SetCursor(1, 2);
  ST7735_OutString("Select Language:");
  ST7735_SetCursor(0, 5);
  ST7735_OutString("P1 Shoot = English");
  ST7735_SetCursor(0, 7);
  ST7735_OutString("P2 Shoot = Chinese");
  ST7735_SetCursor(0, 10);
  ST7735_OutString("   Ying Yu");   // English in Chinese
  ST7735_SetCursor(0, 11);
  ST7735_OutString("   Zhong Wen"); // Chinese in pinyin
  // Wait for either P1 shoot (bit1) or P2 shoot (bit3)
  while(1){
    uint32_t sw = Switch_In();
    if(sw & 0x02){ Language = 0; break; }  // P1 shoot = English
    if(sw & 0x08){ Language = 1; break; }  // P2 shoot = Chinese
  }
  Clock_Delay1ms(300);
}

static void DrawMap(uint8_t mapNum){
  const Wall_t *map;
  int count, i;
  if(mapNum % 3 == 0)     { map = Map0; count = MAP0_COUNT; }
  else if(mapNum % 3 == 1){ map = Map1; count = MAP1_COUNT; }
  else                     { map = Map2; count = MAP2_COUNT; }
  for(i = 0; i < count; i++){
    ST7735_FillRect(map[i].x, map[i].y, map[i].w, map[i].h, COL_GRAY);
  }
} 
static int BulletHitsWall(int16_t bx, int16_t by, uint8_t mapNum){
  const Wall_t *map;
  int count, i;
  if(mapNum % 3 == 0)     { map = Map0; count = MAP0_COUNT; }
  else if(mapNum % 3 == 1){ map = Map1; count = MAP1_COUNT; }
  else                     { map = Map2; count = MAP2_COUNT; }
  for(i = 0; i < count; i++){
    if(bx >= map[i].x && bx <= map[i].x + map[i].w &&
       by >= map[i].y && by <= map[i].y + map[i].h){
      return 1;
    }
  }
  return 0;
}

static int ShipHitsWall(int16_t x, int16_t y, uint8_t mapNum){
  const Wall_t *map;
  int count, i;
  if(mapNum % 3 == 0)     { map = Map0; count = MAP0_COUNT; }
  else if(mapNum % 3 == 1){ map = Map1; count = MAP1_COUNT; }
  else                     { map = Map2; count = MAP2_COUNT; }
  for(i = 0; i < count; i++){
    // Check if ship circle overlaps wall rectangle
    // Use bounding box check with ship radius
    if(x + SHIP_R >= map[i].x            &&
       x - SHIP_R <= map[i].x + map[i].w &&
       y + SHIP_R >= map[i].y            &&
       y - SHIP_R <= map[i].y + map[i].h){
      return 1;
    }
  }
  return 0;
}

static void DrawShip(int16_t cx, int16_t cy, uint8_t dir, uint16_t col){
  uint8_t backDir  = (dir + 4) & 7;
  uint8_t leftDir  = (dir + 2) & 7;
  uint8_t rightDir = (dir + 6) & 7;

  // Nose: 2 pixels ahead of center
  int16_t tx = cx + TipDX[dir];
  int16_t ty = cy + TipDY[dir];

  // Wings: perpendicular, same distance as radius
  int16_t lx = cx + TipDX[leftDir];
  int16_t ly = cy + TipDY[leftDir];
  int16_t rx = cx + TipDX[rightDir];
  int16_t ry = cy + TipDY[rightDir];

  // Tail
  int16_t bx = cx + TipDX[backDir];
  int16_t by = cy + TipDY[backDir];

  // Draw filled body as small circle (radius 2) — keeps it solid
  DrawCircle(cx, cy, 2, col);

  // Draw nose pixel cluster — makes it look pointy
  if(tx>=0&&tx<SCRW&&ty>=0&&ty<SCRH) ST7735_DrawPixel(tx,   ty,   col);
  if(tx>=0&&tx<SCRW&&ty>=0&&ty<SCRH) ST7735_DrawPixel(tx,   ty,   col);
  // One step toward nose from center
  int16_t mx = (cx + tx) / 2;
  int16_t my = (cy + ty) / 2;
  if(mx>=0&&mx<SCRW&&my>=0&&my<SCRH) ST7735_DrawPixel(mx, my, col);

  // Draw wing pixels
  if(lx>=0&&lx<SCRW&&ly>=0&&ly<SCRH) ST7735_DrawPixel(lx, ly, col);
  if(rx>=0&&rx<SCRW&&ry>=0&&ry<SCRH) ST7735_DrawPixel(rx, ry, col);

  // Connect wings to nose with midpoints
  int16_t lmx = (tx+lx)/2, lmy = (ty+ly)/2;
  int16_t rmx = (tx+rx)/2, rmy = (ty+ry)/2;
  if(lmx>=0&&lmx<SCRW&&lmy>=0&&lmy<SCRH) ST7735_DrawPixel(lmx, lmy, col);
  if(rmx>=0&&rmx<SCRW&&rmy>=0&&rmy<SCRH) ST7735_DrawPixel(rmx, rmy, col);

  // Tail pixels
  if(bx>=0&&bx<SCRW&&by>=0&&by<SCRH) ST7735_DrawPixel(bx, by, col);

  // White cockpit dot at nose
  if(tx>=0&&tx<SCRW&&ty>=0&&ty<SCRH) ST7735_DrawPixel(tx, ty, COL_WHITE);

  // Yellow engine at tail
  if(bx>=0&&bx<SCRW&&by>=0&&by<SCRH) ST7735_DrawPixel(bx, by, COL_YELLOW);
}

static void EraseShip(int16_t cx, int16_t cy){
  ST7735_FillRect(cx - SHIP_R - 2, cy - SHIP_R - 2,
                  SHIP_R*2 + 4, SHIP_R*2 + 4, COL_BLACK);
}
// 2x2 bullet
static void DrawBullet(int16_t x, int16_t y, uint16_t col){
  if(x < 0 || x+1 >= SCRW || y < 0 || y+1 >= SCRH) return;
  ST7735_DrawPixel(x,   y,   col);
  ST7735_DrawPixel(x+1, y,   col);
  ST7735_DrawPixel(x,   y+1, col);
  ST7735_DrawPixel(x+1, y+1, col);
}

static void DrawAsteroid(int16_t cx, int16_t cy){
  // Orange circle base
  DrawCircle(cx, cy, ASTEROID_R, COL_ORANGE);
  // Jagged edge: draw black notches around the perimeter
  ST7735_DrawPixel(cx + ASTEROID_R, cy,             COL_BLACK);
  ST7735_DrawPixel(cx - ASTEROID_R, cy,             COL_BLACK);
  ST7735_DrawPixel(cx,              cy + ASTEROID_R, COL_BLACK);
  ST7735_DrawPixel(cx,              cy - ASTEROID_R, COL_BLACK);
  ST7735_DrawPixel(cx + 3, cy + 3,  COL_BLACK);
  ST7735_DrawPixel(cx - 3, cy + 3,  COL_BLACK);
  ST7735_DrawPixel(cx + 3, cy - 3,  COL_BLACK);
  ST7735_DrawPixel(cx - 3, cy - 3,  COL_BLACK);
  // Inner detail
  ST7735_DrawPixel(cx + 2, cy,      COL_YELLOW);
  ST7735_DrawPixel(cx - 2, cy,      COL_YELLOW);
}

static void EraseAsteroid(int16_t cx, int16_t cy){
  DrawCircle(cx, cy, ASTEROID_R + 1, COL_BLACK);
}

static void DrawPowerup(int16_t x, int16_t y){
  // Mystery box: white ? mark inside a small colored square
  ST7735_FillRect(x-3, y-3, 7, 7, COL_WHITE);
  ST7735_DrawPixel(x,   y-1, COL_BLACK);
  ST7735_DrawPixel(x,   y,   COL_BLACK);
  ST7735_DrawPixel(x,   y+1, COL_BLACK);
}

static void ErasePowerup(int16_t x, int16_t y){
  ST7735_FillRect(x-4, y-4, 9, 9, COL_BLACK);
}



static void SpawnAsteroid(void){
  // Spawn at random position away from both ships
  // Use FrameCount as a simple random seed
  uint32_t rx = ((FrameCount * 1664525 + 1013904223) >> 16) % (SCRW - 20) + 10;
  uint32_t ry = ((FrameCount * 22695477 + 1) >> 16) % (SCRH - TOP_Y - 20) + TOP_Y + 10;
  Asteroid.x  = (int16_t)rx;
  Asteroid.y  = (int16_t)ry;
  Asteroid.px = Asteroid.x;
  Asteroid.py = Asteroid.y;
  // Random velocity: 1 or 2 in each axis, random sign
  Asteroid.dx = ((FrameCount >> 3) & 1) ? 1 : -1;
  Asteroid.dy = ((FrameCount >> 5) & 1) ? 1 : -1;
  Asteroid.alive = 1;
  Pup.alive = 0;
}

static void SpawnPowerup(int16_t x, int16_t y){
  Pup.x = x;
  Pup.y = y;
  // Random powerup type 1-3
  Pup.type  = (uint8_t)((FrameCount % 3) + 1);
  Pup.alive = 1;
  Sound_Fastinvader1();
}

static void ApplyPowerup(uint8_t player, uint8_t type){
  Sound_Highpitch();
  if(player == 1){
    Stat1.type = type;
    if(type == PWR_SHIELD) Stat1.shield_hits = 2;
  } else {
    Stat2.type = type;
    if(type == PWR_SHIELD) Stat2.shield_hits = 2;
  }
}

static uint8_t ShipTouchingPowerup(volatile Ship_t *s){
  if(!Pup.alive) return 0;
  int16_t ddx = s->x - Pup.x;
  int16_t ddy = s->y - Pup.y;
  return (ddx*ddx + ddy*ddy) <= (SHIP_R+3)*(SHIP_R+3);
}

// Gray border + score separator
static void DrawBorder(void){
  ST7735_DrawFastHLine(0,      0,      SCRW, COL_GRAY);
  ST7735_DrawFastHLine(0,      SCRH-1, SCRW, COL_GRAY);
  ST7735_DrawFastVLine(0,      0,      SCRH, COL_GRAY);
  ST7735_DrawFastVLine(SCRW-1, 0,      SCRH, COL_GRAY);
  ST7735_DrawFastHLine(0,      TOP_Y-1, SCRW, COL_GRAY);
}

// Score bar at top
static void DrawScoreBar(void){
  ST7735_FillRect(1, 1, SCRW-2, TOP_Y-2, COL_BLACK);
  ST7735_SetCursor(0, 0);
  ST7735_OutString("P1:");
  ST7735_OutUDec(RoundWins1);
  ST7735_SetCursor(8, 0);
  ST7735_OutString("P2:");
  ST7735_OutUDec(RoundWins2);
}

// Full redraw
static void DrawAll(void){
  ST7735_FillScreen(COL_BLACK);
  DrawBorder();
  DrawMap(RoundNumber);
  DrawScoreBar();
  if(Ship1.alive) DrawShip(Ship1.x, Ship1.y, Ship1.dir, Ship1.color);
  if(Ship2.alive) DrawShip(Ship2.x, Ship2.y, Ship2.dir, Ship2.color);
  if(Asteroid.alive) DrawAsteroid(Asteroid.x, Asteroid.y);
  if(Pup.alive) DrawPowerup(Pup.x, Pup.y);
}

// Helpers bruh

static int FreeBulletSlot(void){
  int i;
  for(i = 0; i < MAX_BULLETS; i++)
    if(!Bullets[i].alive) return i;
  return -1;
}

static int HitTest(int16_t bx, int16_t by, int16_t sx, int16_t sy){
  int16_t ddx = bx - sx, ddy = by - sy;
  return (ddx*ddx + ddy*ddy) <= (SHIP_R+1)*(SHIP_R+1);
}

#define FLIP(d)  (((d) + 8) & 15)

static void BounceShip(volatile Ship_t *s){
  if(s->x - SHIP_R < 1){
    s->x   = 1 + SHIP_R;
    s->dir = FLIP(s->dir);
  }
  if(s->x + SHIP_R > SCRW-2){
    s->x   = SCRW-2 - SHIP_R;
    s->dir = FLIP(s->dir);
  }
  if(s->y - SHIP_R < TOP_Y){
    s->y   = TOP_Y + SHIP_R;
    s->dir = FLIP(s->dir);
  }
  if(s->y + SHIP_R > SCRH-2){
    s->y   = SCRH-2 - SHIP_R;
    s->dir = FLIP(s->dir);
  }
}

// Round init 
static void RoundInit(void){
  RoundNumber++;
  int i;
  RoundOver = 0;
  FrameCount = 0;
  for(i = 0; i < MAX_BULLETS; i++) Bullets[i].alive = 0;

  // Reset player stats
  Stat1.type = PWR_NONE; Stat1.shield_hits = 0;
  Stat2.type = PWR_NONE; Stat2.shield_hits = 0;

  // Spawn first asteroid
  Asteroid.alive = 0;
  Pup.alive = 0;

  // Ship spawns same as before
  Ship1.x=24;      Ship1.y=TOP_Y+16;
  Ship1.px=Ship1.x; Ship1.py=Ship1.y;
  Ship1.dir=1; Ship1.alive=1; Ship1.color=COL_CYAN;

  Ship2.x=SCRW-24; Ship2.y=SCRH-16;
  Ship2.px=Ship2.x; Ship2.py=Ship2.y;
  Ship2.dir=5; Ship2.alive=1; Ship2.color=COL_ORANGE;

  // Spawn asteroid after a short delay (triggered in ISR)
  SpawnAsteroid();
}

// 30 Hz Game Engine ISR 
void TIMG12_IRQHandler(void){
  int i;
  if((TIMG12->CPU_INT.IIDX) == 1){
    GPIOB->DOUTTGL31_0 = (1UL << 27);
    GPIOB->DOUTTGL31_0 = (1UL << 27);

    if(!RoundOver){

      // Move Ship1
      if(Ship1.alive){
        Ship1.px = Ship1.x;
        Ship1.py = Ship1.y;
        int16_t nx, ny;
        // Speed powerup affects how many pixels to move
        uint8_t steps = (Stat1.type == PWR_FAST) ? 2 :
                        (Stat1.type == PWR_SLOW) ? 0 : 1;
        // For slow: only move every other frame
        if(Stat1.type == PWR_SLOW){
          steps = (FrameCount % 2 == 0) ? 1 : 0;
        }
        nx = Ship1.x + DirDX[Ship1.dir] * steps;
        ny = Ship1.y + DirDY[Ship1.dir] * steps;
        if(!ShipHitsWall(nx, ny, RoundNumber)){
          Ship1.x = nx; Ship1.y = ny;
        }
        BounceShip(&Ship1);
      }

      // Move Ship2
      if(Ship2.alive){
        Ship2.px = Ship2.x;
        Ship2.py = Ship2.y;
        int16_t nx, ny;
        uint8_t steps;
        if(Stat2.type == PWR_SLOW){
          steps = (FrameCount % 2 == 0) ? 1 : 0;
        } else {
          steps = (Stat2.type == PWR_FAST) ? 2 : 1;
        }
        nx = Ship2.x + DirDX[Ship2.dir] * steps;
        ny = Ship2.y + DirDY[Ship2.dir] * steps;
        if(!ShipHitsWall(nx, ny, RoundNumber)){
          Ship2.x = nx;
          Ship2.y = ny;
        }
        BounceShip(&Ship2);
      }
      // Move bullets
      for(i = 0; i < MAX_BULLETS; i++){
        if(!Bullets[i].alive) continue;
        Bullets[i].px = Bullets[i].x;
        Bullets[i].py = Bullets[i].y;
        Bullets[i].x += Bullets[i].dx;
        Bullets[i].y += Bullets[i].dy;

        // Out of bounds: kill bullet
        if(Bullets[i].x < 1     || Bullets[i].x >= SCRW-1 ||
           Bullets[i].y < TOP_Y || Bullets[i].y >= SCRH-1){
          Bullets[i].alive = 0;
          continue;
        }
        // Hit a wall: kill bullet
        if(BulletHitsWall(Bullets[i].x, Bullets[i].y, RoundNumber)){
          Bullets[i].alive = 0;
          continue;
        }

        // P1 bullet vs Ship2
        if(Bullets[i].owner == 1 && Ship2.alive && HitTest(Bullets[i].x, Bullets[i].y, Ship2.x, Ship2.y)){
          Bullets[i].alive = 0;
          if(Stat2.type == PWR_SHIELD && Stat2.shield_hits > 1){
            Stat2.shield_hits--;  // absorb hit
            Sound_Killed();
          } else {
            Ship2.alive = 0;
            Stat2.type = PWR_NONE;
            RoundWins1++;
            RoundOver = 1;
            Sound_Explosion();
          }
        }
        // P2 bullet vs Ship1
        else if(Bullets[i].owner == 2 && Ship1.alive && HitTest(Bullets[i].x, Bullets[i].y, Ship1.x, Ship1.y)){
          Bullets[i].alive = 0;
          if(Stat1.type == PWR_SHIELD && Stat1.shield_hits > 1){
            Stat1.shield_hits--;
            Sound_Killed();
          } else {
            Ship1.alive = 0;
            Stat1.type = PWR_NONE;
            RoundWins2++;
            RoundOver = 1;
            Sound_Explosion();
          }
        }
      
      }
      // Asteroid movement 
      FrameCount++;

      // Spawn new asteroid every minute if current one is gone btw
      if(!Asteroid.alive && !Pup.alive &&
        FrameCount % ASTEROID_SPAWN_FRAMES == 0){
        SpawnAsteroid();
      }

      if(Asteroid.alive){
        Asteroid.px = Asteroid.x;
        Asteroid.py = Asteroid.y;
        Asteroid.x += Asteroid.dx;
        Asteroid.y += Asteroid.dy;
        // Bounce off walls
        if(Asteroid.x - ASTEROID_R < 1 || Asteroid.x + ASTEROID_R > SCRW-2)
          Asteroid.dx = -Asteroid.dx;
        if(Asteroid.y - ASTEROID_R < TOP_Y || Asteroid.y + ASTEROID_R > SCRH-2)
          Asteroid.dy = -Asteroid.dy;
        // Check bullet hits
        for(i = 0; i < MAX_BULLETS; i++){
          if(!Bullets[i].alive) continue;
          int16_t ddx = Bullets[i].x - Asteroid.x;
          int16_t ddy = Bullets[i].y - Asteroid.y;
          if(ddx*ddx + ddy*ddy <= (ASTEROID_R+1)*(ASTEROID_R+1)){
            Bullets[i].alive = 0;
            Asteroid.alive = 0;
            SpawnPowerup(Asteroid.x, Asteroid.y);
            break;
          }
        }
      }

      // Powerup pickup 
      if(Pup.alive){
        if(ShipTouchingPowerup(&Ship1)){
          Pup.alive = 0;
          ApplyPowerup(1, Pup.type);
        } else if(ShipTouchingPowerup(&Ship2)){
          Pup.alive = 0;
          ApplyPowerup(2, Pup.type);
        }
      }

    }

    Semaphore = 1;
    GPIOB->DOUTTGL31_0 = (1UL << 27);
  }
}

static void HandleInput(void){
  uint32_t now     = Switch_In();
  uint32_t pressed = now & ~LastButtons;  // edges only (for shooting)
  LastButtons      = now;
  int slot;
  static uint8_t RotateTimer1 = 0;
  static uint8_t RotateTimer2 = 0;

  
// Player 1 rotate: immediate on first press, then repeat while held...
  if(now & 0x01){
    if(RotateTimer1 == 0){
      Ship1.dir = (Ship1.dir + 1) & 15;  // rotate immediately
    }
    RotateTimer1++;
    if(RotateTimer1 >= 4){
      RotateTimer1 = 1;  // reset to 1 not 0, so it keeps repeating every 4 frames
      Ship1.dir = (Ship1.dir + 1) & 15;
    }
  } else {
    RotateTimer1 = 0;
  }

  // Player 1 shoot (edge only)
  if((pressed & 0x02) && Ship1.alive){
    slot = FreeBulletSlot();
    if(slot >= 0){
      Bullets[slot].x     = Ship1.x + TipDX[Ship1.dir];
      Bullets[slot].y     = Ship1.y + TipDY[Ship1.dir];
      Bullets[slot].px    = Bullets[slot].x;
      Bullets[slot].py    = Bullets[slot].y;
      Bullets[slot].dx    = (int8_t)(DirDX[Ship1.dir] * BSPEED);
      Bullets[slot].dy    = (int8_t)(DirDY[Ship1.dir] * BSPEED);
      Bullets[slot].alive = 1;
      Bullets[slot].owner = 1;
      Sound_Shoot();
    }
  }

  // Player 2 rotate: same logic
  if(now & 0x04){
    if(RotateTimer2 == 0){
      Ship2.dir = (Ship2.dir + 1) & 15;
    }
    RotateTimer2++;
    if(RotateTimer2 >= 4){
      RotateTimer2 = 1;
      Ship2.dir = (Ship2.dir + 1) & 15;
    }
  } else {
    RotateTimer2 = 0;
  }

  // Player 2 shoot (edge only)
  if((pressed & 0x08) && Ship2.alive){
    slot = FreeBulletSlot();
    if(slot >= 0){
      Bullets[slot].x     = Ship2.x + TipDX[Ship2.dir];
      Bullets[slot].y     = Ship2.y + TipDY[Ship2.dir];
      Bullets[slot].px    = Bullets[slot].x;
      Bullets[slot].py    = Bullets[slot].y;
      Bullets[slot].dx    = (int8_t)(DirDX[Ship2.dir] * BSPEED);
      Bullets[slot].dy    = (int8_t)(DirDY[Ship2.dir] * BSPEED);
      Bullets[slot].alive = 1;
      Bullets[slot].owner = 2;
      Sound_Shoot();
    }
  }
}

static void UpdateLCD(void){
  int i;

  // Ships: erase at old position, draw at new position
  if(Ship1.alive){
    if(Ship1.px != Ship1.x || Ship1.py != Ship1.y){
      EraseShip(Ship1.px, Ship1.py);
      DrawMap(RoundNumber);   // restore any walls erased
    }
    DrawShip(Ship1.x, Ship1.y, Ship1.dir, Ship1.color);
  }
  if(Ship2.alive){
    if(Ship2.px != Ship2.x || Ship2.py != Ship2.y){
      EraseShip(Ship2.px, Ship2.py);
      DrawMap(RoundNumber);   // restore any walls erased
    }
    DrawShip(Ship2.x, Ship2.y, Ship2.dir, Ship2.color);
  }

  // Bullets: always erase old spot, draw new spot if still alive
  for(i = 0; i < MAX_BULLETS; i++){
    DrawBullet(Bullets[i].px, Bullets[i].py, COL_BLACK);
    if(Bullets[i].alive)
      DrawBullet(Bullets[i].x, Bullets[i].y, COL_YELLOW);
  }

  // Asteroid
  if(Asteroid.alive){
    if(Asteroid.px != Asteroid.x || Asteroid.py != Asteroid.y)
      EraseAsteroid(Asteroid.px, Asteroid.py);
    DrawAsteroid(Asteroid.x, Asteroid.y);
  } else {
    EraseAsteroid(Asteroid.px, Asteroid.py);
  }

  // Powerup
  if(Pup.alive){
    DrawPowerup(Pup.x, Pup.y);
  } else {
    ErasePowerup(Pup.x, Pup.y);
  }

  // Redraw border and score
  DrawBorder();
  DrawScoreBar();
}

static void ShowRoundResult(uint8_t winner){
  ST7735_FillRect(10, 62, 108, 36, COL_BLACK);
  ST7735_DrawFastHLine(10, 62, 108, COL_WHITE);
  ST7735_DrawFastHLine(10, 97, 108, COL_WHITE);
  ST7735_DrawFastVLine(10, 62,  36, COL_WHITE);
  ST7735_DrawFastVLine(117,62,  36, COL_WHITE);
  ST7735_SetCursor(2, 9);
  if(Language == 0){
    if(winner == 1) ST7735_OutString(" P1 wins round!");
    else            ST7735_OutString(" P2 wins round!");
  } else {
    if(winner == 1) ST7735_OutString(" P1 Ying Le!");
    else            ST7735_OutString(" P2 Ying Le!");
  }
  ST7735_SetCursor(3, 11);
  if(Language == 0) ST7735_OutString("Score: ");
  else              ST7735_OutString("Fen Shu: ");
  ST7735_OutUDec(RoundWins1);
  ST7735_OutString(" - ");
  ST7735_OutUDec(RoundWins2);
  Clock_Delay1ms(2000);
}


static void ShowMatchResult(void){
  ST7735_FillScreen(COL_BLACK);
  ST7735_SetCursor(2, 2);
  if(Language == 0) ST7735_OutString("MATCH OVER");
  else              ST7735_OutString("BI SAI JIE SHU");
  ST7735_SetCursor(0, 4);
  if(Language == 0){
    if(RoundWins1 >= 3) ST7735_OutString(" Player 1 Wins!");
    else                ST7735_OutString(" Player 2 Wins!");
  } else {
    if(RoundWins1 >= 3) ST7735_OutString(" P1 Ying Le!");
    else                ST7735_OutString(" P2 Ying Le!");
  }
  ST7735_SetCursor(0, 6);
  if(Language == 0) ST7735_OutString("Final Score:");
  else              ST7735_OutString("Zui Zhong Fen Shu:");
  ST7735_SetCursor(0, 8);
  ST7735_OutString("  P1: ");
  ST7735_OutUDec(RoundWins1);
  ST7735_SetCursor(0, 9);
  ST7735_OutString("  P2: ");
  ST7735_OutUDec(RoundWins2);
  ST7735_SetCursor(0, 12);
  if(Language == 0) ST7735_OutString("Press any button");
  else              ST7735_OutString("An Ren Yi An Jian");
  ST7735_SetCursor(0, 13);
  if(Language == 0) ST7735_OutString("  to play again");
  else              ST7735_OutString("  Zai Wan Yi Ci");
  while(Switch_In() == 0){}
  Clock_Delay1ms(300);
}
static void ShowMenu(void){
  ST7735_FillScreen(COL_BLACK);
  ST7735_SetCursor(2, 1);
  if(Language == 0) ST7735_OutString("ASTRO PARTY");
  else              ST7735_OutString("ASTRO DAZHAN");
  ST7735_SetCursor(0, 3);
  ST7735_OutString("Malik & Chodi");
  ST7735_SetCursor(0, 6);
  if(Language == 0) ST7735_OutString("Controls:");
  else              ST7735_OutString("Kong Zhi:");
  ST7735_SetCursor(0, 7);
  if(Language == 0) ST7735_OutString("PA24/26 = Rotate");
  else              ST7735_OutString("PA24/26 = Xuan Zhuan");
  ST7735_SetCursor(0, 8);
  if(Language == 0) ST7735_OutString("PA25/PA9 = Shoot");
  else              ST7735_OutString("PA25/PA9 = She Ji");
  ST7735_SetCursor(0, 10);
  if(Language == 0) ST7735_OutString("First to 3 rounds");
  else              ST7735_OutString("Xian dao 3 ju");
  ST7735_SetCursor(0, 11);
  if(Language == 0) ST7735_OutString("wins the match!");
  else              ST7735_OutString("ying de bi sai!");
  ST7735_SetCursor(0, 14);
  if(Language == 0) ST7735_OutString("P1 Shoot = Start");
  else              ST7735_OutString("P1 She Ji = Kai Shi");
  while(!(Switch_In() & 0x02)){}
  Clock_Delay1ms(300);
}


int main2(void){
  __disable_irq();
  Clock_Init80MHz(0);
  LaunchPad_Init();
  Switch_Init();
  Sound_Init();
  __enable_irq();

  while(1){
    uint32_t sw = Switch_In();
    if(sw & 0x01) Sound_Shoot();
    if(sw & 0x02) Sound_Explosion();
    if(sw & 0x04) Sound_Killed();
    if(sw & 0x08) Sound_Highpitch();
    // NO delay - just spin and check buttons constantly
    // SysTick runs in background at 11kHz outputting samples
  }
}


int main(void){
  uint8_t roundWinner;

  __disable_irq();
  PLL_Init();
  LaunchPad_Init();
  ST7735_InitPrintf(INITR_BLACKTAB);   
  Clock_Delay1ms(100);                 
  ST7735_FillScreen(COL_BLACK);
  ADCinit();
  Switch_Init();
  Sound_Init();
  TExaS_Init(0, 0, &TExaS_LaunchPadLogicPB27PB26);
  TimerG12_IntArm(80000000/30, 2);
  __enable_irq();

  while(1){

    RoundWins1 = 0;
    RoundWins2 = 0;
    RoundNumber = 0; 
    ShowLanguageSelect();
    ShowMenu();

    while(RoundWins1 < 3 && RoundWins2 < 3){

      RoundInit();
      DrawAll();
      Semaphore = 0;
      LastButtons = Switch_In();

      // Wait one tick so ISR runs and updates positions
      // then re-sync px/py so first UpdateLCD doesn't see a false move
      while(!Semaphore){}
      Semaphore = 0;
      Ship1.px = Ship1.x;
      Ship1.py = Ship1.y;
      Ship2.px = Ship2.x;
      Ship2.py = Ship2.y;

      // NOW show Ready/Go
      ST7735_SetCursor(4, 9);
      ST7735_OutString("Ready...");
      Clock_Delay1ms(800);
      ST7735_SetCursor(4, 9);
      ST7735_OutString("GO!     ");
      Clock_Delay1ms(400);
      DrawAll();

      // Round loop
      while(!RoundOver){
        while(!Semaphore){}
        Semaphore = 0;
        HandleInput();
        UpdateLCD();
      }

      roundWinner = (!Ship2.alive) ? 1 : 2;
      Clock_Delay1ms(300);
      ShowRoundResult(roundWinner);
    }

    Sound_Highpitch();
    ShowMatchResult();
  }
}