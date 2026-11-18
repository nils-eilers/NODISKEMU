/* NODISKEMU - SD/MMC to IEEE-488 interface/controller

   Multi-drive support: up to MD_MAX_DEVICES device addresses, each with
   drive 0 and optionally drive 1, configured by a text file on the card.

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


   multidrive.h: Definitions for multi-drive support

*/

#ifndef MULTIDRIVE_H
#define MULTIDRIVE_H

#include <stdint.h>
#include "config.h"
#include "bus.h"

#define MD_MAX_DEVICES   4
#define MD_DRIVES        2
#define MD_NO_SLOT       0xff
#define MD_NO_DEVICE     0xff

/* Name of the configuration file in the root directory of the card */
#define MD_CONFIG_FILE   "PETSD.CFG"
#define MD_TEMP_FILE     "PETSD.TMP"   /* used while rewriting it */

typedef struct {
  uint8_t address;            /* bus address, 0 = entry unused       */
  uint8_t file_address;       /* address as written in PETSD.CFG     */
  uint8_t slot[MD_DRIVES];    /* partition[] index per drive or NO_SLOT */
  uint8_t err_code;           /* saved error channel state while the */
  uint8_t err_track;          /*   device is not addressed            */
  uint8_t err_sector;
} md_device_t;

#ifdef CONFIG_MULTIDRIVE

extern uint8_t     md_active;       /* non-zero if a configuration was loaded */
extern md_device_t md_dev[MD_MAX_DEVICES];
extern uint8_t     md_cur;          /* index of the currently addressed device */
extern uint8_t     md_drive_given;  /* last parsed name had an explicit drive */
extern uint8_t     md_device_lost;  /* md_init: addressed device is gone   */
extern uint8_t     md_layout_changed; /* md_init: address list changed, cleared by the user */

/* Read the configuration file and set up the drive slots. */
/* Must be called after fatops_init and before eefsops_init. */
void md_init(void);

/* Return the index of the device with bus address addr or MD_NO_DEVICE */
uint8_t md_find(uint8_t addr);

/* Make device idx the current one, switching drive and error channel */
void md_select(uint8_t idx);

/* Change the bus address of the current device until the next restart */
/* Returns 0 if successful, 1 if the address is invalid or in use       */
uint8_t md_set_address(uint8_t addr);

/* XW: store an address change of the current device in PETSD.CFG */
void md_store_address(void);

/* ---- editing PETSD.CFG (drives menu, XW) ---- */

#define MD_PATH_MAX      128   /* path storage per drive line incl. 0 */
#define MD_PATH_LIMIT    112   /* longest path that fits a line:     */
                               /* 119 characters minus "30:1 = "      */
#define MD_EDIT_MAX      12    /* drive lines while editing           */

/* flags of md_edit_t */
#define MDE_REMOVED      1     /* drop the line                       */
#define MDE_NEWPATH      2     /* write the line anew with path       */

/* One drive line while editing the configuration */
typedef struct {
  uint8_t file_address;  /* address in the file, 0 = new line         */
  uint8_t drive;         /* drive number 0 or 1                       */
  uint8_t address;       /* address to write                          */
  uint8_t flags;         /* MDE_*                                     */
  uint8_t *path;         /* ASCII path, needed for new and MDE_NEWPATH */
} md_edit_t;

/* Read the drive lines of PETSD.CFG into tab, whose path pointers must  */
/* point to MD_PATH_MAX bytes each. Returns the number of lines read,    */
/* *exists tells if the file exists. Uses the command buffer.           */
uint8_t md_edit_load(md_edit_t *tab, uint8_t max, uint8_t *exists);

/* Write PETSD.CFG with the changes in tab via PETSD.TMP.          */
/* Returns 0 if successful, otherwise the error channel is set.    */
uint8_t md_edit_save(md_edit_t *tab, uint8_t count, uint8_t create);

#ifdef CONFIG_ONBOARD_DISPLAY
/* The "Configure drives" menu */
void menu_drives(void);

/* "Browse files": let the user choose a drive and make it current. */
/* Returns 0 if the user went back.                                  */
uint8_t md_browse_select(void);
#endif

/* Bus address of the currently addressed device */
#define MD_CUR_ADDRESS (md_active ? md_dev[md_cur].address : device_address)

/* Return the partition index of drive 0/1 of the current device */
static inline uint8_t md_drive_slot(uint8_t drive) {
  if (drive >= MD_DRIVES)
    return MD_NO_SLOT;
  return md_dev[md_cur].slot[drive];
}

/* Return the drive number (0/1) a partition index belongs to */
uint8_t md_slot_drive(uint8_t part);

/* Non-zero if the current device has a drive 1 */
static inline uint8_t md_is_dual(void) {
  return md_active && md_dev[md_cur].slot[1] != MD_NO_SLOT;
}

#else

#  define md_active      0
#  define md_cur         0
#  define md_drive_given 0
#  define md_device_lost 0
#  define md_layout_changed 0
#  define md_find(a)     ((a) == device_address ? 0 : MD_NO_DEVICE)
#  define md_select(i)   do {} while (0)
#  define MD_CUR_ADDRESS device_address
#  define md_init()      do {} while (0)
#  define md_drive_slot(d) MD_NO_SLOT
#  define md_slot_drive(p) 0
#  define md_is_dual()   0

#endif

#endif
