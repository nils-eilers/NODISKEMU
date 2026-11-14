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

; This file is included by the timer interrupt routine from timerint.S
; to read the buttons to r24.
; Please note that pressed buttons are represented with bits set.

; TODO: support other hardware variants


#if CONFIG_HARDWARE_VARIANT == HW_PETSDPLUS || CONFIG_HARDWARE_VARIANT == HW_CBMSDMINI

; The C sampler performs 10-bit validation, median filtering and hysteresis.
; Preserve the additional call-clobbered registers and the saved SREG in r0.
; r24-r29 are already saved by the enclosing ISR. r1 may be nonzero if
; interrupted between MUL and CLR, so provide and restore the C zero register.
        push    r0
        push    r1
        clr     r1
        push    r18
        push    r19
        push    r20
        push    r21
        push    r22
        push    r23
        push    r30
        push    r31
        call    analog_buttons_poll
        pop     r31
        pop     r30
        pop     r23
        pop     r22
        pop     r21
        pop     r20
        pop     r19
        pop     r18
        pop     r1
        pop     r0

#else
#if CONFIG_HARDWARE_VARIANT == HW_PETSD

        clr     r24
        sbis    _SFR_IO_ADDR(PINB), 3 	; skip next if PB3 is high
        ori     r24, 2			; NEXT
        sbis    _SFR_IO_ADDR(PINB), 1 	; skip next if PB1 is high
        ori     r24, 1			; SELECT

#else
#if CONFIG_HARDWARE_VARIANT == HW_LARSP

        clr     r24
        sbis    _SFR_IO_ADDR(PINA), 5 	; skip next if PB3 is high
        ori     r24, 2			; NEXT
        sbis    _SFR_IO_ADDR(PINA), 4 	; skip next if PA4 is high
        ori     r24, 1			; SELECT

#else

#error Missing readbuttons.inc definition for your hardware variant
#endif
#endif
#endif
