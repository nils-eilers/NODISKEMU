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


   main.c: Lots of init calls for the submodules

*/

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "bus.h"
#include "diskchange.h"
#include "diskio.h"
#include "display.h"
#include "eeprom-conf.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "filesystem.h"
#include "i2c.h"
#include "led.h"
#include "time.h"
#include "rtc.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "uart.h"
#include "ustring.h"
#include "utils.h"
#include "diagnose.h"
#include "lcd.h"
#include "menu.h"

#ifdef HAVE_DUAL_INTERFACE
  uint8_t active_bus = IEEE488;
#endif

#if defined(__AVR__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1))
int main(void) __attribute__((OS_main));
#endif
int main(void) {
  /* Early system initialisation */
  early_board_init();
  system_init_early();
  leds_init();

  set_busy_led(1);
  set_dirty_led(0);

  /* Due to an erratum in the LPC17xx chips anything that may change */
  /* peripheral clock scalers must come before system_init_late()    */
  uart_init();
#ifndef SPI_LATE_INIT
  spi_init(SPI_SPEED_SLOW);
#endif
  timer_init();
  i2c_init();

  /* Second part of system initialisation, switches to full speed on ARM */
  system_init_late();
  enable_interrupts();

  /* Prompt software name and version string */
  uart_puts_P(PSTR("\r\nNODISKEMU " VERSION "\r\n"));

  /* Internal-only initialisation, called here because it's faster */
  buffers_init();
  buttons_init();

  /* Anything that does something which needs the system clock */
  /* should be placed after system_init_late() */
  rtc_init();    // accesses I2C
  disk_init();   // accesses card
  read_configuration(); // restores configuration, may change device address

  filesystem_init(0);
  // FIXME: change_init();

#ifdef CONFIG_REMOTE_DISPLAY
  /* at this point all buffers should be free, */
  /* so just use the data area of the first to build the string */
  uint8_t *strbuf = buffers[0].data;
  ustrcpy_P(strbuf, versionstr);
  ustrcpy_P(strbuf+ustrlen(strbuf), longverstr);
  if (display_init(ustrlen(strbuf), strbuf)) {
    display_address(device_address);
    display_current_part(0);
  }
#endif

  set_busy_led(0);

#if defined(HAVE_SD)
  /* card switch diagnostic aid - hold down PREV button to use */
  if (menu_system_enabled && get_key_press(KEY_PREV))
    board_diagnose();
#endif

  if (menu_system_enabled)
    lcd_splashscreen();

  bus_interface_init();
  bus_init();
  read_configuration();
  late_board_init();

  for (;;) {
    if (menu_system_enabled)
      lcd_refresh();
    else {
      lcd_clear();
      lcd_printf("#%d", device_address);
    }
    /* Unit number may depend on hardware and stored settings */
    /* so present it here at last */
    printf("#%02d\r\n", device_address);
    bus_mainloop();
    bus_interface_init();
    bus_init();    // needs delay, inits device address with HW settings
  }
}
