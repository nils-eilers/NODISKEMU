/* NODISKEMU - SD/MMC to IEEE-488 interface/controller

   NODISKEMU is a fork of sd2iec by Ingo Korb (et al.), http://sd2iec.de

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA


   menu-drives.c: The "Configure drives" menu of the multi-drive support

   Shows all devices and drives of PETSD.CFG, lets the user change
   addresses, add and remove drives and devices and writes the file
   line by line, keeping comments. Without a configuration file the
   single legacy device is shown; as long as nothing but its address
   changes, the address is stored in the EEPROM as before and no file
   is created.

   All working data lives in system buffers, the bus sleeps while the
   menu is open.
*/

#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "bus.h"
#include "eeprom-conf.h"
#include "fatops.h"
#include "ff.h"
#include "errormsg.h"
#include "filesystem.h"
#include "lcd.h"
#include "menu.h"
#include "multidrive.h"
#include "parser.h"
#include "timer.h"
#include "ustring.h"

/* from menu.c */
uint8_t menu_edit_value(uint8_t v, uint8_t min, uint8_t max);

#define PATH_BUFFERS  (MD_DRIVES * MD_MAX_DEVICES * MD_PATH_MAX / 256)
#define PATH_SLOTS    (MD_DRIVES * MD_MAX_DEVICES)
#define NONE          0xff

/* list rows after the drives */
#define ROW_ADD       0xfd
#define ROW_SAVE      0xfe
#define ROW_EXIT      0xff

/* entries of the drive submenu */
enum { SUB_BACK, SUB_CHOOSE, SUB_ADDRESS, SUB_ADD_DRIVE1, SUB_DEL_DRIVE1,
       SUB_DEL_DEVICE };

typedef struct {
  md_edit_t tab[MD_EDIT_MAX];
  uint8_t   count;                 /* used entries of tab              */
  uint8_t   rows[MD_EDIT_MAX + 3]; /* list row -> tab index or ROW_*   */
  uint8_t   nrows;
  uint8_t   sub[6];                /* submenu row -> SUB_*             */
  uint8_t   sub_entry;             /* tab index the submenu is about   */
  uint8_t   slots_used;            /* bit mask of used path slots      */
  uint8_t   had_lines;             /* PETSD.CFG had drive lines        */
  uint8_t   file_exists;
  buffer_t  *bufs[1 + PATH_BUFFERS];
} state_t;

static state_t *st;

/* ------------------------------------------------------------------ */
/*  helpers                                                           */
/* ------------------------------------------------------------------ */

static uint8_t *slot_ptr(uint8_t k) {
  return st->bufs[1 + k / 2]->data + (k % 2) * MD_PATH_MAX;
}

/* get storage for a path, NULL if all are used */
static uint8_t *alloc_path(void) {
  for (uint8_t k = 0; k < PATH_SLOTS; k++) {
    if (!(st->slots_used & (1 << k))) {
      st->slots_used |= 1 << k;
      return slot_ptr(k);
    }
  }
  return NULL;
}

static void free_path(uint8_t *p) {
  for (uint8_t k = 0; k < PATH_SLOTS; k++)
    if (slot_ptr(k) == p)
      st->slots_used &= ~(1 << k);
}

static uint8_t is_live(uint8_t i) {
  return !(st->tab[i].flags & MDE_REMOVED);
}

/* table index of a drive that is not removed, or NONE */
static uint8_t find_live(uint8_t address, uint8_t drive) {
  for (uint8_t i = 0; i < st->count; i++)
    if (is_live(i) && st->tab[i].address == address &&
        st->tab[i].drive == drive)
      return i;
  return NONE;
}

static uint8_t device_exists(uint8_t address) {
  return find_live(address, 0) != NONE || find_live(address, 1) != NONE;
}

static uint8_t count_devices(void) {
  uint8_t n = 0;

  for (uint8_t a = 4; a <= 30; a++)
    if (device_exists(a))
      n++;
  return n;
}

/* new table entry with path "/", NONE if the table is full */
static uint8_t add_entry(uint8_t address, uint8_t drive) {
  uint8_t i, *path;

  /* reuse an entry that was added and removed again */
  for (i = 0; i < st->count; i++)
    if (!is_live(i) && st->tab[i].file_address == 0)
      break;
  if (i == st->count) {
    if (st->count == MD_EDIT_MAX)
      return NONE;
    st->count++;
  }

  path = alloc_path();
  if (path == NULL)
    return NONE;
  path[0] = '/';
  path[1] = 0;

  st->tab[i].file_address = 0;
  st->tab[i].drive        = drive;
  st->tab[i].address      = address;
  st->tab[i].flags        = 0;
  st->tab[i].path         = path;
  return i;
}

static void remove_entry(uint8_t i) {
  st->tab[i].flags |= MDE_REMOVED;
  free_path(st->tab[i].path);
  st->tab[i].path = NULL;
}

/* wait for any key */
static void wait_key(void) {
  while (!get_key_press(KEY_ANY)) ;
}

/* print a path into the rest of the line, keeping its end */
static void put_path(uint8_t *path, uint8_t width) {
  uint8_t len = ustrlen(path);

  if (len > width) {
    lcd_puts_P(PSTR(".."));
    path += len - (width - 2);
  }
  lcd_puts((char *)path);
}

/**
 * choose - let the user pick one of several lines
 * @count: number of lines
 * @pos  : line the cursor starts on
 * @draw : prints line i at the current position
 *
 * PREV and NEXT move the cursor and scroll page by page like the main
 * menu, SEL returns the line.
 */
static uint8_t choose(uint8_t count, uint8_t pos, void (*draw)(uint8_t)) {
  uint8_t top = NONE, i;

  if (pos >= count)
    pos = 0;

  for (;;) {
    if (top != pos - pos % LCD_LINES) {
      top = pos - pos % LCD_LINES;
      lcd_cursor(false);
      lcd_clear();
      for (i = 0; i < LCD_LINES && top + i < count; i++) {
        lcd_locate(0, i);
        draw(top + i);
      }
    }
    lcd_locate(0, pos - top);
    lcd_cursor(true);

    for (;;) {
      if (get_key_autorepeat(KEY_PREV)) {
        pos = pos ? pos - 1 : count - 1;
        break;
      }
      if (get_key_autorepeat(KEY_NEXT)) {
        pos = (pos + 1 < count) ? pos + 1 : 0;
        break;
      }
      if (get_key_press(KEY_SEL)) {
        lcd_cursor(false);
        return pos;
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/*  list of drives                                                    */
/* ------------------------------------------------------------------ */

static void build_rows(void) {
  uint8_t n = 0, i;

  for (uint8_t a = 4; a <= 30; a++) {
    for (uint8_t d = 0; d < MD_DRIVES; d++) {
      i = find_live(a, d);
      if (i != NONE)
        st->rows[n++] = i;
    }
  }
  if (count_devices() < MD_MAX_DEVICES)
    st->rows[n++] = ROW_ADD;
  st->rows[n++] = ROW_SAVE;
  st->rows[n++] = ROW_EXIT;
  st->nrows = n;
}

static void draw_row(uint8_t r) {
  uint8_t i = st->rows[r];
  md_edit_t *e;

  switch (i) {
  case ROW_ADD:  lcd_puts_P(PSTR("Add device"));          return;
  case ROW_SAVE: lcd_puts_P(PSTR("Save and exit"));       return;
  case ROW_EXIT: lcd_puts_P(PSTR("Exit without saving")); return;
  }

  /* "#10 0:/SPIELE" or "    1:/SPIELE" below drive 0 */
  e = &st->tab[i];
  if (e->drive == 0 || find_live(e->address, 0) == NONE) {
    lcd_printf("#%d", e->address);
    if (e->address < 10)
      lcd_putc(' ');
    lcd_putc(' ');
  } else {
    lcd_puts_P(PSTR("    "));
  }
  lcd_printf("%d:", e->drive);
  put_path(e->path, LCD_COLS - 6);
}

/* ------------------------------------------------------------------ */
/*  actions                                                           */
/* ------------------------------------------------------------------ */

static void change_address(uint8_t address) {
  uint8_t v;

  lcd_clear();
  lcd_printf("Change address\nfrom %02d to:", address);
  lcd_locate(12, 1);
  v = menu_edit_value(address, 8, 30);
  if (v == address)
    return;

  if (device_exists(v)) {
    lcd_clear();
    lcd_printf("#%d is in use\n\nPress any key", v);
    wait_key();
    return;
  }

  for (uint8_t i = 0; i < st->count; i++)
    if (is_live(i) && st->tab[i].address == address)
      st->tab[i].address = v;
}

static void no_space(void) {
  lcd_clear();
  lcd_puts_P(PSTR("Too many drives\n\nPress any key"));
  wait_key();
}

static void add_device(void) {
  uint8_t a;

  for (a = 8; a <= 30; a++)
    if (!device_exists(a))
      break;
  if (a > 30 || add_entry(a, 0) == NONE)
    no_space();
}

/* ------------------------------------------------------------------ */
/*  choosing a directory or an image                                  */
/* ------------------------------------------------------------------ */

/* The picker reads the FAT volume directly, so the drives keep their */
/* current directories and images while the user looks around.       */

#define PICK_DEPTH    8               /* directory levels below the root */
#define PICK_FIXED    3               /* back, use, parent               */
#define ROW_P_BACK    0
#define ROW_P_USE     1
#define ROW_P_PARENT  2

typedef struct {
  uint8_t  path[MD_PATH_MAX];         /* ASCII path of the directory     */
  uint32_t parent[PICK_DEPTH];        /* clusters of the directories up  */
  uint32_t cluster;                   /* current directory, 0 = root     */
  uint8_t  depth;
  uint8_t  ndirs;                     /* directories shown               */
  uint8_t  nimgs;                     /* images shown                    */
  uint8_t  lfn[_MAX_LFN_LENGTH + 1];
} pick_t;

static pick_t *pk;

/**
 * pick_entry - find a directory or image in the current directory
 * @want_dir: 1 for the n-th directory, 0 for the n-th image
 * @n       : number of the entry, counting from 0, or 0xff to count
 * @name    : receives the name as used in PETSD.CFG, may be NULL
 * @cluster : receives the start cluster, may be NULL
 *
 * The name is the long name if it fits into 16 characters like the
 * computer shows it, otherwise the 8.3 name (e.g. ARCHON~1.D64).
 * Hidden entries (starting with a dot) are skipped. Returns the number
 * of matching entries passed when n is not found.
 */
static uint8_t pick_entry(uint8_t want_dir, uint8_t n, uint8_t *name,
                          uint32_t *cluster) {
  DIR     dir;
  FILINFO fi;
  uint8_t count = 0, *fname;

  fi.lfn = pk->lfn;
  if (l_opendir(&fatvol[0], pk->cluster, &dir) != FR_OK)
    return 0;

  while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
    if (fi.fattrib & AM_VOL)
      continue;
    fname = fi.fname;
    if (pk->lfn[0] && ustrlen(pk->lfn) <= CBM_NAME_LENGTH)
      fname = pk->lfn;
    if (fname[0] == '.')
      continue;
    if (want_dir != !!(fi.fattrib & AM_DIR))
      continue;
    if (!want_dir && check_imageext(fi.fname) == IMG_UNKNOWN)
      continue;

    if (count == n) {
      if (name)
        ustrcpy(name, fname);
      if (cluster)
        *cluster = fi.clust;
      return count;
    }
    if (count < 255 - PICK_FIXED - 1)
      count++;
  }
  return count;
}

static uint8_t pick_parent_rows(void) {
  return pk->depth ? 1 : 0;
}

static void pick_count(void) {
  pk->ndirs = pick_entry(1, 0xff, NULL, NULL);
  pk->nimgs = pick_entry(0, 0xff, NULL, NULL);
  if (pk->ndirs + pk->nimgs > 255 - PICK_FIXED)
    pk->nimgs = 255 - PICK_FIXED - pk->ndirs;
}

static void draw_pick(uint8_t r) {
  uint8_t name[CBM_NAME_LENGTH + 1];
  uint8_t fixed = ROW_P_PARENT + pick_parent_rows();

  if (r == ROW_P_BACK) {
    lcd_puts_P(PSTR("< Back"));
    return;
  }
  if (r == ROW_P_USE) {
    lcd_puts_P(PSTR("Use "));
    put_path(pk->path, LCD_COLS - 4);
    return;
  }
  if (r < fixed) {
    lcd_puts_P(PSTR(".. (parent dir)"));
    return;
  }

  r -= fixed;
  if (r < pk->ndirs) {
    lcd_puts_P(PSTR("DIR "));
    pick_entry(1, r, name, NULL);
  } else {
    lcd_puts_P(PSTR("IMG "));
    pick_entry(0, r - pk->ndirs, name, NULL);
  }
  name[CBM_NAME_LENGTH] = 0;
  lcd_puts((char *)name);
}

/* append a name to the path, 1 if it does not fit */
static uint8_t pick_append(uint8_t *path, uint8_t *name) {
  uint8_t len = ustrlen(path);

  if (len + 1 + ustrlen(name) > MD_PATH_LIMIT) {
    lcd_clear();
    lcd_puts_P(PSTR("Path too long\n\nPress any key"));
    wait_key();
    return 1;
  }
  if (len > 1)
    path[len++] = '/';
  ustrcpy(path + len, name);
  return 0;
}

/* take a path for drive entry e, an unchanged path keeps the line as is */
static void pick_take(md_edit_t *e, uint8_t *path) {
  if (ustrcmp(e->path, path) == 0)
    return;
  ustrcpy(e->path, path);
  e->flags |= MDE_NEWPATH;
}

/* returns 1 if a path was taken, 0 if the user went back */
static uint8_t pick_path(md_edit_t *e) {
  buffer_t *b;
  uint8_t  r = 0, fixed, name[CBM_NAME_LENGTH + 1];
  uint32_t cluster;
  uint8_t  *p;

  uint8_t  taken = 0;

  b = alloc_system_buffer();
  if (b == NULL) {
    set_error(ERROR_OK);
    lcd_clear();
    lcd_puts_P(PSTR("Not enough buffers\n\nPress any key"));
    wait_key();
    return 0;
  }
  pk = (pick_t *)b->data;
  memset(pk, 0, sizeof(pick_t));
  pk->path[0] = '/';
  pick_count();

  for (;;) {
    fixed = ROW_P_PARENT + pick_parent_rows();
    r = choose(fixed + pk->ndirs + pk->nimgs, r, draw_pick);

    if (r == ROW_P_BACK)
      break;

    if (r == ROW_P_USE) {
      pick_take(e, pk->path);
      taken = 1;
      break;
    }

    if (r < fixed) {
      /* one level up */
      pk->cluster = pk->parent[--pk->depth];
      p = pk->path + ustrlen(pk->path);
      while (p > pk->path + 1 && *--p != '/') ;
      *p = 0;
      if (pk->path[0] == 0) {
        pk->path[0] = '/';
        pk->path[1] = 0;
      }
      pick_count();
      r = 0;
      continue;
    }

    r -= fixed;
    if (r < pk->ndirs) {
      /* into a directory */
      pick_entry(1, r, name, &cluster);
      if (pk->depth == PICK_DEPTH) {
        lcd_clear();
        lcd_puts_P(PSTR("Too deep\n\nPress any key"));
        wait_key();
      } else if (!pick_append(pk->path, name)) {
        pk->parent[pk->depth++] = pk->cluster;
        pk->cluster = cluster;
        pick_count();
        r = 0;
        continue;
      }
      r += fixed;
    } else {
      /* an image */
      pick_entry(0, r - pk->ndirs, name, NULL);
      if (!pick_append(pk->path, name)) {
        pick_take(e, pk->path);
        taken = 1;
        break;
      }
      r += fixed;
    }
  }

  free_buffer(b);
  pk = NULL;
  return taken;
}

static void draw_sub(uint8_t r) {
  md_edit_t *e = &st->tab[st->sub_entry];

  switch (st->sub[r]) {
  case SUB_BACK:
    lcd_printf("< #%d drive %d", e->address, e->drive);
    break;
  case SUB_CHOOSE:     lcd_puts_P(PSTR("Choose dir/image")); break;
  case SUB_ADDRESS:    lcd_puts_P(PSTR("Change address")); break;
  case SUB_ADD_DRIVE1: lcd_puts_P(PSTR("Add drive 1"));    break;
  case SUB_DEL_DRIVE1: lcd_puts_P(PSTR("Remove drive 1")); break;
  case SUB_DEL_DEVICE: lcd_puts_P(PSTR("Remove device"));  break;
  }
}

static void drive_menu(uint8_t i) {
  md_edit_t *e = &st->tab[i];
  uint8_t n, d1, start = 0;

 again:
  n = 0;
  st->sub_entry = i;
  st->sub[n++] = SUB_BACK;
  st->sub[n++] = SUB_CHOOSE;
  st->sub[n++] = SUB_ADDRESS;
  d1 = find_live(e->address, 1);
  if (e->drive == 1)
    st->sub[n++] = SUB_DEL_DRIVE1;
  else if (d1 == NONE)
    st->sub[n++] = SUB_ADD_DRIVE1;
  if (e->drive == 0 && count_devices() > 1)
    st->sub[n++] = SUB_DEL_DEVICE;

  switch (st->sub[choose(n, start, draw_sub)]) {
  case SUB_CHOOSE:
    if (!pick_path(e)) {
      start = 1;        // back from the browser: this submenu again,
      goto again;       // cursor on "Choose dir/image"
    }
    break;

  case SUB_ADDRESS:
    change_address(e->address);
    break;

  case SUB_ADD_DRIVE1:
    if (add_entry(e->address, 1) == NONE)
      no_space();
    break;

  case SUB_DEL_DRIVE1:
    remove_entry(i);
    break;

  case SUB_DEL_DEVICE:
    if (d1 != NONE)
      remove_entry(d1);
    remove_entry(i);
    break;

  default:
    break;
  }
}

/* store the changes, returns 1 if the menu can be left */
static uint8_t save(void) {
  uint8_t i, live = 0, first = NONE;

  for (i = 0; i < st->count; i++) {
    if (is_live(i)) {
      live++;
      if (first == NONE)
        first = i;
    }
  }

  /* Still the plain single drive without a file: EEPROM as before */
  if (!st->had_lines && live == 1 && st->tab[first].drive == 0 &&
      st->tab[first].path[0] == '/' && st->tab[first].path[1] == 0) {
    device_address = st->tab[first].address;
    write_configuration();
    return 1;
  }

  set_error(ERROR_OK);
  if (md_edit_save(st->tab, st->count, 1)) {
    lcd_clear();
    lcd_puts_P(PSTR("Saving failed:\n"));
    lcd_puts((char *)error_buffer);
    wait_key();
    return 0;
  }
  return 2;
}

/* ------------------------------------------------------------------ */
/*  drive selection for "Browse files"                                */
/* ------------------------------------------------------------------ */

typedef struct {
  uint8_t n;
  uint8_t dev[MD_MAX_DEVICES * MD_DRIVES];
  uint8_t drive[MD_MAX_DEVICES * MD_DRIVES];
} drvlist_t;

static drvlist_t *dl;

static void draw_drv(uint8_t r) {
  if (r == 0) {
    lcd_puts_P(PSTR("< Back"));
    return;
  }
  r--;
  lcd_printf("#%d drive %d", md_dev[dl->dev[r]].address, dl->drive[r]);
}

/**
 * md_browse_select - choose the drive "Browse files" works on
 *
 * Lists the drives that are active right now. With only one drive no
 * question is asked. The chosen device becomes the current one and the
 * chosen drive the current partition, so the browser and its CD
 * commands work there. Returns 0 if the user went back.
 */
uint8_t md_browse_select(void) {
  drvlist_t l;
  uint8_t   i, d, r;

  l.n = 0;
  for (i = 0; i < MD_MAX_DEVICES; i++) {
    if (!md_dev[i].address)
      continue;
    for (d = 0; d < MD_DRIVES; d++) {
      if (md_dev[i].slot[d] != MD_NO_SLOT) {
        l.dev[l.n]   = i;
        l.drive[l.n] = d;
        l.n++;
      }
    }
  }
  if (l.n == 0)
    return 0;

  if (l.n == 1) {
    r = 0;
  } else {
    dl = &l;
    r = choose(l.n + 1, 1, draw_drv);
    dl = NULL;
    if (r == 0)
      return 0;
    r--;
  }

  md_select(l.dev[r]);
  current_part = md_dev[l.dev[r]].slot[l.drive[r]];
  return 1;
}

/* ------------------------------------------------------------------ */
/*  entry point                                                       */
/* ------------------------------------------------------------------ */

void menu_drives(void) {
  buffer_t *b;
  uint8_t  i, r = 0, sel, result = 0;

  if (max_part == 0) {
    lcd_clear();
    lcd_puts_P(PSTR("No card\n\nPress any key"));
    wait_key();
    return;
  }

  b = alloc_system_buffer();
  if (b == NULL)
    goto nomem;
  st = (state_t *)b->data;
  memset(st, 0, sizeof(state_t));
  st->bufs[0] = b;
  for (i = 1; i <= PATH_BUFFERS; i++) {
    st->bufs[i] = alloc_system_buffer();
    if (st->bufs[i] == NULL)
      goto nomem;
  }

  /* drive lines of the file, or the single legacy device */
  for (i = 0; i < PATH_SLOTS; i++)
    st->tab[i].path = slot_ptr(i);
  st->count = md_edit_load(st->tab, PATH_SLOTS, &st->file_exists);
  st->had_lines = (st->count > 0);
  st->slots_used = (1 << st->count) - 1;
  for (i = st->count; i < PATH_SLOTS; i++)
    st->tab[i].path = NULL;
  if (st->count == 0)
    add_entry(device_address, 0);

  for (;;) {
    build_rows();
    r = choose(st->nrows, r, draw_row);
    sel = st->rows[r];

    if (sel == ROW_EXIT)
      break;
    if (sel == ROW_SAVE) {
      result = save();
      if (result)
        break;
    } else if (sel == ROW_ADD) {
      add_device();
    } else {
      drive_menu(sel);
    }
  }

  for (i = 0; i <= PATH_BUFFERS; i++)
    if (st->bufs[i])
      free_buffer(st->bufs[i]);
  st = NULL;

  if (result == 2) {
    /* read the new file like after a card change */
    free_multiple_buffers(FMB_ALL);
    filesystem_init(0);
    md_layout_changed = 0;
  }
  return;

 nomem:
  if (b) {
    for (i = 0; i <= PATH_BUFFERS; i++)
      if (st->bufs[i])
        free_buffer(st->bufs[i]);
  }
  set_error(ERROR_OK);
  lcd_clear();
  lcd_puts_P(PSTR("Not enough buffers\nClose open files\n\nPress any key"));
  wait_key();
}
