/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2014  Ingo Korb <ingo@akana.de>

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


   atn-ack-petsd+.S: ATN acknowledge code by Nils Eilers <nils.eilers@gmx.de>

*/

#include <avr/io.h>

.global INT0_vect


#define SAVE_SREG       r2
#define TMP             r16

#define IO_SREG         0x3F
#define IO_DDR_EOI      0x0A
#define IO_PORT_NRFD    0x08
#define IO_PORT_NDAC    0x08
#define IO_DDR_NRFD     0x07
#define IO_DDR_NDAC     0x07
#define IO_PORT_TE      0x05
#define IO_DDR_DAV      0x04
#define IO_DDR_DATA     0x01

#define NRFD            7
#define NDAC            6
#define EOI             7
#define DAV             2
#define TE              0

INT0_vect:

; save registers
        push SAVE_SREG
        in SAVE_SREG, IO_SREG
        push TMP

; switch all ports to input to avoid conflicting with bus drivers

        clr TMP                 ; switch data lines to input
        out IO_DDR_DATA, TMP
        cbi 0x0A, 7             ; D8 (PD7) as input

        cbi IO_DDR_NRFD, NRFD   ; NRFD as input
        cbi IO_DDR_NDAC, NDAC   ; NDAC as input
        cbi IO_DDR_EOI, EOI     ; EOI as input
        cbi IO_DDR_DAV, DAV     ; DAV as input

; switch bus drivers to listen mode (TE=0)

        cbi IO_PORT_TE, TE

; switch port directions that are outputs in listen mode

        sbi IO_DDR_NRFD, NRFD   ; NRFD as output
        sbi IO_DDR_NDAC, NDAC   ; NDAC as output

; acknowledge ATN

        cbi IO_PORT_NRFD, NRFD  ; pull NRFD low
        sbi IO_PORT_NDAC, NDAC  ; release NDAC

; restore registers
        pop TMP
        out IO_SREG, SAVE_SREG
        pop SAVE_SREG

        reti
