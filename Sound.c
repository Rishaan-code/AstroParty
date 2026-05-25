// Sound.c
// Lab 9 ECE319K - Astro Party
// Rishaan Malik and Abhinav Chodisetti
//
// Sound engine:
//   SysTick fires at 11 kHz (period = 80,000,000 / 11,000 = 7272 ticks).
//   Each ISR call outputs one 5-bit sample to the DAC (PB4-PB0).
//   Playback is one-shot: sound plays once then stops automatically.
//   Calling Sound_Start while a sound plays immediately switches to the new one.
//   Volume is controlled by the slide pot on PB18 (ADC1 ch5).
//     ADCin() -> 0-4095; we map to 0-31 (>>7) and scale the sample.

#include <stdint.h>
#include <ti/devices/msp/msp.h>
#include "Sound.h"
#include "sounds/sounds.h"
#include "../inc/DAC5.h"
#include "../inc/Timer.h"
#include "../inc/ADC1.h"


static const uint8_t *SoundPt    = 0;  // pointer to current sample
static uint32_t       SoundCount = 0;  // samples remaining i think


// Arms SysTick with the given reload period (bus-clock ticks) and priority setted up
// SysTick CTRL bits:  2=clock source (1=core), 1=interrupt enable, 0=enable
void SysTick_IntArm(uint32_t period, uint32_t priority){
  SysTick->CTRL = 0;                             // disable while configuring
  SysTick->LOAD = period - 1;                    // reload value
  SysTick->VAL  = 0;                             // clear counter
  // Priority field sits in SCB->SHP[1] bits 23:21
  SCB->SHP[1] = (SCB->SHP[1] & ~0x00E00000) | ((priority & 0x07) << 21);
  SysTick->CTRL = 0x07;                          // core clock, IRQ on, enable
}


// Call once at startup.
// Initializes DAC, arms SysTick at 11 kHz, but leaves interrupt DISABLED
// until Sound_Start is called (CTRL = 0x05 = no interrupt bit).
void Sound_Init(void){
  DAC5_Init();
  SoundPt    = 0;
  SoundCount = 0;
  SysTick->CTRL = 0;
  SysTick->LOAD = (80000000 / 11000) - 1;   // 7271 -> ~11.0 kHz
  SysTick->VAL  = 0;
  SCB->SHP[1]   = (SCB->SHP[1] & ~0x00E00000) | (2 << 21);  // priority 2
  SysTick->CTRL = 0x05;    // core clock, interrupt DISABLED, counter running
}

// Called at 11 kHz do not change pls
// Outputs one 5-bit sample; auto-stops when the array is exhausted.
// Volume scaling: raw (0-255) >> 3 gives 0-31, then scaled by volume (0-31).
// Result = (sample5bit * volume) >> 5, still fits in 5 bits.
void SysTick_Handler(void){
  if(SoundCount == 0){
    DAC5_Out(0);
    SysTick->CTRL &= ~0x02;   // clear interrupt-enable -> silence
    return;
  }

  uint32_t sample = (*SoundPt++) >> 3;  // 8-bit -> 5-bit  (0-31)
  SoundCount--;

  // Volume from slide pot: read ADC, scale to 0-31
  uint32_t vol = ADCin() >> 7;          // 0-4095 -> 0-31
  sample = (sample * vol) >> 5;         // scale, keep 0-31

  DAC5_Out(sample & 0x1F);
}

// Load new wave and (re-)enable the SysTick interrupt to start playback, do not mess w
void Sound_Start(const uint8_t *pt, uint32_t count){
  SysTick->CTRL &= ~0x02;   // briefly disable IRQ while updating pointers
  SoundPt    = pt;
  SoundCount = count;
  SysTick->CTRL |= 0x02;    // re-enable IRQ -> playback starts immediately
}

// Array names and sizes come from sounds/sounds.h keep same

void Sound_Shoot(void){
  Sound_Start(shoot, 4080);
}

void Sound_Killed(void){
  Sound_Start(invaderkilled, 3377);
}

void Sound_Explosion(void){
  Sound_Start(explosion, 2000);
}

void Sound_Fastinvader1(void){
  Sound_Start(fastinvader1, 982);
}

void Sound_Fastinvader2(void){
  Sound_Start(fastinvader2, 1042);
}

void Sound_Fastinvader3(void){
  Sound_Start(fastinvader3, 1054);
}

void Sound_Fastinvader4(void){
  Sound_Start(fastinvader4, 1098);
}

void Sound_Highpitch(void){
  Sound_Start(highpitch, 1802);
}