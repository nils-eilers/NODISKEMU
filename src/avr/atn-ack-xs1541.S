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
#include <avr/interrupt.h>

.global PCINT3_vect
.extern ieee488_ATN_received


PCINT3_vect:

; interrupt caused by rising or falling edge of ATN?
        sbic _SFR_IO_ADDR(PIND), PD3    ; skip next instruction if ATN is low
        reti                            ; ATN is high --> return from interrupt

; save registers
        push r2
        in r2, _SFR_IO_ADDR(SREG)
        push r16

; acknowledge ATN by pulling NRFD low and releasing NDAC
; switch ports to listen mode

; configure EOI(PC7) + DAV(PC6) as inputs
; NDAC(PC4) is actually an output, but is configured for input also
; because the high state is achieved by enabling the pull-up resistor
; in input mode
        in r16, _SFR_IO_ADDR(DDRC)
        andi r16, 255 - _BV(PC7) - _BV(PC6) - _BV(PC4);
        ori r16, _BV(PC5)               ; NRFD as output
        out _SFR_IO_ADDR(DDRC), r16

        cbi _SFR_IO_ADDR(PORTC), PC5    ; NRFD low

; enable pull-up resistors for EOI(PC7), DAV(PC6) and NDAC(PC4)
        in r16, _SFR_IO_ADDR(PORTC)
        ori r16, _BV(PC7) + _BV(PC6) + _BV(PC4)
        out _SFR_IO_ADDR(PORTC), r16

        clr r16                         ; switch data lines to input
        out _SFR_IO_ADDR(DDRA), r16
        ser r16                         ; set all bits in register
        out _SFR_IO_ADDR(PORTA), r16    ; enable all pull-ups for data lines

; Set ATN received flag

        ser r16                         ; set all bits in register
        sts ieee488_ATN_received, r16

; restore registers
        pop r16
        out _SFR_IO_ADDR(SREG), r2
        pop r2

        reti
