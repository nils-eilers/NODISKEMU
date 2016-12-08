/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2015  Ingo Korb <ingo@akana.de>

   NODISKEMU is a fork of sd2iec by Ingo Korb (et al.), http://sd2iec.de

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   eeprom-conf.c: Persistent configuration storage

*/

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "arch-eeprom.h"
#include "diskio.h"
#include "fatops.h"
#include "flags.h"
#include "bus.h"
#include "timer.h"
#include "ustring.h"
#include "eeprom-conf.h"
#include "debug.h"
#include "devnumbers.h"

uint8_t rom_filename[ROM_NAME_LENGTH+1];

/**
 * struct storedconfig - in-eeprom data structure
 * @dummy      : EEPROM position 0 is unused
 * @checksum   : Checksum over the EEPROM contents
 * @structsize : size of the eeprom structure
 * @unused     : unused byte kept for structure compatibility
 * @globalflags: subset of the globalflags variable
 * @address    : device address set by software
 * @hardaddress: device address set by jumpers
 * @fileexts   : file extension mapping mode
 * @drvflags0  : 16 bits of drv mappings, organized as 4 nybbles.
 * @drvflags1  : 16 bits of drv mappings, organized as 4 nybbles.
 * @imagedirs  : Disk images-as-directory mode
 * @romname    : M-R rom emulation file name (zero-padded, but not terminated)
 * @active_bus : IEC or IEEE488
 * @menu_system_enabled : control LCD menu with buttons / buttons set device addr
 *
 * This is the data structure for the contents of the EEPROM.
 *
 * Do not remove any fields!
 * Only add fields at the end!
 */
static EEMEM struct {
  uint8_t  dummy;
  uint8_t  checksum;
  uint16_t structsize;
  uint8_t  unused;
  uint8_t  global_flags;
  uint8_t  address;
  uint8_t  hardaddress;
  uint8_t  fileexts;
  uint16_t drvconfig0;
  uint16_t drvconfig1;
  uint8_t  imagedirs;
  uint8_t  romname[ROM_NAME_LENGTH];
  uint8_t  active_bus;
  uint8_t  menu_system_enabled;
} __attribute__((packed)) storedconfig;


/**
 * read_configuration - reads configuration from EEPROM
 *
 * This function reads the stored configuration values from the EEPROM.
 * If the stored checksum doesn't match the calculated one nothing will
 * be changed.
 *
 * Some situations cause (re-)writing the EEPROM:
 *   - unset size bytes
 *   - bad CRC
 *   - device addr jumper changed since last reboot
 */
void read_configuration(void) {
  uint_fast16_t i,size;
  uint8_t checksum, tmp;
  bool rewrite_required = false;
  uint8_t device_address;

  /* Set default values */
  globalflags         |= POSTMATCH;            /* Post-* matching enabled */
  file_extension_mode  = 0;                    /* Never write x00 format files */
  set_drive_config(get_default_driveconfig()); /* Set the default drive configuration */
  memset(rom_filename, 0, sizeof(rom_filename));

#if 0
  FIXME!
  /* Use the NEXT button to skip reading the EEPROM configuration */
  if (!(buttons_read() & BUTTON_NEXT)) {
    ignore_keys();
    return;
  }
#endif

  size = eeprom_read_word(&storedconfig.structsize);

  /* write, then abort if the size bytes are not set */
  if (size == 0xffff) {
    write_configuration();
    return;
  }

  /* Calculate checksum of EEPROM contents */
  checksum = 0;
  for (i=2; i<size; i++)
    checksum += eeprom_read_byte((uint8_t *)i);

  /* Write, Abort if the checksum doesn't match */
  if (checksum != eeprom_read_byte(&storedconfig.checksum)) {
    debug_puts_P(PSTR("EEPROM checksum error\r\n"));
    write_configuration();
    return;
  }

  /* Read data from EEPROM */

#ifdef CONFIG_HW_ADDR_OR_BUTTONS
  if (size > 31)
    /* Read this first because it affects device_hw_address() */
    menu_system_enabled = eeprom_read_byte(&storedconfig.menu_system_enabled);
#endif

  tmp = eeprom_read_byte(&storedconfig.global_flags);
  globalflags &= (uint8_t)~(POSTMATCH |
                            EXTENSION_HIDING);
  globalflags |= tmp;

  uint8_t current_hw_addr = device_hw_address();
  uint8_t stored_hw_addr  = eeprom_read_byte(&storedconfig.hardaddress);
  uint8_t stored_sw_addr  = eeprom_read_byte(&storedconfig.address);
  if (current_hw_addr != stored_hw_addr) {
    device_address = current_hw_addr;
    rewrite_required = true;
  } else {
    device_address = stored_sw_addr;
  }
  printf("current hw addr: %d\r\n", current_hw_addr);
  printf("stored  hw addr: %d, stored sw addr: %d\r\n", stored_hw_addr,
      stored_sw_addr);
  devnumbers_Add(device_address);

  file_extension_mode = eeprom_read_byte(&storedconfig.fileexts);

#ifdef NEED_DISKMUX
  if (size > 9) {
    uint32_t tmpconfig;
    tmpconfig = eeprom_read_word(&storedconfig.drvconfig0);
    tmpconfig |= (uint32_t)eeprom_read_word(&storedconfig.drvconfig1) << 16;
    set_drive_config(tmpconfig);
  }

  /* sanity check.  If the user has truly turned off all drives, turn the
   * defaults back on
   */
  if(drive_config == 0xffffffff)
    set_drive_config(get_default_driveconfig());
#endif

  if (size > 13)
    image_as_dir = eeprom_read_byte(&storedconfig.imagedirs);

  if (size > 29)
    eeprom_read_block(rom_filename, &storedconfig.romname, ROM_NAME_LENGTH);

#ifdef HAVE_DUAL_INTERFACE
  if (size > 30)
    active_bus = eeprom_read_byte(&storedconfig.active_bus);
#endif

  /* Prevent problems due to accidental writes */
  eeprom_safety();

  if (rewrite_required) write_configuration();
}

/**
 * write_configuration - stores configuration data to EEPROM
 *
 * This function stores the current configuration values to the EEPROM.
 */
void write_configuration(void) {
  uint_fast16_t i;
  uint8_t checksum;

  /* Write configuration to EEPROM */
  eeprom_write_word(&storedconfig.structsize, sizeof(storedconfig));
  eeprom_write_byte(&storedconfig.global_flags,
                    globalflags & (POSTMATCH |
                                   EXTENSION_HIDING));
  eeprom_write_byte(&storedconfig.address, MyDevNumbers[0]);
  eeprom_write_byte(&storedconfig.hardaddress, device_hw_address());
  eeprom_write_byte(&storedconfig.fileexts, file_extension_mode);
#ifdef NEED_DISKMUX
  eeprom_write_word(&storedconfig.drvconfig0, drive_config);
  eeprom_write_word(&storedconfig.drvconfig1, drive_config >> 16);
#endif
  eeprom_write_byte(&storedconfig.imagedirs, image_as_dir);
  memset(rom_filename+ustrlen(rom_filename), 0, sizeof(rom_filename)-ustrlen(rom_filename));
  eeprom_write_block(rom_filename, &storedconfig.romname, ROM_NAME_LENGTH);
  eeprom_write_byte(&storedconfig.active_bus, active_bus);
#ifdef CONFIG_HW_ADDR_OR_BUTTONS
  eeprom_write_byte(&storedconfig.menu_system_enabled, menu_system_enabled);
#endif

  /* Calculate checksum over EEPROM contents */
  checksum = 0;
  for (i=2;i<sizeof(storedconfig);i++)
    checksum += eeprom_read_byte((uint8_t *) i);

  /* Store checksum to EEPROM */
  eeprom_write_byte(&storedconfig.checksum, checksum);

  /* Prevent problems due to accidental writes */
  eeprom_safety();
}

