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

#pragma once

#include <stdint.h>

#define MCP23S17_WR_ADDR_0   0x40
#define MCP23S17_RD_ADDR_0   0x41

enum
{
   GPA0, GPA1, GPA2, GPA3, GPA4, GPA5, GPA6, GPA7,
   GPB0, GPB1, GPB2, GPB3, GPB4, GPB5, GPB6, GPB7
};


enum
{
   IODIRA,        // Data Direction Register for PORTA
   IODIRB,        // Data Direction Register for PORTB
   IPOLA,         // Input Polarity Register for PORTA
   IPOLB,         // Input Polarity Register for PORTB
   GPINTENA,      // Interrupt-on-change enable Register for PORTA
   GPINTENB,      // Interrupt-on-change enable Register for PORTB
   DEFVALA,       // Default Value Register for PORTA
   DEFVALB,       // Default Value Register for PORTB
   INTCONA,       // Interrupt-on-change control Register for PORTA
   INTCONB,       // Interrupt-on-change control Register for PORTB
   IOCON,         // Configuration register for device
   IOCON2,        // Configuration register for device (mirror of IOCON)
   GPPUA,         // 100kOhm pullup resistor register for PORTA
   GPPUB,         // 100kOhm pullup resistor register for PORTB
   INTFA,         // Interrupt flag Register for PORTA
   INTFB,         // Interrupt flag Register for PORTB
   INTCAPA,       // Interrupt captured value Register for PORTA
   INTCAPB,       // Interrupt captured value Register for PORTB
   GPIOA,         // General purpose I/O Register for PORTA
   GPIOB,         // General purpose I/O Register for PORTB
   OLATA,         // Output latch Register for PORTA
   OLATB          // Output latch Register for PORTB
};

void    mcp23s17_Init(void);
uint8_t mcp23s17_Read(uint8_t reg);
void    mcp23s17_Write(uint8_t reg, uint8_t val);
void    mcp23s17_SetBit(uint8_t bit);
void    mcp23s17_ClearBit(uint8_t bit);
