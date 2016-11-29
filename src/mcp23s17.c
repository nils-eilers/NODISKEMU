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

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "mcp23s17.h"
#include "spi.h"


void
mcp23s17_Init()
{
   /* IOCON:
      Bit 7 BANK:   0=Registers in same bank, addresses sequential
      Bit 6 MIRROR: 0=INT pins not connected (unconnected here anyway)
      Bit 5 SEQOP:  0=sequential operation disabled, addr ptr doesn't inc
      Bit 4 DISSLW: 1=SDA slew rate disabled
      Bit 3 HAEN:   1=addr pins enabled (tied to 0 in hw, same as disabled)
      Bit 2 ODR:    0=INT as open-drain output (int pin is unconnected)
      Bit 1 INTPOL: 0=polarity when INT configured as active driver
      Bit 0: unimplemented (read as '0')
    */
   mcp23s17_Write(IOCON,  0x18);
   mcp23s17_Write(IODIRA, 0xFF);  // GPA0-7 as input
   mcp23s17_Write(GPPUA,  0xFF);  // enable pull-ups for Port A
   mcp23s17_Write(IPOLA,  0xFF);  // Enable inverted polarity for read data
   mcp23s17_Write(IODIRB, 0x0F);  // GPB0-3 as input, GPB4-7 as output
   mcp23s17_Write(GPPUB,  0x0F);  // enable pull-ups for GPB0-3
}


void
mcp23s17_Write(uint8_t Register, uint8_t Value)
{
   spi_select_mcp23s17(true);
   spi_tx_byte(MCP23S17_WR_ADDR_0);
   spi_tx_byte(Register);
   spi_tx_byte(Value);
   spi_select_mcp23s17(false);
}


uint8_t
mcp23s17_Read(uint8_t Register)
{
   spi_select_mcp23s17(true);
   spi_tx_byte(MCP23S17_RD_ADDR_0);
   spi_tx_byte(Register);
   uint8_t Value = spi_rx_byte();
   spi_select_mcp23s17(false);
   return Value;
}


void
mcp23s17_SetBit(uint8_t Bit)
{
   uint8_t Port = (Bit & 8) ? OLATB : OLATA;
   uint8_t Value  = mcp23s17_Read(Port);
   Value |= (1 << (Bit & 7));
   mcp23s17_Write (Port, Value);
}


void
mcp23s17_ClearBit(uint8_t Bit)
{
   uint8_t Port = (Bit & 8) ? OLATB : OLATA;
   uint8_t Value  = mcp23s17_Read(Port);
   Value &= ~(1 << (Bit & 7));
   mcp23s17_Write (Port, Value);
}
