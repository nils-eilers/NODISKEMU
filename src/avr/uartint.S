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

.extern txbuf
.extern read_idx
.extern write_idx
.global USART0_UDRE_vect

USART0_UDRE_vect:

        push	r0
        in	r0, 0x3f	; save SREG

; We want to enable interrupts ASAP again for a fast ATN acknowledge,
; but the UDRE interrupt is level triggered. That means, it will
; trigger as long as the UDR register is empty or it gets disabled.
; That's why we disable the UART interrupt now:

        push	r18
; UCSRB &= ~ _BV(UDRIE);        ; disable UART interrupt
        lds	r18, 0x00C1
        andi	r18, 0xDF
        sts	0x00C1, r18
; Enable interrupts ASAP again, esp. for ATN
        sei

        push	r19             ; save other registers
        push	r24
        push	r25
        push	r30
        push	r31

; If the transmit buffer is emtpy, return without enabling the
; interrupt again
; if (read_idx == write_idx) return;
        lds	r18, read_idx
        lds	r19, read_idx+1
        lds	r24, write_idx
        lds	r25, write_idx+1
        cp	r18, r24        ; compare low byte
        brne    .Lcont
        cp 	r19, r25        ; compare high byte
        breq quit
.Lcont:

; There is something to transmit, copy it to the UART's data register
; UDR = txbuf[read_idx];
        lds	r30, read_idx
        lds	r31, read_idx+1
        subi	r30, lo8(-(txbuf))
        sbci	r31, hi8(-(txbuf))
        ld	r24, Z
        sts	0x00C6, r24     ; UDR0

; Compute next position for transmit buffer
; read_idx = (read_idx+1) & (sizeof(txbuf)-1);
        lds	r24, read_idx
        lds	r25, read_idx+1
        adiw	r24, 1          ; increment
        eor	r25, r25        ; clear high byte
        sts	read_idx,   r24
        sts	read_idx+1, r25

; If the transmit buffer is empty now, leave the UART interrupt disabled
; if (read_idx == write_idx)    ; buffer empty?
        lds	r18, write_idx
        lds	r19, write_idx+1
        cp	r18, r24
        brne .Lcont2
        cp 	r19, r25
        brne    quit
.Lcont2:

; no, enable uart interrupt again
        lds	r24, 0x00C1
        ori 	r24, 0x20
        sts	0x00C1, r24

quit:
        pop	r31             ; restore registers
        pop	r30
        pop	r25
        pop	r24
        pop	r19
        pop	r18

        out	0x3f, r0	; restore SREG
        pop	r0
        reti
