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

#include <stdbool.h>
#include <avr/io.h>

#if F_CPU != 18432000UL
#error ADC divider may not fit for your clock frequency (F_CPU)
#endif


void adc_init(void) {
  // AVcc as voltage reference
  ADMUX |= _BV(REFS0);

  // divider 128: 18.432 MHz / 128 = 144 kHz (50 kHz..200 kHz)
  ADCSRA |= _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);

  // ADCSRB: free running mode selected by default

  // disable digitial input register for PA7
  DIDR0 |= _BV(ADC7D);

  // select ADC7
  ADMUX |= _BV(MUX0) | _BV(MUX1) | _BV(MUX2);

  // enable ADC
  ADCSRA |= _BV(ADEN);

  // Start first conversion
  ADCSRA |= _BV(ADSC);

  // Wait for conversion to complete
  while (ADCSRA & _BV(ADSC));

  // dummy read of result
  (void) ADCW;
}


uint16_t adc_value(void) {
  ADCSRA |= _BV(ADSC);          // start conversion
  while (ADCSRA & _BV(ADSC));   // wait for conversion done
  return ADCW;
}
