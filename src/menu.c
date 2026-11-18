/*-
 * Copyright (c) 2015, 2017 Nils Eilers. All rights reserved.
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
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "config.h"
#include "lcd.h"
#include "led.h"
#include "menu.h"
#include "timer.h"
#include "errormsg.h"
#include "bus.h"
#include "eeprom-conf.h"
#include "rtc.h"
#include "ustring.h"
#include "doscmd.h"
#include "i2c.h"
#include "uart.h"
#include "dirent.h"
#include "parser.h"     // current_part
#include "wrapops.h"
#include "fatops.h"     // pet2ascn()
#include "doscmd.h"
#include "multidrive.h"


uint8_t menu_system_enabled = true;

#define MAX_LASTPOS 16
#define ENTRIES_PER_BLOCK (256 / sizeof(entry_t))
#define DIRNAV_OFFSET 2
#define NAV_ABORT 0
#define NAV_PARENT 1
#define E_DIR   1
#define E_IMAGE 2

typedef struct {
  uint8_t  flags;
  uint8_t  filename[16];
  uint16_t filesize;
  uint32_t ordinal; // Stable tie-breaker for identical 16-byte names
} entry_t;

typedef struct {
  tick_t level_since;
  tick_t next;
  uint8_t key;
  uint8_t level;
} browser_repeat_t;


static tick_t lcd_timeout;
static bool lcd_timer;
static uint16_t lcd_current_screen;
static struct {
  buffer_t *blocks;
  uint8_t *first_data; // Directory-handle metadata leaves its data area free
  uint16_t capacity, count;
  uint32_t first, total;
  bool sorted;
} browser_page;

static entry_t *browser_entry(uint16_t index) {
  if (index < ENTRIES_PER_BLOCK)
    return (entry_t *)(browser_page.first_data + index * sizeof(entry_t));
  index -= ENTRIES_PER_BLOCK;
  buffer_t *block = browser_page.blocks;
  while (index >= ENTRIES_PER_BLOCK) {
    index -= ENTRIES_PER_BLOCK;
    block = block->pvt.buffer.next;
  }
  return (entry_t *)(block->data + index * sizeof(entry_t));
}

#if CONFIG_HARDWARE_VARIANT == HW_PETSDPLUS && LCD_COLS >= 8 && LCD_LINES >= 4
/* "SD", 20x16 pixels in a 4x2 arrangement: slots 0..3 above 4..7.
 * "pet" on the baseline and the superscript plus use normal ROM glyphs.
 */
static const PROGMEM uint8_t petsd_logo[8][8] = {
  {0x00, 0x0f, 0x0f, 0x18, 0x18, 0x18, 0x18, 0x0f},
  {0x00, 0x1c, 0x1c, 0x06, 0x06, 0x00, 0x00, 0x1c},
  {0x00, 0x1f, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18},
  {0x00, 0x18, 0x18, 0x0c, 0x0c, 0x06, 0x06, 0x06},
  {0x0f, 0x00, 0x00, 0x18, 0x18, 0x0f, 0x0f, 0x00},
  {0x1c, 0x06, 0x06, 0x06, 0x06, 0x1c, 0x1c, 0x00},
  {0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x1f, 0x00},
  {0x06, 0x06, 0x06, 0x0c, 0x0c, 0x18, 0x18, 0x00}
};
#endif


static inline uint8_t min(uint8_t a, uint8_t b) {
  if (a < b) return a;
  return b;
}


void lcd_update_device_addr(void) {
  if (lcd_current_screen == SCRN_STATUS) {
    lcd_home();
#ifdef CONFIG_MULTIDRIVE
    if (md_active) {
      /* all addresses in the 16 columns left of the bus name */
      uint8_t i, col = 0;
      for (i = 0; i < MD_MAX_DEVICES; i++) {
        if (md_dev[i].address) {
          lcd_printf("#%d ", md_dev[i].address);
          col += md_dev[i].address < 10 ? 3 : 4;
        }
      }
      while (col++ < 16)
        lcd_putc(' ');
      return;
    }
#endif
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
#if CONFIG_HARDWARE_VARIANT == HW_PETSDPLUS && LCD_COLS >= 8 && LCD_LINES >= 4
    lcd_load_custom_chars_P(&petsd_logo[0][0]);
    lcd_locate((LCD_COLS - 8) / 2 + 3, 0);
    for (uint8_t i = 0; i < 4; i++) lcd_putc(i);
    lcd_putc('+');
    lcd_locate((LCD_COLS - 8) / 2, 1);
    lcd_puts_P(PSTR("pet"));
    for (uint8_t i = 4; i < 8; i++) lcd_putc(i);
    size_t version_length = strlen_P(versionstr);
    if (version_length <= LCD_COLS)
      lcd_locate((LCD_COLS - version_length) / 2, LCD_LINES - 1);
    else
      lcd_locate(0, LCD_LINES - 2);
    lcd_puts_P(versionstr);
#else
    lcd_puts_P(versionstr);
    lcd_locate(0,3); lcd_puts_P(PSTR(HWNAME));
#endif
    // TODO: if available, print serial number here
    break;

  case SCRN_STATUS:
    lcd_locate(16, 0);
    if (active_bus == IEC)     lcd_puts_P(PSTR(" IEC"));
    if (active_bus == IEEE488) lcd_puts_P(PSTR("IEEE"));
    lcd_update_device_addr();
    lcd_update_disk_status();
    break;

  default:
    break;
  }
}


void lcd_refresh(void) {
  lcd_draw_screen(lcd_current_screen);
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


bool handle_buttons(void) {
  if (!menu_system_enabled) return false;
  uint8_t buttons = get_key_press(KEY_ANY);
  if (!buttons) return false;
  if (buttons & KEY_PREV) {
    // If there's an error, PREV clears the disk status
    // If not, enter menu system just as any other key
    if (current_error != ERROR_OK) {
      set_error(ERROR_OK);
      return false;
    }
  }
  return menu();
}


int8_t menu_vertical(uint8_t min, uint8_t max) {
  uint8_t pos = min;

  lcd_cursor(true);
  for (;;) {
    lcd_locate(0, pos);
    if (get_key_autorepeat(KEY_PREV)) {
      if (pos > min) --pos;
      else pos = max;
    }
    if (get_key_autorepeat(KEY_NEXT)) {
      if (pos < max) ++pos;
      else pos = min;
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
  lcd_clear();
  lcd_puts_P(PSTR("Save settings?\n\nyes\nno"));
  if (menu_vertical(2,3) == 2) {
    write_configuration();
  }
}

#ifdef HAVE_DUAL_INTERFACE
void menu_select_bus(void) {
  lcd_clear();
  lcd_puts_P(PSTR("IEC\nIEEE-488"));
  active_bus = menu_vertical(0, 1);
  menu_ask_store_settings();
}
#else
static inline void menu_select_bus(void) {}
#endif


void menu_device_number(void) {
  lcd_printf("Change device number\nfrom %02d to:", device_address);
  lcd_locate(12, 1);
  device_address = menu_edit_value(device_address, 8, 30);
  menu_ask_store_settings();
}


#ifdef HAVE_RTC
static const PROGMEM uint8_t menu_setclk_pos[] = {0, 3, 9, 12, 15, 18, 0, 8};
#endif
static const PROGMEM uint8_t monthnames[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";
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
    set_busy_led(true);
    lcd_cursor(true);
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
      if (get_key_press(KEY_SEL)) {
        set_busy_led(false);
        lcd_cursor(false);
        return m;
      }
    }
  }
}


uint8_t calc_number_of_days(uint8_t month, uint8_t year) {
  uint8_t days = pgm_read_byte(&number_of_days[month]);
  if ((month == 1) && (year % 4 == 0)) ++days;
  return days;
}


void menu_set_clock(void) {
#ifdef HAVE_RTC
  struct tm t;
  uint8_t p;
  uint8_t days;


  switch (rtc_state) {
    case RTC_OK:
      read_rtc(&t);
      if (rtc_state == RTC_OK && t.tm_year <= RTC_MAX_YEAR)
        break;
      // Reading may invalidate the RTC, e.g. after a DS3231 century rollover.
      // fall through

    case RTC_INVALID:
      memset(&t, 0, sizeof(t));
      t.tm_mday = 1;
      t.tm_year = 115;
      break;

    case RTC_NOT_FOUND:
    default:
      return;
  }

  lcd_printf("%02d-MMM-20%2d %02d:%02d:%02d",
      t.tm_mday, t.tm_year - 100, t.tm_hour, t.tm_min, t.tm_sec);
  lcd_locate(3, 0);
  menu_print_month(t.tm_mon);

  p = SETCLK_MDAY;
  for (;;) {
    if (p < SETCLK_SET) {
      lcd_locate(pgm_read_byte(&(menu_setclk_pos[p])), 0);
      switch (p++) {
        case SETCLK_MDAY:
          t.tm_mday = menu_edit_value(t.tm_mday, 1, 31);
          continue;
        case SETCLK_MON:
          t.tm_mon = menu_edit_month(t.tm_mon);
          days = calc_number_of_days(t.tm_mon, t.tm_year);
          if (t.tm_mon == 1) ++days; // Could be a leap year
          if (t.tm_mday > days) p = SETCLK_MDAY;
          continue;
        case SETCLK_YEAR:
          t.tm_year = menu_edit_value(t.tm_year - 100, 15, RTC_MAX_YEAR - 100) + 100;
          days = calc_number_of_days(t.tm_mon, t.tm_year);
          if (t.tm_mday > days) p = SETCLK_MDAY;
          continue;
        case SETCLK_HOUR:
          t.tm_hour = menu_edit_value(t.tm_hour, 0, 23);
          continue;
        case SETCLK_MIN:
          t.tm_min = menu_edit_value(t.tm_min, 0, 59);
          continue;
        case SETCLK_SEC:
          t.tm_sec = menu_edit_value(t.tm_sec, 0, 59);
          continue;
      }
    } else {
      lcd_locate(0,1);
      lcd_puts_P(PSTR("Write to RTC\nEdit again\nAbort"));
      uint8_t sel = menu_vertical(1,3);
      lcd_clrlines(1,3);
      switch (sel) {
        case 1:         // Set time
          t.tm_wday = day_of_week(t.tm_year, t.tm_mon + 1, t.tm_mday);
          set_rtc(&t);

        // fall through

        case 3:         // Abort
          return;

        default:        // Edit date & time
          p = SETCLK_MDAY;
      }
    }
  }
#else
  return;               // no RTC
#endif
}

void lcd_print_dir_entry(uint16_t i) {
  char filename[16 + 1];
  entry_t *entry = browser_entry(i);

  if      (entry->flags & E_DIR)   lcd_puts_P(PSTR("DIR "));
  else if (entry->flags & E_IMAGE) lcd_puts_P(PSTR("IMG "));
  else lcd_printf("%3u ", entry->filesize);
  memset (filename, 0, sizeof(filename));
  ustrncpy(filename, entry->filename,
           (LCD_COLS - 4) > 16 ? 16 : LCD_COLS - 4);
  pet2asc((uint8_t *) filename);
  lcd_puts(filename);
}

void clear_command_buffer(void) {
  memset(command_buffer, 0, sizeof(command_buffer));
  command_length = 0;
}

static int browser_compare(const entry_t *a, const entry_t *b) {

  // 1st: directories alphabetically
  // 2nd: image files alphabetically
  // 3rd: file names alphabetically

  if (!browser_page.sorted) goto ordinal;
  bool a_is_dir = (a->flags & E_DIR);
  bool b_is_dir = (b->flags & E_DIR);
  if (a_is_dir && !b_is_dir) return -1;
  else if (!a_is_dir && b_is_dir) return 1;

  bool a_is_img = (a->flags & E_IMAGE);
  bool b_is_img = (b->flags & E_IMAGE);
  if (a_is_img && !b_is_img) return -1;
  else if (!a_is_img && b_is_img) return 1;

  int result = strncmp((const char *)a->filename,
                       (const char *)b->filename, sizeof(a->filename));
  if (result) return result;
ordinal:
  return a->ordinal < b->ordinal ? -1 : a->ordinal > b->ordinal;
}

static void browser_free_cache(void) {
  buffer_t *block = browser_page.blocks;
  while (block) {
    buffer_t *next = block->pvt.buffer.next;
    free_buffer(block);
    block = next;
  }
  memset(&browser_page, 0, sizeof(browser_page));
}

static uint8_t browser_allocate_cache(buffer_t *buf) {
  browser_page.first_data = buf->data;
  browser_page.capacity = ENTRIES_PER_BLOCK;
  buffer_t **tail = &browser_page.blocks;
  for (uint8_t i = 0; i < CONFIG_BUFFER_COUNT; i++) {
    if (buffers[i].allocated) continue;
    buffer_t *block = alloc_system_buffer();
    if (!block) return 1;
    *tail = block;
    block->pvt.buffer.next = NULL;
    tail = &block->pvt.buffer.next;
    browser_page.capacity += ENTRIES_PER_BLOCK;
  }
  return 0;
}

static void browser_swap(uint16_t a, uint16_t b) {
  entry_t *pa = browser_entry(a), *pb = browser_entry(b);
  entry_t tmp = *pa;
  *pa = *pb;
  *pb = tmp;
}

/* A max-heap retains the smallest entries for forward paging; a min-heap
 * retains the largest for backward paging. No pointer table is required. */
static int browser_heap_compare(uint16_t a, uint16_t b, bool reverse) {
  int result = browser_compare(browser_entry(a), browser_entry(b));
  return reverse ? -result : result;
}

static void browser_sift_down(uint16_t root, uint16_t count, bool reverse) {
  for (;;) {
    uint16_t child = root * 2 + 1;
    if (child >= count) return;
    if (child + 1 < count && browser_heap_compare(child, child + 1, reverse) < 0)
      child++;
    if (browser_heap_compare(root, child, reverse) >= 0) return;
    browser_swap(root, child);
    root = child;
  }
}

/* Scan the directory and retain the adjacent sorted window. The anchor is
 * copied by the caller before replacing the cache. Each file's ordinal makes
 * even duplicate display names unambiguous. Native image order is retained.
 * Return 1 only for real I/O errors, never merely because the cache is full. */
static uint8_t browser_load_page(path_t *path, buffer_t *buf,
                                 const entry_t *anchor, bool reverse) {
  cbmdirent_t dent;
  uint32_t eligible = 0;
  browser_page.count = 0;
  browser_page.total = 0;
  if (opendir(&buf->pvt.dir.dh, path)) return 1;
  for (;;) {
    int8_t result = next_match(&buf->pvt.dir.dh, NULL, NULL, NULL, 0, &dent);
    if (result < 0) break;
    if (result > 0) return 1;
    if (browser_page.total == UINT32_MAX - DIRNAV_OFFSET) {
      set_error(ERROR_DIR_ERROR);
      return 1;
    }
    if (!browser_page.total) browser_page.sorted = dent.opstype == OPSTYPE_FAT;
    entry_t entry;
    entry.ordinal = browser_page.total++;
    entry.flags = 0;
    if (dent.opstype == OPSTYPE_FAT &&
        check_imageext(dent.pvt.fat.realname) != IMG_UNKNOWN)
      entry.flags |= E_IMAGE;
    if ((dent.typeflags & EXT_TYPE_MASK) == TYPE_DIR) entry.flags |= E_DIR;
    ustrncpy(entry.filename, dent.name, sizeof(entry.filename));
    entry.filesize = dent.blocksize;
    if (anchor) {
      int order = browser_compare(&entry, anchor);
      if (reverse ? order >= 0 : order <= 0) continue;
    }
    eligible++;
    if (browser_page.count < browser_page.capacity) {
      uint16_t child = browser_page.count++;
      *browser_entry(child) = entry;
      while (child) {
        uint16_t parent = (child - 1) / 2;
        if (browser_heap_compare(parent, child, reverse) >= 0) break;
        browser_swap(parent, child);
        child = parent;
      }
    } else {
      int order = browser_compare(&entry, browser_entry(0));
      if (reverse ? order <= 0 : order >= 0) continue;
      *browser_entry(0) = entry;
      browser_sift_down(0, browser_page.count, reverse);
    }
  }
  browser_page.first = reverse ? eligible - browser_page.count :
                                browser_page.total - eligible;
  for (uint16_t count = browser_page.count; count > 1; count--) {
    browser_swap(0, count - 1);
    browser_sift_down(0, count - 1, reverse);
  }
  if (reverse) {
    for (uint16_t i = 0; i < browser_page.count / 2; i++)
      browser_swap(i, browser_page.count - 1 - i);
  }
  set_busy_led(false); set_dirty_led(true);
  return 0;
}

static uint8_t browser_seek_position(path_t *path, buffer_t *buf, uint32_t *mp) {
  for (;;) {
    if (*mp >= browser_page.total + DIRNAV_OFFSET)
      *mp = browser_page.total + DIRNAV_OFFSET - 1;
    if (*mp < DIRNAV_OFFSET && !browser_page.first) return 0;
    uint32_t rank = *mp < DIRNAV_OFFSET ? 0 : *mp - DIRNAV_OFFSET;
    if (browser_page.count && rank >= browser_page.first &&
        rank - browser_page.first < browser_page.count) return 0;

    lcd_clear();
    lcd_puts_P(PSTR("Reading..."));
    uint32_t old_first = browser_page.first;
    bool reverse = rank < browser_page.first;
    bool edge = !rank || rank == browser_page.total - 1 || !browser_page.count;
    entry_t anchor;
    if (!edge)
      anchor = *browser_entry(reverse ? 0 : browser_page.count - 1);
    if (browser_load_page(path, buf, edge ? NULL : &anchor, edge ? rank != 0 : reverse))
      return 1;
    if (!browser_page.count) {
      if (browser_page.total && browser_load_page(path, buf, NULL, false)) return 1;
      *mp = 0;
      return 0;
    }
    /* A changed directory must not cause an endless rescan. */
    if (!edge && (reverse ? browser_page.first >= old_first :
                           browser_page.first <= old_first)) {
      *mp = browser_page.first + DIRNAV_OFFSET;
      return 0;
    }
  }
}

/* Local to the browser: other menus retain the normal key-repeat behavior.
 * Initial repeat delay 500 ms; intervals 150, 80 and 40 ms. Each level
 * lasts two seconds of continuously holding the same direction.
 */
static uint8_t browser_repeat_update(browser_repeat_t *repeat, uint8_t held,
                                     uint8_t pressed, tick_t now) {
  const uint8_t mask = KEY_PREV | KEY_NEXT;
  held &= mask;
  pressed &= mask;
  if (held == mask || pressed == mask) {
    repeat->key = 0;
    return 0;
  }
  if ((pressed && (!held || pressed == held)) ||
      (held && held != repeat->key)) {
    repeat->key = held;
    repeat->level = 0;
    repeat->level_since = now;
    repeat->next = now + MS_TO_TICKS(500);
    return pressed && !held ? pressed : held;
  }
  if (!held) {
    repeat->key = 0;
    return 0;
  }
  while (repeat->level < 2 &&
         (tick_t)(now - repeat->level_since) >= MS_TO_TICKS(2000)) {
    repeat->level++;
    repeat->level_since += MS_TO_TICKS(2000);
  }
  if (time_before(now, repeat->next)) return 0;
  uint8_t interval = repeat->level == 0 ? MS_TO_TICKS(150) :
                     repeat->level == 1 ? MS_TO_TICKS(80) : MS_TO_TICKS(40);
  /* Schedule from now; slow LCD redraws must not produce a burst of moves. */
  repeat->next = now + interval;
  return held;
}

static void rom_menu_browse(uint8_t y) {
  switch (y) {
    case 0: lcd_puts_P(PSTR("Return to main menu\n")); break;
    case 1: lcd_puts_P(PSTR("Change to parent dir\n")); break;
    default: printf("Internal error: rom_menu_browse(%d)\r\n", y);
  }
}


static void rom_menu_main(uint8_t y) {
  switch (y) {
    case 0: lcd_puts_P(PSTR("Exit menu")); break;
    case 1: lcd_puts_P(PSTR("Browse files")); break;
#ifdef CONFIG_MULTIDRIVE
    case 2: lcd_puts_P(PSTR("Configure drives")); break;
#else
    case 2: lcd_puts_P(PSTR("Change device number")); break;
#endif
    case 3:
      if (rtc_state == RTC_NOT_FOUND)
        lcd_printf("Clock not found");
      else
        lcd_printf("Set clock");
      break;
    case 4: lcd_puts_P(PSTR("Select IEC/IEEE-488")); break;
    case 5: lcd_puts_P(PSTR("Adjust LCD contrast")); break;
    case 6: lcd_puts_P(PSTR("Adjust brightness")); break;
    default: break;
  }
}


/* Return true when selecting a regular file should close the entire menu. */
bool menu_browse_files(void) {
  bool exit_menu = false;
  browser_repeat_t repeat = {0};
  buffer_t *buf;
  path_t path;
  uint32_t entries;
  uint8_t i;
  uint8_t my;
  uint32_t mp;
  bool action;
  uint32_t stack_mp[MAX_LASTPOS];
  uint8_t stack_my[MAX_LASTPOS];
  uint8_t pos_stack;

  pos_stack = 0;
  memset(stack_mp, 0, sizeof(stack_mp));
  memset(stack_my, 0, sizeof(stack_my));

start:
  repeat.key = 0;
  lcd_clear();
  lcd_puts_P(PSTR("Reading..."));

  buf = NULL;
  mp = 0;
  if ((buf = alloc_system_buffer()) == NULL) goto cleanup;
  if (browser_allocate_cache(buf)) goto cleanup;
  path.part = current_part;
  path.dir = partition[path.part].current_dir;
  if (browser_load_page(&path, buf, NULL, false)) goto cleanup;
  entries = browser_page.total;

  mp = stack_mp[pos_stack];
  my = stack_my[pos_stack];
  printf("mp set to %lu\r\n", (unsigned long)mp);
  if (mp < (LCD_LINES - 1))
    my = mp;
  else
    my = 0;
  action = false;

  for (i=0; i < MAX_LASTPOS; i++) {
    if (pos_stack == i) uart_putc('>');
    printf("%lu ", (unsigned long)stack_mp[i]);
  }
  uart_putcrlf();

  for (;;) {
    if (browser_seek_position(&path, buf, &mp)) goto cleanup;
    entries = browser_page.total;
    uint32_t first = browser_page.first ? browser_page.first + DIRNAV_OFFSET : 0;
    if (mp - first < my) my = mp - first;
    lcd_clear();
    for (i = 0; i < LCD_LINES; i++) {
      lcd_locate(0, i);
      uint32_t y = mp - my + i;
      if (y < DIRNAV_OFFSET) {
        rom_menu_browse(y);
      } else {
        y -= DIRNAV_OFFSET;
        if (y >= entries) {
          lcd_puts_P(PSTR("-- End of dir --"));
          break;
        } else if (y < browser_page.first ||
                   y - browser_page.first >= browser_page.count) {
          lcd_puts_P(PSTR("-- More files --"));
          break;
        } else {
          lcd_print_dir_entry(y - browser_page.first);
        }
      }
    }

    lcd_cursor(true);
    action = false;
    for (;;) {
      lcd_locate(0, my);
      //printf("mp: %u   my: %u\r\n", mp, my);
      //while (!get_key_state(KEY_ANY));
      uint8_t pressed;
      uint8_t held = get_key_state_and_press(KEY_PREV | KEY_NEXT, &pressed);
      (void)get_key_rpt(KEY_PREV | KEY_NEXT);
      uint8_t key = browser_repeat_update(&repeat, held, pressed, getticks());
      if (key == KEY_PREV) {
        if (mp > 0) {
          --mp;
          if (browser_page.first && mp < browser_page.first + DIRNAV_OFFSET) {
            my = LCD_LINES - 1;
            break;
          }
          if (my > 0) --my;
          else {
            my = LCD_LINES - 1;
            if (mp < LCD_LINES) {
              while ((mp - my) >= DIRNAV_OFFSET) {
                --my;
                // TODO: is this really necessary?
                uart_puts_P(PSTR("my fixed\r\n"));
              }
            }
            break;
          }
        } else {
          my = LCD_LINES - 2;
          mp = entries + DIRNAV_OFFSET - 1;
          break;
        }
      }
      if (key == KEY_NEXT) {
        if (mp < (entries -1 + DIRNAV_OFFSET)) {
          ++mp;
          if (mp >= browser_page.first + browser_page.count + DIRNAV_OFFSET) {
            my = 0;
            break;
          }
          if (my < (LCD_LINES - 1))  ++my;
          else {
            my = 0;
            break;
          }
        } else {
          mp = 0;
          my = 0;
          break;
        }
      }
      if (get_key_press(KEY_SEL)) {
        action = true;
        break;
      }
    }
    lcd_cursor(false);
    if (!action) continue;
    if (mp == NAV_ABORT) goto cleanup;
    if (mp == NAV_PARENT) {
      uart_puts_P(PSTR("CD_\r\n"));
      ustrcpy_P(command_buffer, PSTR("CD_"));
      command_length = 3;
      browser_free_cache();
      free_buffer(buf); buf = NULL;
      parse_doscommand();
      clear_command_buffer();
      stack_mp[pos_stack] = 0;
      stack_my[pos_stack] = 0;
      if (pos_stack > 0) --pos_stack;
      printf("pos_stack set to %d\r\n", pos_stack);
      if (current_error != ERROR_OK) goto cleanup;
      goto reread;
    }
    entry_t *selected = browser_entry(mp - DIRNAV_OFFSET - browser_page.first);
    if (selected->flags & (E_DIR | E_IMAGE))
    {
      // The current directory also occupies a slot in the position stack.
      if (pos_stack >= MAX_LASTPOS - 1) {
        lcd_clear();
        lcd_puts_P(PSTR("Maximum menu depth\nreached"));
        wait_anykey();
        action = false;
        continue;
      }
      clear_command_buffer();
      ustrcpy_P(command_buffer, PSTR("CD:"));
      memcpy(command_buffer + 3, selected->filename, sizeof(selected->filename));
      command_length = ustrlen(command_buffer);
      browser_free_cache();
      free_buffer(buf); buf = NULL;
      parse_doscommand();
      clear_command_buffer();
      if (current_error != ERROR_OK) goto cleanup;
      stack_mp[pos_stack]   = mp;
      stack_my[pos_stack++] = my;
      printf("pos_stack set to %d\r\n", pos_stack);
      goto reread;
    }
    exit_menu = true;
    goto cleanup;
  }

reread:
  free_buffer(buf);
  browser_free_cache();
  set_busy_led(false); set_dirty_led(true);
  if (mp == NAV_ABORT) return exit_menu;
  goto start;

cleanup:
  mp = NAV_ABORT;
  goto reread;
}

/* Main menu entry. The fastloader hook calls menu_browse_files directly */
/* and stays on the drive the loader is using.                           */
static bool browse_selected_drive(void) {
#ifdef CONFIG_MULTIDRIVE
  /* Several drives: ask which one, browse there, then switch back */
  uint8_t saved_dev  = md_cur;
  uint8_t saved_part = current_part;

  if (md_active) {
    bool exit_menu;

    if (!md_browse_select())
      return false;
    exit_menu = menu_browse_files();
    md_select(saved_dev);
    current_part = saved_part;
    return exit_menu;
  }
#endif
  return menu_browse_files();
}

#ifdef HAVE_DUAL_INTERFACE
#define MAIN_MENU_LAST_ENTRY 6
#else
#define MAIN_MENU_LAST_ENTRY 5
#endif

bool menu(void) {
  uint8_t mp = 0;
  uint8_t my = 0;
  uint8_t i;
  uint8_t old_bus;
  bool action;

  old_bus = active_bus;
  bus_sleep(true);

  menu_select_status(); // disable splash screen if still active
  set_error(ERROR_OK);
  for (;;) {
    set_busy_led(false); set_dirty_led(true);
    lcd_clear();
    for (i = 0; i < LCD_LINES; i++) {
      lcd_locate(0, i);
      rom_menu_main(mp - my + i);
    }

    lcd_cursor(true);
    for (;;) {
      action = false;
      lcd_locate(0, my);
      //printf("mp: %u   my: %u\r\n", mp, my);
      //while (!get_key_state(KEY_ANY));
      if (get_key_autorepeat(KEY_PREV)) {
        if (mp > 0) {
          // Move up
          --mp;
          if (my > 0) {
            --my;               // Move within same page
          } else {
            mp = LCD_LINES - 1;
            my = LCD_LINES - 1; // page up
            break;
          }
        } else {
          // flip down to last menu entry
          my = MAIN_MENU_LAST_ENTRY % LCD_LINES;
          mp = MAIN_MENU_LAST_ENTRY;
          break;
        }
      }
      if (get_key_autorepeat(KEY_NEXT)) {
        if (mp < MAIN_MENU_LAST_ENTRY) {
          ++mp;
          if (my < (LCD_LINES - 1)) {
            ++my;               // Move within same page
          } else {
            my = 0;             // page down
            break;
          }
        } else {
          // flip up to first menu entry
          mp = 0;
          my = 0;
          break;
        }
      }
      if (get_key_press(KEY_SEL)) {
        action = true;
        break;
      }
    }
    lcd_cursor(false);
    if (!action) continue;
    lcd_clear();
    if      (mp == 1) {
      if (browse_selected_drive()) break;
    }
#ifdef CONFIG_MULTIDRIVE
    else if (mp == 2) menu_drives();
#else
    else if (mp == 2) menu_device_number();
#endif
    else if (mp == 3) menu_set_clock();
    else if (mp == 4) menu_select_bus();
    else if (mp == 5) menu_adjust_contrast();
    else if (mp == 6) menu_adjust_brightness();
    else  break;
    if (current_error != ERROR_OK) break;
  }

  bus_sleep(false);
  lcd_draw_screen(SCRN_STATUS);
  update_leds();
  return old_bus != active_bus;
}

static void pwm_error(uint8_t error) {
  lcd_locate(0, LCD_LINES - 2);
  lcd_puts_P(PSTR("PWM controller error\n"));
  switch (error) {
    case I2C_ERROR_ADDRESS_NACK:  lcd_puts_P(PSTR("I2C address NACK")); break;
    case I2C_ERROR_REGISTER_NACK: lcd_puts_P(PSTR("I2C register NACK")); break;
    case I2C_ERROR_DATA_NACK:     lcd_puts_P(PSTR("I2C data NACK")); break;
    case I2C_ERROR_SCL_TIMEOUT:   lcd_puts_P(PSTR("I2C SCL timeout")); break;
    case I2C_ERROR_SDA_STUCK:     lcd_puts_P(PSTR("I2C SDA held low")); break;
    default: lcd_puts_P(PSTR("I2C transfer failed")); break;
  }
  wait_anykey();
}

void menu_adjust_contrast(void) {
  uint8_t i;
  uint8_t min = 0;
  uint8_t max = LCD_COLS - 2;
  uint8_t res;

  lcd_clear();
  lcd_puts_P(PSTR("Adjust LCD contrast"));
  lcd_locate(0, 1);

  lcd_cursor(false);
  set_busy_led(true);
  for (;;) {
    lcd_locate(0, 1);
    lcd_putc('[');
    for (i = 0; i < LCD_COLS - 2; i++) {
      lcd_putc(i >= lcd_contrast ? ' ' : 0xFF);
    }
    lcd_putc(']');
    res = lcd_set_contrast(lcd_contrast);
    if (res) break;
    for (;;) {
      if (get_key_autorepeat(KEY_PREV)) {
        if (lcd_contrast <= min) lcd_contrast = max;
        else --lcd_contrast;
        break;
      }
      if (get_key_autorepeat(KEY_NEXT)) {
        if (lcd_contrast >= max) lcd_contrast = min;
        else ++lcd_contrast;
        break;
      }
      if (get_key_press(KEY_SEL)) {
        lcd_cursor(false);
        set_busy_led(false);
        menu_ask_store_settings();
        return;
      }
    }
  }
  pwm_error(res);
}


void menu_adjust_brightness(void) {
  uint8_t i;
  uint8_t min = 0;
  uint8_t max = 255;
  uint8_t res;
  uint8_t step;

  lcd_clear();
  lcd_puts_P(PSTR("Adjust brightness"));
  lcd_locate(0, 1);

  lcd_cursor(false);
  set_busy_led(true);
  for (;;) {
    lcd_locate(0, 1);
    lcd_putc('[');
    for (i = 0; i < 18; i++) {
      lcd_putc(i >= (lcd_brightness / 14) ? ' ' : 0xFF);
    }
    lcd_putc(']');
    res = lcd_set_brightness(lcd_brightness);
    if (res) break;
    for (;;) {
      step = 10;
      if (lcd_brightness < 20 || lcd_brightness > 235) step = 1;
      if (get_key_autorepeat(KEY_PREV)) {
        if (lcd_brightness <= min) lcd_brightness = max;
        else lcd_brightness -= step;
        break;
      }
      if (get_key_autorepeat(KEY_NEXT)) {
        if (lcd_brightness >= max) lcd_brightness = min;
        else lcd_brightness += step;
        break;
      }
      if (get_key_press(KEY_SEL)) {
        lcd_cursor(false);
        set_busy_led(false);
        menu_ask_store_settings();
        return;
      }
    }
  }
  pwm_error(res);
}
