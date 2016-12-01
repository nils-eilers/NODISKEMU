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
#include "asmconfig.h"

#define WR_ADDR0        0x40
#define IODIRA          0

#define SAVE_SREG       r2
#define TMP             r16
#define TMP2            r17

#define IO_SREG         0x3F
#define IO_SPDR         0x2E
#define IO_SPSR         0x2D
#define SPIF0           7

; Port C
#define IO_PORT_NRFD    0x08
#define IO_PORT_NDAC    0x08
#define IO_PORT_TEC     0x08
#define IO_DDR_NRFD     0x07
#define IO_DDR_NDAC     0x07
#define IO_DDR_EOI      0x07
#define IO_DDR_DAV      0x07
#define NRFD            7
#define NDAC            6
#define DAV             5
#define EOI             4
#define TEC             3
#define PECS            2

; Port B
#define IO_PORT_TED     0x05
#define TED             1

#ifdef CONFIG_HAVE_IEEE

; ATN interrupt

.global INT0_vect
.extern ieee488_TE75161
.extern ieee488_ATN_received


INT0_vect:

; Save registers
        push SAVE_SREG
        in   SAVE_SREG, IO_SREG
        push TMP
;        push TMP2

; SBI and CBI commands do not affect flags or registers, so no need
; to save registers yet

        cbi IO_DDR_EOI, EOI              ; EOI as input
        cbi IO_DDR_DAV, DAV              ; DAV as input
        cbi IO_PORT_TEC, TEC             ; Switch control lines to listen mode
        sbi IO_DDR_NRFD, NRFD            ; NRFD as output
        sbi IO_DDR_NDAC, NDAC            ; NDAC as output

; ATN acknowledge: pull NRFD low, release NDAC
atn_ack:
        cbi IO_PORT_NRFD, NRFD           ; pull NRFD low
        sbi IO_PORT_NDAC, NDAC           ; release NDAC

; Set ATN received flag

        ser TMP                          ; set all bits in register
        sts ieee488_ATN_received, TMP

; Clear talk mode flag because drivers are in listen mode now
        clr TMP
        sts ieee488_TE75161, TMP

#if 0
; Switching port direction of data lines required?
        lds TMP, ieee488_HWTalkMode
        tst TMP
        breq done

; Switch IEEE-488 data lines attached to the port expander to input

; Make sure, SPI is ready
wait1:  in TMP2, IO_SPSR                 ; Wait until SPI TX complete
        sbrs TMP2, 7
        rjmp wait1

; Deselect other SPI devices for the case of SPI being in use
DeselectOther:
        sbi  _SFR_IO_ADDR(SPI_PORT_RAMCS), SPI_PIN_RAMCS
        sbi  _SFR_IO_ADDR(SPI_PORT_SDCS),  SPI_PIN_SDCS

; Select port expander and send command to switch data lines to input
SelectPE:
        cbi  _SFR_IO_ADDR(SPI_PORT_PECS),  SPI_PIN_PECS

SendWR:
        ldi TMP, WR_ADDR0                ; MCP23S17 write command
        out IO_SPDR, TMP
wait2:  in TMP2, IO_SPSR                 ; Wait until SPI TX complete
        sbrs TMP2, 7
        rjmp wait2

SendIODIRA:
        ldi TMP, IODIRA                  ; to IODIRA register
        out IO_SPDR, TMP
wait3:  in TMP2, IO_SPSR                 ; Wait until SPI TX complete
        sbrs TMP2, 7
        rjmp wait3

SendINPUT:
        ldi TMP, 0xFF                    ; Port as input
        out IO_SPDR, TMP
wait4:  in TMP2, IO_SPSR                 ; Wait until SPI TX complete
        sbrs TMP2, 7
        rjmp wait4

; Deselect port expander
        sbi  _SFR_IO_ADDR(SPI_PORT_PECS), SPI_PIN_PECS

; Switch bus drivers for data lines to listen mode (TE=0)
        cbi IO_PORT_TED, TED

; Clear talk mode flag because drivers are in listen mode now
        clr TMP
        sts ieee488_HWTalkMode, TMP

#endif

; End of interrupt routine, restore registers
done:
;       pop     TMP2
        pop     TMP
        out     IO_SREG, SAVE_SREG
        pop     SAVE_SREG
        reti

#endif // CONFIG_HAVE_IEEE

.end
