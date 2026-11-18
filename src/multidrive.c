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


   multidrive.c: Multi-drive support

   The configuration file PETSD.CFG in the root directory of the card
   assigns a directory or disk image to each drive, one per line:

     # comment
     8:0 = /
     8:1 = /GAMES/ARCHON.D64
     9:0 = /WORK

   Each configured drive gets its own slot in partition[]. All slots
   share the FAT volume of the first partition on the card. Without a
   configuration file nothing changes and the legacy single drive
   behaviour is kept.

*/

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "bus.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "parser.h"
#include "ustring.h"
#include "utils.h"
#include "multidrive.h"

uint8_t     md_active;
md_device_t md_dev[MD_MAX_DEVICES];
uint8_t     md_device_lost;
uint8_t     md_layout_changed;
uint8_t     md_cur;
uint8_t     md_drive_given;

#define MD_MAX_SLOTS (MD_MAX_DEVICES * MD_DRIVES)

#if MD_MAX_SLOTS >= CONFIG_MAX_PARTITIONS
#  error CONFIG_MAX_PARTITIONS must be larger than MD_MAX_DEVICES * MD_DRIVES
#endif

#if MD_MAX_SLOTS > 9
#  error The slot number must fit into a single digit for do_chdir
#endif

/**
 * md_slot_drive - return the drive number of a partition slot
 * @part: partition index
 *
 * Returns 0 or 1 for a drive slot of the current device, otherwise
 * the drive number of the first device using this slot or 0.
 */
uint8_t md_slot_drive(uint8_t part) {
  uint8_t i;

  if (md_dev[md_cur].slot[1] == part)
    return 1;
  if (md_dev[md_cur].slot[0] == part)
    return 0;

  for (i = 0; i < MD_MAX_DEVICES; i++)
    if (md_dev[i].slot[1] == part)
      return 1;

  return 0;
}

/**
 * md_find - find the device with a bus address
 * @addr: bus address
 *
 * Returns the index of the device in md_dev or MD_NO_DEVICE.
 * Without a configuration only device_address is used.
 */
uint8_t md_find(uint8_t addr) {
  uint8_t i;

  if (!md_active)
    return addr == device_address ? 0 : MD_NO_DEVICE;

  for (i = 0; i < MD_MAX_DEVICES; i++)
    if (md_dev[i].address && md_dev[i].address == addr)
      return i;

  return MD_NO_DEVICE;
}

/**
 * md_select - make a device the current one
 * @idx: index in md_dev
 *
 * Saves the error channel state of the previous device and restores
 * the one of the new device. The current partition becomes drive 0
 * of the new device, so all code using current_part works on it.
 */
void md_select(uint8_t idx) {
  md_device_t *dev;

  if (idx == md_cur || idx >= MD_MAX_DEVICES)
    return;

  dev = &md_dev[md_cur];
  dev->err_code   = current_error;
  dev->err_track  = current_error_track;
  dev->err_sector = current_error_sector;

  md_cur       = idx;
  dev          = &md_dev[idx];
  current_part = dev->slot[0];
  set_error_ts(dev->err_code, dev->err_track, dev->err_sector);
}

/**
 * md_set_address - change the bus address of the current device
 * @addr: new address
 *
 * The change lasts until the next restart or card change.
 * Returns 0 if successful, 1 if the address is out of range or
 * already used by another device.
 */
uint8_t md_set_address(uint8_t addr) {
  if (addr < 4 || addr > 30)
    return 1;

  if (md_find(addr) != MD_NO_DEVICE && md_find(addr) != md_cur)
    return 1;

  md_dev[md_cur].address = addr;
  return 0;
}

/* find the table entry for a device address, create it if required */
static md_device_t *get_device(uint8_t address) {
  uint8_t i;

  for (i = 0; i < MD_MAX_DEVICES; i++)
    if (md_dev[i].address == address)
      return &md_dev[i];

  for (i = 0; i < MD_MAX_DEVICES; i++)
    if (md_dev[i].address == 0) {
      md_dev[i].address      = address;
      md_dev[i].file_address = address;
      return &md_dev[i];
    }

  return NULL;
}

/**
 * cfg2pet - convert a path from the configuration file to PETSCII
 * @buf: 0-terminated string, converted in place
 *
 * Unlike asc2pet both upper and lower case letters become unshifted
 * PETSCII letters, just like a path typed on a C64. FAT names are
 * matched without case anyway, but check_imageext only recognizes
 * unshifted extensions, so "ARCHON.D64" must not turn into shifted
 * characters.
 */
static void cfg2pet(uint8_t *buf) {
  for (; *buf; buf++) {
    if (*buf >= 'a' && *buf <= 'z')
      *buf -= 'a' - 'A';
    else if (*buf == '~')
      *buf = 255;
  }
}

/* Start of a drive line: "<address> : <drive> = <path>" */
typedef struct {
  uint8_t  address;
  uint8_t  drive;
  uint8_t  addr_len;     /* number of address digits */
  uint16_t addr_start;   /* offset of the address    */
  uint16_t path_start;   /* offset of the path       */
} md_head_t;

/**
 * parse_head - parse the start of a configuration file line
 * @line: line, need not be 0-terminated
 * @len : length of the line
 * @h   : receives the parsed values
 *
 * Returns 1 for a drive line with an address of 4..30, 0 for comments,
 * blank lines and everything else.
 */
static uint8_t parse_head(uint8_t *line, uint16_t len, md_head_t *h) {
  uint16_t i = 0;

#define BLANKS() while (i < len && (line[i] == ' ' || line[i] == '\t')) i++

  BLANKS();
  h->addr_start = i;
  h->address    = 0;
  while (i < len && isdigit(line[i]) && i - h->addr_start < 3)
    h->address = h->address * 10 + (line[i++] - '0');
  h->addr_len = i - h->addr_start;
  if (h->addr_len == 0 || h->address < 4 || h->address > 30)
    return 0;

  BLANKS();
  if (i >= len || line[i++] != ':')
    return 0;
  BLANKS();
  if (i >= len || (line[i] != '0' && line[i] != '1'))
    return 0;
  h->drive = line[i++] - '0';

  BLANKS();
  if (i >= len || line[i++] != '=')
    return 0;
  BLANKS();
  h->path_start = i;
  return 1;

#undef BLANKS
}

/* cut trailing blanks of a 0-terminated string */
static void strip_trailing(uint8_t *str) {
  uint8_t *end = str + ustrlen(str);

  while (end > str && (end[-1] == ' ' || end[-1] == '\t'))
    end--;
  *end = 0;
}

/**
 * apply_line - parse one line of the configuration file
 * @line : 0-terminated line, ASCII
 * @slot : next free partition slot
 *
 * Returns 1 if a slot was used, 0 otherwise. The line buffer must be
 * the command buffer, it is modified in place.
 */
static uint8_t apply_line(uint8_t *line, uint8_t slot) {
  uint8_t *path;
  md_head_t h;
  md_device_t *dev;

  if (!parse_head(line, ustrlen(line), &h))
    return 0;
  path = line + h.path_start;
  strip_trailing(path);

  dev = get_device(h.address);
  if (dev == NULL || dev->slot[h.drive] != MD_NO_SLOT)
    return 0;

  /* Build a legacy CD string in front of the path: "<slot+1>//path" */
  /* At least four characters ("8:0=") precede the path.              */
  if (*path == '/') {
    path -= 2;
  } else {
    path -= 3;
    path[2] = '/';
  }
  path[0] = '1' + slot;
  path[1] = '/';
  cfg2pet(path + 1);

  /* reset the slot to the root directory of the first FAT volume */
  partition[slot].fatfs           = &fatvol[0];
  partition[slot].fop             = &fatops;
  partition[slot].current_dir.fat = 0;

  set_error(ERROR_OK);
  do_chdir(path);
  if (current_error != ERROR_OK) {
    if (partition[slot].fop != &fatops)
      image_unmount(slot);
    partition[slot].fop = &fatops;
    return 0;
  }

  dev->slot[h.drive] = slot;
  return 1;
}

/**
 * read_line - read the next line of the configuration file
 * @fh : file
 * @pos: file position, advanced past the line
 *
 * Puts the line 0-terminated into the command buffer. Lines too long
 * for it are skipped. Returns 1 if a line was read, 0 at the end.
 */
static uint8_t read_line(FIL *fh, uint32_t *pos) {
  UINT     n;
  uint8_t  *p;
  uint8_t  skipping = 0;

  for (;;) {
    if (f_lseek(fh, *pos) != FR_OK)
      return 0;
    if (f_read(fh, command_buffer, CONFIG_COMMAND_BUFFER_SIZE - 1, &n) != FR_OK)
      return 0;
    if (n == 0)
      return 0;
    command_buffer[n] = 0;

    p = command_buffer;
    while (*p && *p != '\r' && *p != '\n')
      p++;
    *pos += p - command_buffer;

    if (*p == 0 && n == CONFIG_COMMAND_BUFFER_SIZE - 1) {
      /* line too long for the buffer, ignore it */
      skipping = 1;
      continue;
    }
    if (*p)
      (*pos)++;     // skip one line terminator, empty lines are ignored
    *p = 0;

    if (skipping) {
      skipping = 0;
      continue;
    }
    return 1;
  }
}

/**
 * md_edit_load - read the drive lines of PETSD.CFG for editing
 * @tab   : table to fill, path pointers set by the caller
 * @max   : number of entries in tab
 * @exists: set to 1 if the file exists
 *
 * Duplicate lines are skipped like md_load does. Returns the number of
 * entries filled. Uses the command buffer.
 */
uint8_t md_edit_load(md_edit_t *tab, uint8_t max, uint8_t *exists) {
  FIL       fh;
  uint32_t  pos = 0;
  uint8_t   count = 0, i;
  uint8_t   name[] = MD_CONFIG_FILE;
  md_head_t h;
  uint8_t   *path;

  *exists = 0;
  fatvol[0].curr_dir = 0;
  if (f_open(&fatvol[0], &fh, name, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    return 0;
  *exists = 1;

  while (count < max && read_line(&fh, &pos)) {
    if (!parse_head(command_buffer, ustrlen(command_buffer), &h))
      continue;
    for (i = 0; i < count; i++)
      if (tab[i].file_address == h.address && tab[i].drive == h.drive)
        break;
    if (i < count)
      continue;

    path = command_buffer + h.path_start;
    strip_trailing(path);
    ustrncpy(tab[count].path, path, MD_PATH_MAX - 1);
    tab[count].path[MD_PATH_MAX - 1] = 0;
    tab[count].file_address = h.address;
    tab[count].address      = h.address;
    tab[count].drive        = h.drive;
    tab[count].flags        = 0;
    count++;
  }

  f_close(&fh);
  return count;
}

/* write to a file, a short write means the card is full */
static FRESULT put(FIL *out, const void *data, UINT len) {
  UINT    done;
  FRESULT res = f_write(out, data, len, &done);

  if (res == FR_OK && done != len)
    res = FR_DENIED;
  return res;
}

/* write a line end, CR LF or LF */
static FRESULT put_eol(FIL *out, uint8_t crlf) {
  static const uint8_t eol[] = "\r\n";

  return put(out, eol + !crlf, 1 + crlf);
}

/* write the address as decimal number */
static FRESULT put_address(FIL *out, uint8_t address) {
  uint8_t digits[2], n = 0;

  if (address >= 10)
    digits[n++] = '0' + address / 10;
  digits[n++] = '0' + address % 10;
  return put(out, digits, n);
}

/* write a complete drive line "<address>:<drive> = <path>" */
static FRESULT put_line(FIL *out, md_edit_t *e, uint8_t crlf) {
  uint8_t mid[5] = { ':', '0', ' ', '=', ' ' };
  FRESULT res;

  mid[1] += e->drive;
  res = put_address(out, e->address);
  if (res == FR_OK)
    res = put(out, mid, sizeof(mid));
  if (res == FR_OK)
    res = put(out, e->path, ustrlen(e->path));
  if (res == FR_OK)
    res = put_eol(out, crlf);
  return res;
}

static const PROGMEM uint8_t cfg_header[] =
  "# petSD+ drive configuration, written by the menu Configure drives\r\n"
  "# Format: device:drive = path   (drive 0 or 1, path from the root)\r\n"
  "\r\n";

/**
 * md_edit_save - write PETSD.CFG with changes, line by line
 * @tab   : changed drive lines
 * @count : number of entries in tab
 * @create: create the file if it does not exist
 *
 * Lines of entries with a file address get the new address, are
 * written anew (MDE_NEWPATH) or dropped (MDE_REMOVED). Entries without
 * a file address are appended. Comments, blank lines and all other
 * lines stay as they are. The new contents go to PETSD.TMP first,
 * which replaces PETSD.CFG at the end, so a card pulled in between
 * leaves the old file intact. Returns 0 if successful, otherwise the
 * error channel is set.
 */
uint8_t md_edit_save(md_edit_t *tab, uint8_t count, uint8_t create) {
  FATFS    *fs = &fatvol[0];
  FIL      in, out;
  FRESULT  res;
  UINT     n;
  uint32_t pos = 0;
  uint16_t len;
  uint8_t  midline = 0, crlf = 1, eol_seen = 0, have_in, i;
  uint8_t  name_cfg[] = MD_CONFIG_FILE;
  uint8_t  name_tmp[] = MD_TEMP_FILE;
  uint32_t saved_dir = fs->curr_dir;
  md_head_t h;
  md_edit_t *e;
  buffer_t *buf;
  uint8_t  *line;

  buf = alloc_system_buffer();
  if (!buf)
    return 1;               // error set by alloc
  line = buf->data;

  fs->curr_dir = 0;
  res = f_open(fs, &in, name_cfg, FA_READ | FA_OPEN_EXISTING);
  have_in = (res == FR_OK);
  if (!have_in && (res != FR_NO_FILE || !create))
    goto fail;

  res = f_open(fs, &out, name_tmp, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK) {
    if (have_in)
      f_close(&in);
    goto fail;
  }

  if (!have_in) {
    memcpy_P(line, cfg_header, sizeof(cfg_header) - 1);
    res = put(&out, line, sizeof(cfg_header) - 1);
  }

  while (have_in && res == FR_OK) {
    res = f_lseek(&in, pos);
    if (res == FR_OK)
      res = f_read(&in, line, 256, &n);
    if (res != FR_OK || n == 0)
      break;

    /* one line including its terminator, or a 256 byte piece of it */
    for (len = 0; len < n && line[len] != '\n'; len++) ;
    if (len < n)
      len++;
    pos += len;

    /* new lines get the line end of the file */
    if (!eol_seen && line[len - 1] == '\n') {
      eol_seen = 1;
      crlf = (len >= 2 && line[len - 2] == '\r');
    }

    e = NULL;
    if (!midline && parse_head(line, len, &h)) {
      for (i = 0; i < count; i++)
        if (tab[i].file_address == h.address && tab[i].drive == h.drive) {
          e = &tab[i];
          break;
        }
    }

    if (e == NULL) {
      res = put(&out, line, len);
    } else if (e->flags & MDE_REMOVED) {
      /* drop the line */
    } else if (e->flags & MDE_NEWPATH) {
      res = put_line(&out, e, crlf);
    } else {
      /* only the address changes */
      res = put(&out, line, h.addr_start);
      if (res == FR_OK)
        res = put_address(&out, e->address);
      if (res == FR_OK)
        res = put(&out, line + h.addr_start + h.addr_len,
                  len - h.addr_start - h.addr_len);
    }
    midline = (line[len - 1] != '\n');
  }

  /* append new lines */
  for (i = 0; i < count && res == FR_OK; i++) {
    e = &tab[i];
    if (e->file_address || (e->flags & MDE_REMOVED))
      continue;
    if (midline) {
      /* the file ended without a line end */
      res = put_eol(&out, crlf);
      midline = 0;
    }
    if (res == FR_OK)
      res = put_line(&out, e, crlf);
  }

  if (res == FR_OK)
    res = f_close(&out);
  else
    f_close(&out);
  if (have_in)
    f_close(&in);

  if (res == FR_OK && have_in)
    res = f_unlink(fs, name_cfg);
  if (res == FR_OK)
    res = f_rename(fs, name_tmp, name_cfg);
  else
    f_unlink(fs, name_tmp);

 fail:
  fs->curr_dir = saved_dir;
  free_buffer(buf);
  if (res != FR_OK) {
    parse_error(res, 0);
    return 1;
  }
  return 0;
}

/**
 * md_store_address - store an address change of the current device
 *
 * Called by XW. If U0> or M-W 119 changed the address of the addressed
 * device, the drive lines of that device in PETSD.CFG get the new
 * address. Other devices are not touched. If another device still has
 * the new address in the file, nothing is written (30 SYNTAX ERROR),
 * as both would become one device at the next start.
 */
void md_store_address(void) {
  md_device_t *dev = &md_dev[md_cur];
  md_edit_t   tab[MD_DRIVES];
  uint8_t     i;

  if (!md_active || dev->address == dev->file_address)
    return;

  for (i = 0; i < MD_MAX_DEVICES; i++) {
    if (i != md_cur && md_dev[i].address &&
        md_dev[i].file_address == dev->address) {
      set_error(ERROR_SYNTAX_UNKNOWN);
      return;
    }
  }

  for (i = 0; i < MD_DRIVES; i++) {
    tab[i].file_address = dev->file_address;
    tab[i].drive        = i;
    tab[i].address      = dev->address;
    tab[i].flags        = 0;
    tab[i].path         = NULL;
  }

  if (md_edit_save(tab, MD_DRIVES, 0) == 0)
    dev->file_address = dev->address;
}

/**
 * md_load - read the configuration file and set up the drive slots
 * @prev_address: bus address that stays the current device if it exists
 *
 * Leaves legacy mode active if there is no card or no configuration file.
 */
static void md_load(uint8_t prev_address) {
  FIL      fh;
  FRESULT  res;
  uint32_t pos;
  uint8_t  i, slots;
  uint8_t  saved_error, saved_track, saved_sector;
  uint8_t  name[] = MD_CONFIG_FILE;
  buffer_t *save;

  md_active = 0;
  md_cur    = 0;
  memset(md_dev, 0, sizeof(md_dev));
  for (i = 0; i < MD_MAX_DEVICES; i++)
    md_dev[i].slot[0] = md_dev[i].slot[1] = MD_NO_SLOT;

  if (max_part == 0 || partition[0].fop != &fatops)
    return;

  partition[0].fatfs->curr_dir = 0;
  res = f_open(partition[0].fatfs, &fh, name, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    /* md_edit_save was interrupted between deleting the old file */
    /* and renaming the new one, which was complete at that point    */
    uint8_t tmpname[] = MD_TEMP_FILE;
    res = f_open(partition[0].fatfs, &fh, tmpname, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK)
      return;
  }

  saved_error  = current_error;
  saved_track  = current_error_track;
  saved_sector = current_error_sector;

  /* After a card change this runs right before the pending command is */
  /* executed, so the command buffer must survive reading the file.    */
  save = alloc_system_buffer();
  if (save)
    memcpy(save->data, command_buffer, sizeof(command_buffer));

  /* allow the legacy partition syntax in do_chdir to reach every slot */
  max_part = MD_MAX_SLOTS;
  slots    = 0;
  pos      = 0;

  while (slots < MD_MAX_SLOTS && read_line(&fh, &pos))
    slots += apply_line(command_buffer, slots);

  f_close(&fh);

  /* a device needs a working drive 0 */
  for (i = 0; i < MD_MAX_DEVICES; i++) {
    if (md_dev[i].address && md_dev[i].slot[0] == MD_NO_SLOT) {
      md_dev[i].address = 0;
      md_dev[i].slot[1] = MD_NO_SLOT;
    }
  }

  /* move the used entries to the front */
  {
    uint8_t j = 0;
    for (i = 0; i < MD_MAX_DEVICES; i++)
      if (md_dev[i].address)
        md_dev[j++] = md_dev[i];
    for (; j < MD_MAX_DEVICES; j++) {
      md_dev[j].address = 0;
      md_dev[j].slot[0] = md_dev[j].slot[1] = MD_NO_SLOT;
    }
  }

  if (save) {
    memcpy(command_buffer, save->data, sizeof(command_buffer));
    free_buffer(save);
  } else {
    memset(command_buffer, 0, sizeof(command_buffer));
  }
  set_error_ts(saved_error, saved_track, saved_sector);

  if (md_dev[0].address == 0) {
    /* nothing usable configured, fall back to legacy mode */
    fatops_init(0);
    return;
  }

  /* every device starts with its power-on message */
  for (i = 0; i < MD_MAX_DEVICES; i++) {
    md_dev[i].err_code   = ERROR_DOSVERSION;
    md_dev[i].err_track  = 0;
    md_dev[i].err_sector = 0;
  }

  max_part  = slots;
  md_active = 1;

  /* device_address stays untouched: it is the legacy address */
  /* from the EEPROM, used again when the file is removed.     */
  i = md_find(prev_address);
  md_cur       = (i == MD_NO_DEVICE) ? 0 : i;
  current_part = md_dev[md_cur].slot[0];
}

/**
 * md_init - read the configuration file and set up the drive slots
 *
 * Called by filesystem_init after fatops_init. Sets md_device_lost if
 * the device addressed before (e.g. by a command that is still pending
 * after a card change) does not exist anymore and md_layout_changed if
 * the list of bus addresses differs from the one before.
 */
void md_init(void) {
  uint8_t old_address[MD_MAX_DEVICES];
  uint8_t old_active = md_active;
  uint8_t prev_address = MD_CUR_ADDRESS;
  uint8_t i;

  for (i = 0; i < MD_MAX_DEVICES; i++)
    old_address[i] = md_dev[i].address;

  md_load(prev_address);

  md_device_lost = (md_find(prev_address) == MD_NO_DEVICE);

  if (md_active != old_active)
    md_layout_changed = 1;
  for (i = 0; i < MD_MAX_DEVICES; i++)
    if (md_dev[i].address != old_address[i])
      md_layout_changed = 1;
}
