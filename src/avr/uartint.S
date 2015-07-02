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

; FIXME: this version is still too slow for the CBM-II line

.extern txbuf
.extern read_idx
.extern write_idx
.global USART0_UDRE_vect

USART0_UDRE_vect:

        push	r0
        in	r0, 0x3f	; save SREG

        push	r18
        push	r19
        push	r24
        push	r25
        push	r30
        push	r31

; if (read_idx == write_idx) return;
        lds	r18, read_idx
        lds	r19, read_idx+1
        lds	r24, write_idx
        lds	r25, write_idx+1
        cp	r18, r24        ; compare low byte
        cpc	r19, r25        ; compare high byte
        breq quit

; UDR = txbuf[read_idx];
        lds	r30, read_idx
        lds	r31, read_idx+1
        subi	r30, lo8(-(txbuf))
        sbci	r31, hi8(-(txbuf))
        ld	r24, Z
        sts	0x00C6, r24     ; UDR0

; Enable interrupts ASAP again, esp. for ATN
        sei

; read_idx = (read_idx+1) & (sizeof(txbuf)-1);
        lds	r24, read_idx
        lds	r25, read_idx+1
        adiw	r24, 1          ; increment
        eor	r25, r25        ; clear high byte
        sts	read_idx+1, r25
        sts	read_idx, r24

;  if (read_idx == write_idx)
        lds	r18, read_idx
        lds	r19, read_idx+1
        lds	r24, write_idx
        lds	r25, write_idx+1
        cp	r18, r24
        cpc	r19, r25
        brne    quit
; UCSRB &= ~ _BV(UDRIE);
        lds	r24, 0x00C1
        andi	r24, 0xDF
        sts	0x00C1, r24

quit:
        pop	r31
        pop	r30
        pop	r25
        pop	r24
        pop	r19
        pop	r18

        out	0x3f, r0	; restore SREG
        pop	r0
        reti
