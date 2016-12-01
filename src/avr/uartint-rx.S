/*-
    Copyright (C) 2012 Andre Fachat,
    Copyright (C) 2015, 2016 Nils Eilers

    This file is derived from XD-2031 by Andre Fachat, taken from
    XD2031/firmware/avr/uartints.S licensed unter GPL version 2 only.

    The original copyright header follows below:

*****************************************************************************
    XD-2031 - Serial line filesystem server for CBMs
    Copyright (C) 2012 Andre Fachat

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation;
    version 2 of the License ONLY.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

****************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>
#include "asmconfig.h"

#ifndef CONFIG_UART_RX_BUFFER_SIZE
#define CONFIG_UART_RX_BUFFER_SIZE 128
#endif

#ifdef CONFIG_SPSP
.extern spsp_LastRxChar
.extern spsp_EscEotCounter
.extern spsp_Connect
.extern system_reset

#define EOT     0x04
#define ENQ     0x05
#define ESC     0x1B
#define FS      0x1C

#define MIN_ESC_EOT 4

#endif


#if (!(defined(__AVR_ATmega644__)    \
    || defined(__AVR_ATmega644P__)   \
    || defined(__AVR_ATmega1284P__) ))
#error Usage of UART registers could be inappropiate
#endif


#define RX_BUFFER_SIZE_MASK (CONFIG_UART_RX_BUFFER_SIZE - 1)
#if (CONFIG_UART_RX_BUFFER_SIZE & RX_BUFFER_SIZE_MASK)
#error RX buffer size is not a power of 2
#endif

#define TX_BUFFER_SIZE_MASK (CONFIG_UART_TX_BUFFER_SIZE - 1)
#if (CONFIG_UART_TX_BUFFER_SIZE & TX_BUFFER_SIZE_MASK)
#error TX buffer size is not a power of 2
#endif

.extern uart_rxbuf
.extern uart_rx_wp
.extern uart_rx_rp
.extern rx_buffer_overflows


; TODO: support BUFSIZES > 256 bytes
#if ((CONFIG_RX_BUFFER_SIZE > 256) || (CONFIG_TX_BUFFER_SIZE > 256))
#error buffer size too large
#endif

;------------------------------------------------------------------------------
; UART receive interrupt
.global USART0_RX_vect                  ;                         cycles in IRQ


USART0_RX_vect:
        push    r2                      ; make place for status register      2
        in      r2, _SFR_IO_ADDR(SREG)  ; save status register                1
        push    r16                     ; save working register               2

        ; first part of interrupt routine

        lds     r16, UDR0               ; load data from receive register     2

        ; now we have cleared the interrupt signal
        ; next receive signal is in 1215 cycles
        ; 115200 bps is 11520 bytes per second, i.e. 87us per byte,
        ; i.e. 1215 cycles per byte
        ; so we do not run into this routine again soon, even with
        ; interrupts in between

        sei                              ; allow other nested interrupts,
                                         ; esp. ATN                           1

        push    r17                      ; save working byte                  2
        push    YL                       ; save working register Y low       ===
        push    YH                       ; save working register Y high      10

#ifdef CONFIG_SPSP

; Check ESC/EOT received

        cpi     r16, EOT
        brne    NoEscEot
        lds     r17, spsp_LastRxChar
        cpi     r17, ESC
        brne    NoEscEot
        lds     r17, spsp_EscEotCounter  ; ESC EOT received --> increment counter
        inc     r17
        sts     spsp_EscEotCounter, r17
        rjmp    StoreChar

NoEscEot:
        cpi     r16, ENQ
        brne    ResetEscEotCounter
        lds     r17, spsp_EscEotCounter
        cpi     r17, MIN_ESC_EOT
        brlo    ResetEscEotCounter

; We received a sequence of ESC EOT for at least MIN_ESC_EOT times followed
; by ENQ. This is the magic string that forces the devices to connect to
; the server:
        clr     r17
        sts     uart_rx_rp, r17          ; clear receive buffer
        sts     uart_rx_wp, r17
        call spsp_Connect                ; call C function to connect with server
        rjmp system_reset                ; connect failed, restart system

ResetEscEotCounter:
        clr     r17
        sts     spsp_EscEotCounter, r17

StoreChar:
        sts     spsp_LastRxChar, r16

#endif /* CONFIG_SPSP */

        lds     r17, uart_rx_wp          ; receive write pointer
        ldi     YL, lo8(uart_rxbuf)      ; compute address of actual byte
        ldi     YH, hi8(uart_rxbuf)      ; as uart_rxbuf plus read pointer

        add     YL, r17                  ; compute uart_rxbuf[uart_rx_wp] addr
        brcc    rxnocarry
        inc     YH
rxnocarry:
        st      Y, r16                   ; store byte in receive buffer

        pop     YH                       ; don't need Y anymore,
        pop     YL                       ; release some stack space

        ; now increment uart_rx_wp, which is also still in r17

        inc     r17
        andi    r17, RX_BUFFER_SIZE_MASK

        ; compare with uart_rx_wp to check for overflow

        lds     r16, uart_rx_rp          ; read pointer
        cp      r17, r16                 ; compare
        breq    rxoverflow               ; branch on overflow

        sts     uart_rx_wp, r17          ; store updated uart_rx_wp

        brne    rxend                    ; always

        ; set error flag on overflow
rxoverflow:
        lds     r16, rx_buffer_overflows
        inc     r16
        sts     rx_buffer_overflows, r16

        ; end
rxend:
        pop     r17                       ; restore working registers
        pop     r16
        out     _SFR_IO_ADDR(SREG), r2
        pop     r2
        reti

.end
