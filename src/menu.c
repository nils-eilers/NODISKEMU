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

// LCD menu system

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "lcd.h"
#include "menu.h"
#include "timer.h"
#include "errormsg.h"
#include "bus.h"
#include "eeprom-conf.h"
#include "rtc.h"
#include "ustring.h"
#include "doscmd.h"


static tick_t lcd_timeout;
static bool lcd_timer;
static uint16_t lcd_current_screen;



static inline uint8_t min(uint8_t a, uint8_t b) {
  if (a < b) return a;
  return b;
}


void lcd_update_device_addr(void) {
  if (lcd_current_screen == SCRN_STATUS) {
    lcd_home();
    lcd_printf("Device ID: #%d ", device_address);
  }
}


void lcd_update_disk_status(void) {
  bool visible = true;

  if (lcd_current_screen == SCRN_STATUS) {
    lcd_locate(0, 1);
    lcd_puts_P(PSTR("Status: "));
    for (uint8_t i = 0;
         i < min(CONFIG_ERROR_BUFFER_SIZE, LCD_COLS * (LCD_LINES > 3 ? 3:
             1) - 8);
         i++)
    {
      if (error_buffer[i] == 13) visible = false;
      lcd_putc(visible ? error_buffer[i] : ' ');
    }
  }
}


void lcd_draw_screen(uint16_t screen) {
  extern const char PROGMEM versionstr[];

  lcd_current_screen = screen;
  lcd_clear();

  switch (screen) {
  case SCRN_SPLASH:
    lcd_puts_P(versionstr);
    lcd_locate(0,3); lcd_puts_P(PSTR(HWNAME));
    // TODO: if available, print serial number here
    break;

  case SCRN_STATUS:
    lcd_update_device_addr();
    lcd_update_disk_status();
    break;

  default:
    break;
  }
}


void lcd_splashscreen(void) {
  lcd_timeout = getticks() + MS_TO_TICKS(1000 * 5);
  lcd_timer = true;
}


static void menu_select_status(void) {
  lcd_draw_screen(SCRN_STATUS);
  lcd_timer = false;
}


void handle_lcd(void) {
  tick_t ticks;

  if (lcd_timer) {
    ticks = getticks();
    if (time_before(lcd_timeout, ticks)) menu_select_status();
  }
}


int8_t menu_vertical(uint8_t min, uint8_t max) {
  uint8_t pos = 0;

  lcd_cursor(true);
  for (;;) {
    lcd_locate(0, pos);
    if (get_key_autorepeat(KEY_PREV)) {
      if (pos == 0) pos = max;
      else --pos;
    }
    if (get_key_autorepeat(KEY_NEXT)) {
      if (pos < max) ++pos;
      else pos = 0;
    }
    if (get_key_press(KEY_SEL)) break;
  }
  lcd_cursor(false);
  return pos;
}


uint8_t menu_edit_value(uint8_t v, uint8_t min, uint8_t max) {
  uint8_t x = lcd_x;
  uint8_t y = lcd_y;

  lcd_locate(x, y);
  lcd_cursor(true);
  set_busy_led(true);
  for (;;) {
    lcd_printf("%02d", v);
    lcd_locate(x, y);
    for (;;) {
      if (get_key_autorepeat(KEY_PREV)) {
        if (v <= min) v = max;
        else --v;
        break;
      }
      if (get_key_autorepeat(KEY_NEXT)) {
        if (v >= max) v = min;
        else ++v;
        break;
      }
      if (get_key_press(KEY_SEL)) {
        lcd_cursor(false);
        set_busy_led(false);
        return v;
      }
    }
  }
}


void menu_ask_store_settings(void) {
  bool store = true;
  lcd_clear();
  lcd_printf("Save settings?\n\nyes     no");
  lcd_cursor(true);
  for (;;) {
    if (store) lcd_locate(0,2);
    else       lcd_locate(8,2);
    for (;;) {
      if (get_key_press(KEY_PREV) || get_key_press(KEY_NEXT)) {
        store = !store;
        break;
      }
      if (get_key_press(KEY_SEL)) {
        if (store) write_configuration();
        lcd_cursor(false);
        return;
      }
    }
  }
}


void menu_device_number(void) {
  lcd_printf("Change device number\nfrom %02d to:", device_address);
  lcd_locate(12, 1);
  device_address = menu_edit_value(device_address, 8, 30);
  menu_ask_store_settings();
}


static const PROGMEM uint8_t monthnames[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";
static const PROGMEM uint8_t menu_setclk_pos[] = {0, 3, 9, 12, 15, 18, 0, 8};
static const PROGMEM uint8_t number_of_days[] = {
  31, // January
  28, // February
  31, // March
  30, // April
  31, // May
  30, // June
  31, // July
  31, // August
  30, // September
  31, // October
  30, // November
  31  // December
};

enum set_clock_fields { SETCLK_MDAY, SETCLK_MON, SETCLK_YEAR,
  SETCLK_HOUR, SETCLK_MIN, SETCLK_SEC, SETCLK_SET, SETCLK_ABORT };


void menu_print_month(uint8_t m) {
  const char* p = (const char*) monthnames;

  p += m*3;
  lcd_putc(pgm_read_byte(p++));
  lcd_putc(pgm_read_byte(p++));
  lcd_putc(pgm_read_byte(p));
}


uint8_t menu_edit_month(uint8_t m) {
  for (;;) {
    lcd_locate(3, 0);
    menu_print_month(m);
    lcd_locate(3, 0);
    for (;;) {
      if (get_key_autorepeat(KEY_PREV)) {
        if (m == 0) m = 11;
        else --m;
        break;
      }
      if (get_key_autorepeat(KEY_NEXT)) {
        if (m == 11) m = 0;
        else ++m;
        break;
      }
      if (get_key_press(KEY_SEL)) return m;
    }
  }
}


uint8_t calc_number_of_days(uint8_t month, uint8_t year) {
  uint8_t days = pgm_read_byte(&number_of_days[month]);
  if ((month == 1) && (year % 4 == 0)) ++days;
  return days;
}


void menu_set_clock(void) {
  struct tm t;
  uint8_t p = SETCLK_MDAY;
  uint8_t days;

  switch (rtc_state) {
    case RTC_INVALID:
      memset(&t, 0, sizeof(t));
      t.tm_mday = 1;
      t.tm_year = 115;
      break;

    case RTC_OK:
      read_rtc(&t);
      break;

    case RTC_NOT_FOUND:
    default:
      return;
  }

menu_set_clock_restart:

  lcd_clear();
  lcd_printf("%02d-MMM-20%2d %02d:%02d:%02d",
      t.tm_mday, t.tm_year - 100, t.tm_hour, t.tm_min, t.tm_sec);
  lcd_locate(3, 0);
  menu_print_month(t.tm_mon);
  lcd_locate(0, LCD_LINES - 1);
  lcd_puts_P(PSTR("Set     Abort"));
  for (;;) {
    set_busy_led(false);
    lcd_locate(pgm_read_byte(&(menu_setclk_pos[p])), p >= SETCLK_SET ? LCD_LINES - 1: 0);
    lcd_cursor(true);
    if (get_key_autorepeat(KEY_PREV)) {
      if (p == 0) p = SETCLK_ABORT;
      else --p;
    }
    if (get_key_autorepeat(KEY_NEXT)) {
      if (p == SETCLK_ABORT) p = 0;
      else ++p;
    }
    if (get_key_press(KEY_SEL)) {
      set_busy_led(true);
      switch (p) {
        case SETCLK_MDAY:
          days = calc_number_of_days(t.tm_mon, t.tm_year);
          if (t.tm_mday > days) t.tm_mday = days;
          t.tm_mday = menu_edit_value(t.tm_mday, 1, days);
          break;
        case SETCLK_MON:
          t.tm_mon = menu_edit_month(t.tm_mon);
          break;
        case SETCLK_YEAR:
          t.tm_year = menu_edit_value(t.tm_year - 100, 15, 99) + 100;
          break;
        case SETCLK_HOUR:
          t.tm_hour = menu_edit_value(t.tm_hour, 0, 23);
          break;
        case SETCLK_MIN:
          t.tm_min = menu_edit_value(t.tm_min, 0, 59);
          break;
        case SETCLK_SEC:
          t.tm_sec = menu_edit_value(t.tm_sec, 0, 59);
          break;
        case SETCLK_SET:
          set_busy_led(false);
          t.tm_wday = day_of_week(t.tm_year, t.tm_mon, t.tm_mday);
          days = calc_number_of_days(t.tm_mon, t.tm_year);
          if (t.tm_mday > days) {
            lcd_clear();
            lcd_puts_P(PSTR("Sorry, "));
            menu_print_month(t.tm_mon);
            days = calc_number_of_days(t.tm_mon, t.tm_year);
            lcd_printf(" has only %d days, not %d", days, t.tm_mday);
#if LCD_LINES >= 4
            lcd_puts_P(PSTR("\n\nPress any key..."));
#endif
            while (!get_key_press(KEY_ANY));
            t.tm_mday = days;
            p = SETCLK_MDAY;
            goto menu_set_clock_restart;
          } else {
            set_rtc(&t);
            goto menu_set_clock_exit;
          }
          break;
        default:
          goto menu_set_clock_exit;
      }
    }
  }
menu_set_clock_exit:
  set_busy_led(false);
}


void menu(void) {
  uint8_t sel;
  bus_sleep(true);

  menu_select_status();
  for (;;) {
    lcd_clear();
    lcd_printf("Exit Menu\nChange device number\n");
    if (rtc_state == RTC_NOT_FOUND)
      lcd_printf("Clock not found");
    else
      lcd_printf("Set clock");
    sel = menu_vertical(0,2);
    lcd_clear();
    if (sel == 1) menu_device_number();
    else if (sel == 2) menu_set_clock();
    else break;
  }

  bus_sleep(false);
  lcd_draw_screen(SCRN_STATUS);
}

