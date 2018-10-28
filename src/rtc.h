/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2018  Ingo Korb <ingo@akana.de>

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


   rtc.h: Definitions for RTC support

   rtc.c contains wrapper functions around the best available RTC at
   runtime: an interrupt driven softrtc can act as fallback, if the clock
   chip wasn't found. The main functions for a particular clock chip are
   defined in device-specific .c files, e.g. pcf8583.c.

*/

#ifndef RTC_H
#define RTC_H

#include "time.h"

typedef enum {
  RTC_NOT_FOUND,  /* No RTC present                    */
  RTC_INVALID,    /* RTC present, but contents invalid */
  RTC_OK          /* RTC present and working           */
} rtcstate_t;

# ifdef HAVE_RTC

extern const /* PROGMEM */ struct tm rtc_default_date;
extern rtcstate_t rtc_state;

/* detect and initialize RTC */
void rtc_init(void);

/* Return current time in struct tm */
void read_rtc(struct tm *time);

/* Set time from struct tm */
void set_rtc(struct tm *time);

# else  // HAVE_RTC

#  define rtc_state RTC_NOT_FOUND

#  define rtc_init()    do {} while(0)

# endif // HAVE_RTC

#endif
