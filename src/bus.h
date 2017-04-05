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


   bus.h: Common IEC/IEEE bus definitions

*/

#ifndef BUS_H
#define BUS_H

#include <stdbool.h>

#include "config.h"
#include "iec.h"
#include "ieee.h"

extern uint8_t device_address;

enum { IEC, IEEE488 };

void iec_interface_init(void);
void ieee_interface_init(void);

#ifdef HAVE_DUAL_INTERFACE
extern uint8_t active_bus;

static inline void bus_interface_init(void) {
  if (active_bus == IEC)
    iec_interface_init();
  else
    ieee_interface_init();
}

static inline void bus_init(void) {
  if (active_bus == IEC)
    iec_init();
  else
    ieee488_Init();
}

#ifdef IEC_SLOW_IEEE_FAST

#include <avr/power.h>
#include "uart.h"
#include "timer.h"
#include "spi.h"

static inline void set_clock_prescaler(uint8_t bus) {
  uart_flush();
  if (active_bus == IEC) clock_prescale_set(clock_div_2);    // Set clock to 16/2 =  8 MHz
  else                   clock_prescale_set(clock_div_1);    // Set clock to        16 MHz
  timer_init();
  uart_init();
  spi_set_speed(SPI_SPEED_FAST);
}
#else
static inline void set_clock_prescaler(uint8_t bus) {}
#endif

static inline void bus_mainloop(void) {
  set_clock_prescaler(active_bus);
  if (active_bus == IEC)
    iec_mainloop();
  else
    ieee_mainloop();
}

static inline void bus_sleep(bool sleep) {
  if (active_bus == IEC)
    iec_sleep(sleep);
  else
    ieee488_BusSleep(sleep);
}
#else
#ifdef CONFIG_HAVE_IEC
#define active_bus IEC

static inline void bus_interface_init(void) {
  iec_interface_init();
}

static inline void bus_init(void) {
  iec_init();
}

static inline void bus_mainloop(void) {
  iec_mainloop();
}

static inline void bus_sleep(bool sleep) {
  iec_sleep(sleep);
}
#else
#ifdef CONFIG_HAVE_IEEE
#define active_bus IEEE488

static inline void bus_interface_init(void) {
  ieee_interface_init();
}

static inline void bus_init(void) {
  ieee488_Init();
}

static inline void bus_mainloop(void) {
  ieee_mainloop();
}

static inline void bus_sleep(bool sleep) {
  ieee488_BusSleep(sleep);
}
#endif // CONFIG_HAVE_IEEE
#endif // CONFIG_HAVE_IEC
#endif // HAVE_DUAL_INTERFACE

#endif
