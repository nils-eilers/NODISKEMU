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


   ieee.h: Definitions for the IEEE-488 handling code

*/

#pragma once

// ieee488_RxByte return values:
enum {
  RX_DATA,              // byte received, EOI not set
  RX_EOI,               // byte received, EOI set
  RX_ATN,               // aborted by ATN
  RX_IFC                // aborted by IFC
};

// listen_loop actions:
enum {
  LL_SNIFFONLY,         // ignore third party's data, for sniffing only
  LL_RECEIVE,           // received data or command byte
  LL_OPEN,              // received character for OPEN Filename
};


// function prototypes:
void ieee488_Init(void);
void ieee488_BusSleep(bool sleep);
uint8_t ieee488_ListenIsActive(void);
uint8_t ieee488_RxByte(char *c);
void ieee_mainloop(void);
