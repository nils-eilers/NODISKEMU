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
#include "uart.h"
#include "dirent.h"
#include "parser.h"     // current_part
#include "wrapops.h"
#include "fatops.h"     // pet2ascn()
#include "doscmd.h"


uint8_t menu_system_enabled = true;

#ifndef CONFIG_DIR_BUFFERS
#define CONFIG_DIR_BUFFERS 2
#endif

#define MAX_LASTPOS 16
#define ENTRIES_PER_BLOCK (256 / sizeof(entry_t))
#define E_DIR   1
#define E_IMAGE 2

typedef struct {
  uint8_t  flags;
  uint8_t  filename[16];
  uint16_t filesize;
} entry_t;


static tick_t lcd_timeout;
static bool lcd_timer;
static uint16_t lcd_current_screen;
static entry_t *ep[CONFIG_DIR_BUFFERS * ENTRIES_PER_BLOCK];


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
  struct tm t;
  uint8_t p;
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
          t.tm_year = menu_edit_value(t.tm_year - 100, 15, 99) + 100;
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
}

void lcd_print_dir_entry(uint16_t i) {
  char filename[16 + 1];

  if      (ep[i]->flags & E_DIR)   lcd_puts_P(PSTR("DIR "));
  else if (ep[i]->flags & E_IMAGE) lcd_puts_P(PSTR("IMG "));
  else lcd_printf("%3u ", ep[i]->filesize);
  memset (filename, 0, sizeof(filename));
  ustrncpy(filename, ep[i]->filename,
           (LCD_COLS - 4) > 16 ? 16 : LCD_COLS - 4);
  pet2asc((uint8_t *) filename);
  lcd_puts(filename);
}

void clear_command_buffer(void) {
  memset(command_buffer, 0, sizeof(command_buffer));
  command_length = 0;
}

int compare(const void *p1, const void *p2) {
  const entry_t *const *a = p1;
  const entry_t *const *b = p2;

  // 1st: directories alphabetically
  // 2nd: image files alphabetically
  // 3rd: file names alphabetically

  bool a_is_dir = ((*a)->flags & E_DIR);
  bool b_is_dir = ((*b)->flags & E_DIR);
  if (a_is_dir && !b_is_dir) return -1;
  else if (!a_is_dir && b_is_dir) return 1;

  bool a_is_img = ((*a)->flags & E_IMAGE);
  bool b_is_img = ((*b)->flags & E_IMAGE);
  if (a_is_img && !b_is_img) return -1;
  else if (!a_is_img && b_is_img) return 1;

  return ustrcmp((*a)->filename, (*b)->filename);
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
    case 2: lcd_puts_P(PSTR("Change device number")); break;
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


void menu_browse_files(void) {
  buffer_t *buf;
  buffer_t *buf_tbl;
  buffer_t *buf_cur;
  buffer_t *first_buf;
  path_t path;
  cbmdirent_t dent;
  uint16_t entries;
  uint8_t entry_num;
  bool fat_filesystem;
  uint8_t i;
  uint8_t my;
  uint16_t mp;
  bool action;
  uint16_t stack_mp[MAX_LASTPOS];
  uint8_t stack_my[MAX_LASTPOS];
  uint8_t pos_stack;
  uint8_t save_active_buffers;

  pos_stack = 0;
  memset(stack_mp, 0, sizeof(stack_mp));
  memset(stack_my, 0, sizeof(stack_my));
  save_active_buffers = active_buffers;

start:
  lcd_clear();
  lcd_puts_P(PSTR("Reading..."));

  buf = buf_tbl = buf_cur = first_buf = NULL;
  entries = entry_num = mp = 0;
  fat_filesystem = false;

  // Allocate one buffer, used to read a single directory entry
  if ((buf = alloc_system_buffer()) == NULL) return;

  // Allocate buffers with continuous data segments
  // Stores pointers to directory entries for qsort
  if ((buf_tbl = alloc_linked_buffers(CONFIG_DIR_BUFFERS)) == NULL) return;

  // Buffers to store the actual diretory entries.
  // Whilst the directory grows, new buffers get allocated and linked
  if ((buf_cur = alloc_system_buffer()) == NULL) return;
  first_buf = buf_cur;

  // Allocating buffers affects the LEDs
  set_busy_led(false); set_dirty_led(true);

  path.part = current_part;
  path.dir  = partition[path.part].current_dir;
  uart_trace(&path.dir, 0, sizeof(dir_t));
  uart_putcrlf();
  uart_flush();
  if (opendir(&buf->pvt.dir.dh, &path)) return;

  for (;;) {
    if (next_match(&buf->pvt.dir.dh,
                    buf->pvt.dir.matchstr,
                    buf->pvt.dir.match_start,
                    buf->pvt.dir.match_end,
                    buf->pvt.dir.filetype,
                    &dent) == 0)
    {
      uint8_t e_flags = 0;
      if (dent.opstype == OPSTYPE_FAT) {
        fat_filesystem = true;
        if (check_imageext(dent.pvt.fat.realname) != IMG_UNKNOWN)
          e_flags |= E_IMAGE;
      }
      // Current block full?
      if (entry_num == ENTRIES_PER_BLOCK) {
        entry_num = 0;
        buffer_t *old_buf = buf_cur;
        if ((buf_cur = alloc_system_buffer()) == NULL) {
          printf("alloc buf_cur failed, %d entries", entries);
          goto cleanup;
        }
        set_busy_led(false); set_dirty_led(true);
        buf_cur->pvt.buffer.next = NULL;
        old_buf->pvt.buffer.next = buf_cur;
      }

      // Store entry
      ep[entries] = (entry_t *) (buf_cur->data + entry_num * sizeof(entry_t));
      ustrncpy(ep[entries]->filename, dent.name, 16);
      ep[entries]->filesize = dent.blocksize;
      if ((dent.typeflags & EXT_TYPE_MASK) == TYPE_DIR)
        e_flags |= E_DIR;
      ep[entries]->flags = e_flags;
      entries++;
      entry_num++;
    } else {
      // No more directory entries to read
      break;
    }
  }
  printf("%d entries\r\n", entries);

  if (fat_filesystem)
    qsort(ep, entries, sizeof(entry_t*), compare);

  for (uint16_t i = 0; i < entries; i++) {
    printf("%3u: ", i);
    if (ep[i]->flags & E_DIR)
      uart_puts_P(PSTR(" DIR "));
    else if (ep[i]->flags & E_IMAGE)
      uart_puts_P(PSTR(" IMG "));
    else
      printf("%4u ", ep[i]->filesize);
    uint8_t filename[16 + 1];
    ustrncpy(filename, ep[i]->filename, 16);
    filename[16] = '\0';
    pet2asc(filename);
    printf("%s\r\n", filename);
    uart_flush();
  }
  uart_putcrlf();

#define DIRNAV_OFFSET   2
#define NAV_ABORT       0
#define NAV_PARENT      1

  mp = stack_mp[pos_stack];
  my = stack_my[pos_stack];
  printf("mp set to %d\r\n", mp);
  if (mp < (LCD_LINES - 1))
    my = mp;
  else
    my = 0;
  action = false;

  for (i=0; i < MAX_LASTPOS; i++) {
    if (pos_stack == i) uart_putc('>');
    printf("%d ", stack_mp[i]);
  }
  uart_putcrlf();

  for (;;) {
    lcd_clear();
    for (i = 0; i < LCD_LINES; i++) {
      lcd_locate(0, i);
      int8_t y = mp - my + i;
      if (y < 0) return; // should not happen!
      if (y < DIRNAV_OFFSET) {
        rom_menu_browse(y);
      } else {
        y -= DIRNAV_OFFSET;
        if (y >= entries) {
          lcd_puts_P(PSTR("-- End of dir --"));
          break;
        } else {
          lcd_print_dir_entry(y);
        }
      }
    }

    lcd_cursor(true);
    for (;;) {
      lcd_locate(0, my);
      //printf("mp: %u   my: %u\r\n", mp, my);
      //while (!get_key_state(KEY_ANY));
      if (get_key_autorepeat(KEY_PREV)) {
        if (mp > 0) {
          --mp;
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
      if (get_key_autorepeat(KEY_NEXT)) {
        if (mp < (entries -1 + DIRNAV_OFFSET)) {
          ++mp;
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
      parse_doscommand();
      clear_command_buffer();
      stack_mp[pos_stack] = 0;
      stack_my[pos_stack] = 0;
      if (pos_stack > 0) --pos_stack;
      printf("pos_stack set to %d\r\n", pos_stack);
      if (current_error != ERROR_OK) goto cleanup;
      goto reread;
    }
    if (ep[mp - DIRNAV_OFFSET]->flags & E_DIR ||
        ep[mp - DIRNAV_OFFSET]->flags & E_IMAGE)
    {
      clear_command_buffer();
      ustrcpy_P(command_buffer, PSTR("CD:"));
      ustrncpy(command_buffer + 3, ep[mp - DIRNAV_OFFSET]->filename, 16);
      command_length = ustrlen(command_buffer);
      parse_doscommand();
      clear_command_buffer();
      if (current_error != ERROR_OK) goto cleanup;
      if (pos_stack < MAX_LASTPOS) {
        stack_mp[pos_stack]   = mp;
        stack_my[pos_stack++] = my;
        printf("pos_stack set to %d\r\n", pos_stack);
      }
      goto reread;
    }
  }

reread:
  free_buffer(buf);

  buffer_t *p = buf_tbl;
  do {
    p->allocated = 0;
    p = p->pvt.buffer.next;
  } while (p != NULL);

  p = first_buf;
  if (p != NULL) do {
    p->allocated = 0;
    p = p->pvt.buffer.next;
  } while (p != NULL);

  set_busy_led(false); set_dirty_led(true);
  active_buffers = save_active_buffers;
  if (mp == NAV_ABORT) return;
  goto start;

cleanup:
  mp = NAV_ABORT;
  goto reread;
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
    if      (mp == 1) menu_browse_files();
    else if (mp == 2) menu_device_number();
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

static void pwm_error(void) {
  lcd_locate(0, LCD_LINES - 2);
  lcd_puts_P(PSTR("Error:PWM controller\nnot found"));
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
  pwm_error();
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
  pwm_error();
}
