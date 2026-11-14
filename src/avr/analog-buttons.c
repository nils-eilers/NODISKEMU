/* Robust ADC resistor-ladder button acquisition for petSD+/cbmSD-mini.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <stdbool.h>
#include <stdint.h>
#include <util/atomic.h>
#include "config.h"
#include "timer.h"
#include "progmem.h"
#include "analog-buttons.h"
#ifdef IEC_SLOW_IEEE_FAST
#include <avr/power.h>
#endif

#define SAMPLES 5
#define HYSTERESIS 4
#define ADC_MIN 540
#define ADC_MAX 1023

/* Nominal centers: 572, 606, 652, 697, 776, 839, 931, 1023.
 * Four-count guard bands on either side of the midpoints separate the
 * entry windows. Hysteresis extends a held state into its guard band.
 * These are fixed hardware windows, not an assumed power-on calibration.
 */
static const PROGMEM struct {
  uint16_t low, high;
  uint8_t keys;
} windows[] = {
  {540, 585, 0},
  {593, 625, KEY_PREV},
  {633, 671, KEY_NEXT},
  {679, 733, KEY_PREV | KEY_NEXT},
  {741, 804, KEY_SEL},
  {812, 881, KEY_PREV | KEY_SEL},
  {889, 973, KEY_NEXT | KEY_SEL},
  {981, 1023, KEY_PREV | KEY_NEXT | KEY_SEL}
};

static uint16_t samples[SAMPLES];
static uint8_t sample_count, sample_pos, previous;
static bool discard_first;
/* Strict window result for boot checks, independent of held-state hysteresis.
 * The sequence also records invalid excursions between foreground checks. */
static volatile uint8_t boot_keys, boot_sequence, sample_sequence;

uint8_t analog_buttons_decode(uint16_t adc, uint8_t held) {
  if (adc < ADC_MIN || adc > ADC_MAX) return ANALOG_BUTTON_INVALID;

  if (held != ANALOG_BUTTON_INVALID) {
    for (uint8_t i = 0; i < sizeof(windows) / sizeof(windows[0]); i++) {
      if (pgm_read_byte(&windows[i].keys) == held &&
          adc + HYSTERESIS >= pgm_read_word(&windows[i].low) &&
          adc <= pgm_read_word(&windows[i].high) + HYSTERESIS)
        return held;
    }
  }
  for (uint8_t i = 0; i < sizeof(windows) / sizeof(windows[0]); i++) {
    if (adc >= pgm_read_word(&windows[i].low) &&
        adc <= pgm_read_word(&windows[i].high))
      return pgm_read_byte(&windows[i].keys);
  }
  return ANALOG_BUTTON_INVALID;
}

static void update_boot_keys(uint8_t keys) {
  if (keys != boot_keys) {
    boot_keys = keys;
    boot_sequence++;
    sample_sequence = 0;
  }
}

void analog_buttons_init(void) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ADCSRA = 0;
    DDRA &= (uint8_t)~_BV(PA7);
    PORTA &= (uint8_t)~_BV(PA7);
    ADMUX = _BV(REFS0) | _BV(MUX0) | _BV(MUX1) | _BV(MUX2);
    ADCSRB = 0;
    DIDR0 |= _BV(ADC7D);
    sample_count = sample_pos = 0;
    previous = boot_keys = ANALOG_BUTTON_INVALID;
    boot_sequence++;
    discard_first = true;
    /* Do not wait here: the timer collects completed conversions and
     * discards the first one after enabling the ADC/reference. */
    ADCSRA = _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0) | _BV(ADEN) | _BV(ADSC);
  }
}

uint8_t analog_buttons_poll(void) {
  if (!(ADCSRA & _BV(ADEN)) || (ADCSRA & _BV(ADSC))) {
    sample_count = sample_pos = 0;
    previous = ANALOG_BUTTON_INVALID;
    update_boot_keys(ANALOG_BUTTON_INVALID);
    return 0;
  }
  uint16_t adc = ADCW;
  ADCSRA |= _BV(ADSC);
  if (discard_first) {
    discard_first = false;
    return 0;
  }
  /* Electrical faults must not be hidden by a median of older samples. */
  if (adc < ADC_MIN || adc > ADC_MAX) {
    sample_count = sample_pos = 0;
    previous = ANALOG_BUTTON_INVALID;
    update_boot_keys(ANALOG_BUTTON_INVALID);
    return 0;
  }
  samples[sample_pos++] = adc;
  sample_sequence++;
  if (sample_pos == SAMPLES) sample_pos = 0;
  if (sample_count < SAMPLES) sample_count++;
  if (sample_count < SAMPLES) return 0;

  uint16_t sorted[SAMPLES];
  for (uint8_t i = 0; i < SAMPLES; i++) sorted[i] = samples[i];
  for (uint8_t i = 1; i < SAMPLES; i++) {
    uint16_t value = sorted[i];
    uint8_t j = i;
    while (j && sorted[j - 1] > value) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = value;
  }
  adc = sorted[SAMPLES / 2];
  update_boot_keys(analog_buttons_decode(adc, ANALOG_BUTTON_INVALID));
  previous = analog_buttons_decode(adc, previous);
  return previous == ANALOG_BUTTON_INVALID ? 0 : previous;
}

uint8_t analog_buttons_boot_keys(void) {
  uint8_t keys = ANALOG_BUTTON_INVALID, sequence = 0, fresh = 0, stable = 0;

  /* Bounded even if the timer or ADC stops. Match the actual CPU clock,
   * including early boot before the IEC/IEEE prescaler is selected. */
  for (uint8_t i = 0; i < 40; i++) {
#ifdef IEC_SLOW_IEEE_FAST
    if (clock_prescale_get() == clock_div_1) delay_ms(20);
    else
#endif
      delay_ms(10);
    if (i < 9) continue;
    uint8_t now, generation, sample;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      now = boot_keys;
      generation = boot_sequence;
      sample = sample_sequence;
    }
    if (now == ANALOG_BUTTON_INVALID || sample == fresh) stable = 0;
    else if (now != keys || generation != sequence) stable = 1;
    else stable++;
    keys = now;
    sequence = generation;
    fresh = sample;
  }
  return stable >= 31 ? keys : 0;
}
