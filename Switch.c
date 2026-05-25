/*
 * Switch.c
 * Lab 9 ECE319K - Astro Party
 * Rishaan Malik and Abhinav Chodisetti
 *
 * Four buttons, all active-low (pressed = pin LOW, pull-up resistor to 3.3V).
 *
 *   PA24 -> bit 0  Player 1 Rotate
 *   PA25 -> bit 1  Player 1 Shoot
 *   PA8  -> bit 2  Player 2 Rotate
 *   PA9  -> bit 3  Player 2 Shoot
 *
 * Switch_In() returns active-HIGH:
 *   bit set = 1 means that button is currently pressed.
 *
 * PINCM indices verified from MSPM0G3507 datasheet Table 6-1:
 *   PA8  = PINCM19
 *   PA9  = PINCM20
 *   PA24 = PINCM54
 *   PA25 = PINCM55
 *
 * PINCM value 0x00040081:
 *   bit  0    PIPEN  = 1  enable pin function
 *   bit  7    INENA  = 1  enable input buffer
 *   bits 19:18 = 01       pull-up resistor enabled
 */

#include <stdint.h>
#include <ti/devices/msp/msp.h>
#include "../inc/LaunchPad.h"

#define PA24_PINCM  53   // Player 1 Rotate
#define PA25_PINCM  54   // Player 1 Shoot
#define PA8_PINCM   18   // Player 2 Rotate
#define PA9_PINCM   19   // Player 2 Shoot

void Switch_Init(void){
  IOMUX->SECCFG.PINCM[PA24_PINCM] = 0x00040081;
  IOMUX->SECCFG.PINCM[PA25_PINCM] = 0x00040081;
  IOMUX->SECCFG.PINCM[PA8_PINCM]  = 0x00040081;
  IOMUX->SECCFG.PINCM[PA9_PINCM]  = 0x00040081;
}

uint32_t Switch_In(void){
  uint32_t din    = GPIOA->DIN31_0;
  uint32_t result = 0;
  if(!(din & (1UL << 24))) result |= 0x01;  // PA24 low -> P1 Rotate
  if(!(din & (1UL << 25))) result |= 0x02;  // PA25 low -> P1 Shoot
  if(!(din & (1UL <<  8))) result |= 0x04;  // PA8  low -> P2 Rotate
  if(!(din & (1UL <<  9))) result |= 0x08;  // PA9  low -> P2 Shoot
  return result;
}