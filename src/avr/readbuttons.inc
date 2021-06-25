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

; Read ADC value and reduce value to 8 bits
        lds     r24, ADCL       ; read ADC value, LSB first!
        lds     r25, ADCH
        subi    r24, lo8(550)   ; reduce value to 8 bits:
        sbci    r25, hi8(550)   ; r24 = (ADCW-550) >> 1
        lsr     r25
        ror     r24

; Start next ADC conversion for next iteration
        lds     r25, ADCSRA
        ori     r25, _BV(ADSC)
        sts     ADCSRA, r25

        cpi     r24, 25         ; no button pressed?
        brcs    L_no_button
        cpi     r24, 40         ; PREV
        brcs    L_PREV
        cpi     r24, 63         ; NEXT
        brcs    L_NEXT
        cpi     r24, 94         ; PN
        brcs    L_PN
        cpi     r24, 129        ; SELECT
        brcs    L_SEL
        cpi     r24, 168        ; PS
        brcs    L_PS
        cpi     r24, 214        ; NS
        brcs    L_NS
        ldi     r24, 0b00000111 ; PNS
        rjmp    L_converted

L_NS:   ldi     r24, 0b00000011
        rjmp    L_converted
L_PS:   ldi     r24, 0b00000101
        rjmp    L_converted
L_SEL:  ldi     r24, 0b00000001
        rjmp    L_converted
L_PN:   ldi     r24, 0b00000110
        rjmp    L_converted
L_NEXT: ldi     r24, 0b00000010
        rjmp    L_converted
L_PREV: ldi     r24, 0b00000100
        rjmp    L_converted
L_no_button:
        clr     r24
L_converted:

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
