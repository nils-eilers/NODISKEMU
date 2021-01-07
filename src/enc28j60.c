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


#include <stdbool.h>
#include <stdint.h>

#include "spi.h"
#include "uart.h"
#include "enc28j60.h"


static uint8_t enc28j60_bank;


static uint8_t enc28j60_rd(uint8_t op, uint8_t address) {
  uint8_t data;

  enc28j60_set_ss(0);
  spi_tx_byte(op | (address & ADDR_MASK));      // send read cmd
  data = spi_rx_byte();                         // receive data byte
  if (address & 0x80) data = spi_rx_byte();
  enc28j60_set_ss(1);
  return data;
}


static void enc28j60_wr(uint8_t op, uint8_t address, uint8_t data) {
  enc28j60_set_ss(0);
  spi_tx_byte(op | (address & ADDR_MASK));      // send write cmd
  spi_tx_byte(data);                            // send data byte
  enc28j60_set_ss(1);
}


static void enc28j60_set_bank(uint8_t address) {
  // set the bank (if needed)
  if((address & BANK_MASK) != enc28j60_bank) {
    enc28j60_wr(ENC28J60_BIT_FIELD_CLR, ECON1, (ECON1_BSEL1|ECON1_BSEL0));
    enc28j60_wr(ENC28J60_BIT_FIELD_SET, ECON1, (address & BANK_MASK)>>5);
    enc28j60_bank = (address & BANK_MASK);
  }
}


uint8_t enc28j60_read(uint8_t address) {
  enc28j60_set_bank(address);
  return enc28j60_rd(ENC28J60_READ_CTRL_REG, address);
}
