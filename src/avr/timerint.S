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


; TODO: increment ms/rtc for softrtc configuration


#include <avr/io.h>
#include <avr/interrupt.h>
#include "asmconfig.h"

.global TIMER1_COMPA_vect

.extern ticks

.extern led_state

; Variables shared with higher level key functions
.extern key_state
.extern key_press
.extern key_rpt

; Variables used only by interrupt service routine
; 8 vertical three bits counters counting from 0 to 8
; for debouncing of up to 8 buttons
.extern key_ct0                 ; lsb
.extern key_ct1
.extern key_ct2                 ; msb
; tick counter for auto-repeat function
.extern key_repeat_counter

#define REPEAT_MASK     0b110  /* Enable repeat function for PREV and NEXT */
#define REPEAT_START    50     /* Start repeat function after 500 ms       */
#define REPEAT_NEXT     15     /* Wait 150 ms between repeated keys        */

TIMER1_COMPA_vect:


; Enable interrupts ASAP again, esp. for ATN
        sei

; Save registers

        push    r0
        in      r0, 0x3f                ; save SREG
        push    r24
        push    r25
        push    r26
        push    r27
        push    r28
        push    r29

; Increment tick counter
        lds     r24, ticks              ; r24: lsb ticks
        lds     r25, ticks+1
        adiw    r24, 1
        sts     ticks, r24
        sts     ticks+1, r25

; Flash error LED?
        lds     r25, led_state          ; Execute this section only if
        sbrs    r25, 0                  ; led_state's bit 0 is set
        rjmp    L_noerror

        andi    r24, 0x0F               ; lsb ticks & 15
        brne    L_noerror

; toggle dirty led
        sbi _SFR_IO_ADDR(LED_DIRTY_INPUT), LED_DIRTY_PIN

L_noerror:


; Debounce buttons
; Device depended code in readbuttons.inc loads current button state to r24
; Bit 0: button released, Bit 1: button pressed
#include "readbuttons.inc"
        lds     r25, key_state
        eor     r24, r25                ; r24: delta=key_state ^ key_sample

        lds     r26, key_ct0            ; Increment counter for pressed buttons
        lds     r27, key_ct1
        lds     r28, key_ct2
        mov     r25, r28
        and     r25, r27
        eor     r28, r25                ; ct2 ^= ct1 & ct0
        and     r28, r24                ; ct2 &= delta
        sts     key_ct2, r28
        eor     r27, r26                ; ct1 ^= ct0
        and     r27, r24                ; ct1 &= delta
        sts     key_ct1, r27
        com     r26                     ; ct0 = ~ct0
        and     r26, r24                ; ct0 &= delta
        sts     key_ct0, r26
        or      r26, r27
        or      r26, r28
        eor     r26, r24                ; r26: toggle=delta ^ (ct0 | ct1 | ct2)
        lds     r25, key_state
        eor     r25, r26
        sts     key_state, r25          ; store debounced current button state
        mov     r27, r25

        and     r25, r24                ; store pressed keys
        and     r25, r26                ; flags are set by ISR and reset by
        lds     r26, key_press          ; C functions
        or      r26, r25
        sts     key_press, r26

        andi    r27, REPEAT_MASK
        brne    L_no_repeat             ; check repeat function
        ldi     r25, REPEAT_START       ; start delay
        sts     key_repeat_counter, r25
L_no_repeat:
        lds     r25, key_repeat_counter ; repeat delay
        dec     r25
        sts     key_repeat_counter, r25
        brne    L_key_done
        ldi     r24, REPEAT_NEXT
        sts     key_repeat_counter, r24
        lds     r24, key_rpt            ; set flags for buttons that were pressed
        or      r24, r27                ; for a longer while
        sts     key_rpt, r24            ; flags are reset by C functions
L_key_done:

; Restore registers
L_quit:
        pop     r29
        pop     r28
        pop     r27
        pop     r26
        pop     r25
        pop     r24
        out     0x3f, r0                ; restore SREG
        pop     r0
        reti
