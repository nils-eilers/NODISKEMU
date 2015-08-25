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


; TODO: include button functions

#include <avr/io.h>
#include <avr/interrupt.h>
#include "asmconfig.h"

.global TIMER1_COMPA_vect
.extern ticks
.extern led_state

TIMER1_COMPA_vect:


; Enable interrupts ASAP again, esp. for ATN
        sei

; Save registers

        push    r0
        in      r0, 0x3f        ; save SREG
        push    r24
        push    r25

; Increment tick counter
        lds     r24, ticks      ; r24: lsb ticks
        lds     r25, ticks+1
        adiw    r24, 1
        sts     ticks, r24
        sts     ticks+1, r25

; Flash error LED?
        lds     r25, led_state  ; Execute this section only if
        sbrs    r25, 0          ; led_state's bit 0 is set
        rjmp noerror

        andi    r24, 0x0F       ; lsb ticks & 15
        brne    noerror

; toggle dirty led
        sbi _SFR_IO_ADDR(LED_DIRTY_INPUT), LED_DIRTY_PIN

noerror:

; Restore registers
quit:
        pop     r25
        pop     r24
        out     0x3f, r0        ; restore SREG
        pop     r0
        reti
