/* ADC resistor-ladder buttons on petSD+ and cbmSD-mini. */
#ifndef ANALOG_BUTTONS_H
#define ANALOG_BUTTONS_H

#include <stdint.h>

#define ANALOG_BUTTON_INVALID 0xff

void analog_buttons_init(void);
/* Called once per timer interrupt; returns a key mask, or zero if invalid. */
uint8_t analog_buttons_poll(void);
/* Pure decoder: previous is a key mask or ANALOG_BUTTON_INVALID. */
uint8_t analog_buttons_decode(uint16_t adc, uint8_t previous);
/* Bounded startup check: 100 ms settling, then 300 ms stable input. */
uint8_t analog_buttons_boot_keys(void);

#endif
