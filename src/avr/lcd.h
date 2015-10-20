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


#pragma once
#include "config.h"

enum {
  SCRN_SPLASH = 1,      // Software version info, hardware name
  SCRN_STATUS           // Disk status, device number, clock
};



#ifdef CONFIG_ONBOARD_DISPLAY

extern uint8_t lcd_x; // 0..LCD_COLS-1
extern uint8_t lcd_y; // 0..LCD_LINES-1

void lcd_init(void);
void lcd_clear(void);
void lcd_home(void);
void lcd_locate(uint8_t x, uint8_t y);
void lcd_send_command(uint8_t cmd);
void lcd_putc(char c);
void lcd_puts(const char *s);
void lcd_puts_P(const char *progmem_s);
void lcd_printf_P(const char *fmt, ...);
#define lcd_printf(fmt, ...) lcd_printf_P(PSTR(fmt), ##__VA_ARGS__)

void lcd_splashscreen(void);
void lcd_draw_screen(uint16_t screen);
void lcd_refresh(uint16_t screen);
void lcd_update_device_addr(void);
void handle_lcd(void);

static inline void lcd_bootscreen(void) {
  lcd_draw_screen(SCRN_SPLASH);
}

#else

static inline void lcd_bootscreen(void) {}
static inline void lcd_splashscreen(void) {}
static inline void lcd_draw_screen(uint16_t screen) {}
static inline void lcd_refresh(uint16_t screen) {}
static inline void lcd_update_device_addr(void) {}
static inline void lcd_clear(void) {}
static inline void lcd_locate(uint8_t x, uint8_t y) {}
static inline void lcd_printf(const char *fmt, ...) {}
static inline void handle_lcd(void) {}

#endif
