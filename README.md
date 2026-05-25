# Astro Party

A two-player competitive deathmatch game running on a bare-metal MSPM0G3507 
ARM Cortex-M0+ microcontroller. Built for ECE319K (Embedded Systems) at the 
University of Texas at Austin.

## Gameplay
Two ships auto-move across the screen in the direction they face. Players rotate 
their ship and fire bullets to eliminate the opponent. First to win 3 rounds wins 
the match. Asteroids spawn periodically and drop random powerups when shot — 
collect them to gain a shield, speed boost, or slow your opponent.

## Features
- Two-player local multiplayer with 4 external buttons
- 16-direction ship rotation and movement
- 3 procedurally generated maps that rotate each round
- Asteroid powerup system (shield, fast, slow)
- 5-bit resistor ladder DAC audio with slide pot volume control
- English and Chinese language support
- Score tracking across rounds and matches

## Hardware
- MSPM0G3507 LaunchPad
- HiLetGo ST7735 128x160 SPI LCD
- 4 external pushbuttons with pull-up resistors
- Slide potentiometer on PB18
- 5-bit binary-weighted DAC (750Ω, 1.5kΩ, 3kΩ, 6kΩ, 12kΩ) + speaker + LM386 amplifier
