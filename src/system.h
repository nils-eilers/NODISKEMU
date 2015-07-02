/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2014  Ingo Korb <ingo@akana.de>

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


   system.h: System-specific initialisation

*/

#ifndef SYSTEM_H
#define SYSTEM_H

/* Early initialisation, called immediately in main() */
void system_init_early(void);

/* Late initialisation, called after all hardware is set up */
void system_init_late(void);

/* Put MCU into low-power mode until the next interrupt */
void system_sleep(void);

/* Reset MCU */
__attribute__((noreturn)) void system_reset(void);



/* AVR Inline Code */

#ifdef __AVR__

/* Disable interrupts */
static inline void disable_interrupts(void) {
  cli();
}

/* Enable interrupts */
static inline void enable_interrupts(void) {
  sei();
}

#else

/* Enable/disable interrupts */
void disable_interrupts(void);
void enable_interrupts(void);

#endif
#endif
