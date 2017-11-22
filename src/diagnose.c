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

#include <stdio.h>
#include <avr/boot.h>
#include <util/atomic.h>

#include "config.h"
#include "timer.h"
#include "led.h"
#include "progmem.h"
#include "i2c.h"
#include "lcd.h"


// petSD+ diagnose
#if (CONFIG_HARDWARE_VARIANT == HW_PETSDPLUS) && defined(CONFIG_ONBOARD_DISPLAY)
#include "lcd.h"

void board_diagnose(void) {
  char buffer[32];
  unsigned int counter = 0;

  lcd_init();
  lcd_bootscreen();
  lcd_clear();
  sdcard_interface_init();
  buttons_init();

  DDRD |= _BV(PD0) | _BV(PD1);

  //               0....:....1....:....
  lcd_puts_P(PSTR("ADC: ---- CD:- WP:\n"));

  uint8_t lfuse, hfuse, efuse;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    lfuse = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
    hfuse = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
    efuse = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
  }

  snprintf(buffer, sizeof(buffer), "L:%02X H:%02X E:%02X ",
        lfuse, hfuse, efuse);
  lcd_puts(buffer);
  if (lfuse == CONFIG_LFUSE &&
      hfuse == CONFIG_HFUSE &&
      efuse == CONFIG_EFUSE) lcd_puts_P(PSTR("OK"));
  else lcd_puts_P(PSTR("BAD"));

  lcd_locate(0, 2); lcd_puts_P(PSTR("PWM:"));
  int16_t res = i2c_read_register(I2C_SLAVE_ADDRESS, I2C_SOFTWARE_VERSION);
  if (res < 0) lcd_puts_P(PSTR("--"));
  else lcd_printf("%02X", res);

  for (;;) {
    if (counter & 4) {
      PORTD |=  _BV(PD1); set_dirty_led(0);
    } else {
      PORTD &= ~_BV(PD1); set_dirty_led(1);
    }

    lcd_locate(5,0);
    snprintf(buffer, sizeof(buffer), "%4u", ADCW);
    lcd_puts(buffer);

    lcd_locate(13,0);
    lcd_putc(sdcard_detect() ? 'Y' : 'N');

    lcd_locate(18,0);
    lcd_putc(sdcard_wp() ? 'Y' : 'N');

    // Timer interrupt not initialized, so manual start
    // of ADC conversion required
    ADCSRA |= _BV(ADSC);
    delay_ms(200);
    counter++;
  }
}

#else

// Basic diagnose
void board_diagnose(void) {
#  if defined(HAVE_SD) && BUTTON_PREV != 0
     while (!get_key_press(KEY_NEXT)) {
       set_dirty_led(sdcard_detect());
#    ifndef SINGLE_LED
       set_busy_led(sdcard_wp());
#    endif
     }
#  endif
}
#endif

