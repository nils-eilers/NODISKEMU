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
#include <stdbool.h>
#include "config.h"


enum {
  SCRN_SPLASH = 1,      // Software version info, hardware name
  SCRN_STATUS           // Disk status, device number, clock
};


#ifdef CONFIG_ONBOARD_DISPLAY

extern uint8_t menu_system_enabled;

void lcd_splashscreen(void);
void lcd_draw_screen(uint16_t screen);
void lcd_refresh(void);
void lcd_update_device_addr(void);
void lcd_update_disk_status(void);
void handle_lcd(void);
bool handle_buttons(void);
bool menu(void);
void menu_adjust_contrast(void);
void menu_adjust_brightness(void);

static inline void lcd_bootscreen(void) {
  lcd_draw_screen(SCRN_SPLASH);
}

#else

#define menu_system_enabled (0)

static inline void lcd_bootscreen(void) {}
static inline void lcd_splashscreen(void) {}
static inline void lcd_draw_screen(uint16_t screen) {}
static inline void lcd_refresh(void) {}
static inline void lcd_update_device_addr(void) {}
static inline void handle_lcd(void) {}
static inline bool handle_buttons(void) { return false; }
static inline void lcd_update_disk_status(void) {}
static inline bool menu(void) { return false; }
static inline void menu_adjust_contrast(void) {}
static inline void menu_adjust_brightness(void) {}

#endif
