/*-
 * Copyright (c) 2015 Nils Eilers. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <avr/io.h>

.global INT0_vect
.extern ieee488_ATN_received


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

; Set ATN received flag

        ser TMP                 ; set all bits in register
        sts ieee488_ATN_received, TMP

; restore registers
        pop TMP
        out IO_SREG, SAVE_SREG
        pop SAVE_SREG

        reti
