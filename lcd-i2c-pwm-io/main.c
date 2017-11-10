/* lcd-i2c-pwm-io
   Copyright (C) 2017 Nils Eilers <nils.eilers@gmx.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdbool.h>

#include "usi_twi_slave.h"
#include "i2c-lcd-pwm-io-config.h"


static inline void set_io(uint8_t value);


void usiTwiWriteReg(uint8_t i2c_register, uint8_t value) {
   switch (i2c_register) {
      case PWM_CONTRAST:   OCR0B = value; break;     // PWM 0
      case PWM_BRIGHTNESS: OCR1B = value; break;     // PWM 1
      case IO_IEC:         set_io(value);            // digital output
      default:
              break; // ignore writes to non-existent registers

   }
}

static inline void init_timer0(void)
{
   DDRB |= _BV(PB1);    // Define pin as output
   TCCR0B = _BV(CS01);  // Prescaler = 8
   OCR0B = PWM0_DEFAULT;
   // Fast PWM mode
   TCCR0A = _BV(COM0B1) | _BV(COM0B0) | _BV(WGM01) | _BV(WGM00);
}

static inline void init_timer1(void)
{
   DDRB |= _BV(PB4);     // Define pin as output
   TCCR1  =  (1 << CS13) | (1 << CS10);

   TCCR1  |=  (1 << COM1A1) | (1 << COM1A0);
   GTCCR  =  (1 << COM1B1) | (1 << COM1B0);
   OCR1B  =  PWM1_DEFAULT;
   GTCCR  |=  (1 << PWM1B);  // enable PWMB
}

static inline void init_io(void)
{
   DDRB |= _BV(PB3);     // Define pin as output
   PORTB |= _BV(PB3);    // Default: output hi
   set_io(DIGI_DEFAULT);
}

static inline void set_io(uint8_t value)
{
   if (value > 0)
      PORTB |= _BV(PB3);
   else
      PORTB &= ~_BV(PB3);
}

int main(void)
{
   // Init
   usiTwiSlaveInit(I2C_SLAVE_ADDRESS);
   init_timer0();
   init_timer1();
   init_io();

   // Main loop: do nothing
   while (1)
   {
      set_sleep_mode(SLEEP_MODE_IDLE);
      sei();            // Enable interrupts
      sleep_cpu();
   }
}
