/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2014  Ingo Korb <ingo@akana.de>

   NODISKEMU is a fork of sd2iec by Ingo Korb (et al.), http://sd2iec.de

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   timer.h: System timer (and button-debouncer)

*/

#ifndef TIMER_H
#define TIMER_H

#include "arch-timer.h"
#include "atomic.h"


/// Global timing variable, 100 ticks per second
/// Use getticks() !
extern volatile tick_t ticks;

/**
 * getticks - return the current system tick count
 *
 * This inline function returns the current system tick count.
 */
static inline tick_t getticks(void) {
  tick_t tmp;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    tmp = ticks;
  }
  return tmp;
}

#define HZ 100

#define MS_TO_TICKS(x) (x/10)

/* Adapted from Linux 2.6 include/linux/jiffies.h:
 *
 *      These inlines deal with timer wrapping correctly. You are
 *      strongly encouraged to use them
 *      1. Because people otherwise forget
 *      2. Because if the timer wrap changes in future you won't have to
 *         alter your driver code.
 *
 * time_after(a,b) returns true if the time a is after time b.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 * (">=0" refers to the time_after_eq macro which wasn't copied)
 */
#define time_after(a,b)         \
         ((stick_t)(b) - (stick_t)(a) < 0)
#define time_before(a,b)        time_after(b,a)


/* Timer initialisation - defined in $ARCH/arch-timer.c */
void timer_init(void);



// Bit masks for the keys
#define KEY_SEL     (1<<0)
#define KEY_NEXT    (1<<1)
#define KEY_PREV    (1<<2)
#define KEY_ANY     0xff



/* Check if a key has been pressed long enough such that the key repeat
   functionality kicks in. After a small setup delay the key is reported
   being pressed in subsequent calls to this function. This simulates the
   user repeatedly pressing and releasing the key.
*/
uint8_t get_key_rpt(uint8_t key_mask);
uint8_t get_key_autorepeat(uint8_t key_mask);

// Check if a key has been pressed. Each pressed key is reported only once
uint8_t get_key_press(uint8_t key_mask);

// Check if a key is pressed right now
uint8_t get_key_state(uint8_t key_mask);

// Check if a key has been pressed for a short while
uint8_t get_key_short(uint8_t key_mask);

// Check if a key has been pressed for a long while
uint8_t get_key_long(uint8_t key_mask);


#endif
