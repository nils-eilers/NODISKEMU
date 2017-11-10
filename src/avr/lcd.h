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
#include <stdbool.h>

#define lcd_printf(fmt, ...) lcd_printf_P(PSTR(fmt), ##__VA_ARGS__)

#ifdef CONFIG_ONBOARD_DISPLAY

extern uint8_t lcd_x; // 0..LCD_COLS-1
extern uint8_t lcd_y; // 0..LCD_LINES-1
extern uint8_t lcd_contrast;
extern uint8_t lcd_brightness;

void lcd_init(void);
void lcd_clear(void);
void lcd_home(void);
void lcd_locate(uint8_t x, uint8_t y);
void lcd_send_command(uint8_t cmd);
void lcd_putc(char c);
void lcd_puts(const char *s);
void lcd_puts_P(const char *progmem_s);
void lcd_printf_P(const char *fmt, ...);
void lcd_cursor(bool on);
void lcd_clrlines(uint8_t from, uint8_t to);
uint8_t lcd_set_contrast(uint8_t contrast);
uint8_t lcd_set_brightness(uint8_t contrast);

#else

static inline void lcd_init(void) {}
static inline void lcd_clear(void) {}
static inline void lcd_home(void) {}
static inline void lcd_locate(uint8_t x, uint8_t y) {}
static inline void lcd_send_command(uint8_t cmd) {}
static inline void lcd_putc(char c) {}
static inline void lcd_puts(const char *s) {}
static inline void lcd_puts_P(const char *progmem_s) {}
static inline void lcd_printf_P(const char *fmt, ...) {}
static inline void lcd_cursor(bool on) {}
static inline void lcd_clrlines(uint8_t from, uint8_t to) {}
static inline uint8_t lcd_set_contrast(uint8_t contrast) { return 1; }
static inline uint8_t lcd_set_brightness(uint8_t contrast) { return 1; }
#endif
