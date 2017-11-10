/*-
 * Copyright (c) 2015 Nils Eilers. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* System tick counter and button debouncer with repeat function
   Based on code by Peter Dannegger
*/

#include <stdint.h>
#include "config.h"
#include "timer.h"


// System tick counter
volatile tick_t ticks;


// Shared with assembly interrupt routine:
volatile uint8_t key_state;     // debounced key state: bit = 1: key pressed
volatile uint8_t key_press;     // key press detect
volatile uint8_t key_rpt;       // key long press and repeat

// Used by the assembly interrupt routine:
volatile uint8_t key_ct0;               // 8 vertical counters, bit 0
volatile uint8_t key_ct1;               // 8 vertical counters, bit 1
volatile uint8_t key_ct2;               // 8 vertical counters, bit 2
volatile uint8_t key_repeat_counter;    // repeat counter


// Check if a key has been pressed. Each pressed key is reported only once
uint8_t get_key_press(uint8_t key_mask) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    key_mask &= key_press;      // read key(s)
    key_press ^= key_mask;      // clear key(s)
  }
  return key_mask;
}

/* Check if a key has been pressed long enough such that the key repeat
   functionality kicks in. After a small setup delay the key is reported
   being pressed in subsequent calls to this function. This simulates the
   user repeatedly pressing and releasing the key.
*/

uint8_t get_key_rpt(uint8_t key_mask) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    key_mask &= key_rpt;        // read key(s)
    key_rpt ^= key_mask;        // clear key(s)
  }
  return key_mask;
}


uint8_t get_key_autorepeat(uint8_t key_mask) {
   return get_key_press(key_mask) || get_key_rpt(key_mask);
}


// Check if a key is pressed right now
uint8_t get_key_state(uint8_t key_mask) {
  key_mask &= key_state;
  return key_mask;
}


uint8_t get_key_short(uint8_t key_mask) {
  uint8_t tmp;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    tmp = get_key_press(~key_state & key_mask);
  }
  return tmp;
}


uint8_t get_key_long(uint8_t key_mask) {
  return get_key_press(get_key_rpt(key_mask));
}

void wait_anykey(void) {
  while (get_key_state(KEY_ANY));
  while (!get_key_state(KEY_ANY));
  while (get_key_state(KEY_ANY));
  key_state = key_press = key_rpt = key_ct0 = key_ct1 = key_ct2 = key_repeat_counter = 0;
}
