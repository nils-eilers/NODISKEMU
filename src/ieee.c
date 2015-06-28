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

// If nonzero, output all bus data
#define DEBUG_BUS_DATA 0

#include "config.h"

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "uart.h"
#include "buffers.h"
#include "d64ops.h"
#include "diskchange.h"
#include "diskio.h"
#include "doscmd.h"
#include "fatops.h"
#include "fileops.h"
#include "filesystem.h"
#include "led.h"
#include "ieee.h"
#include "fastloader.h"
#include "errormsg.h"
#include "ctype.h"
#include "display.h"
#include "system.h"

// -------------------------------------------------------------------------
//  Global variables
// -------------------------------------------------------------------------

uint8_t detected_loader = FL_NONE;      // Workaround serial fastloader
uint8_t device_address;                 // Current device address
volatile bool ieee488_TE75160;          // direction set for data lines
volatile bool ieee488_TE75161;          // direction set for ctrl lines


#define PRESERVE_CURRENT_DIRECTORY      1
#define CHANGE_TO_ROOT_DIRECTORY        0

#define IEEE_EOI        0x100
#define IEEE_NO_DATA    0x200

#define TE_LISTEN       0
#define TE_TALK         1

#define DC_BUSMASTER    0
#define DC_DEVICE       1




// ieee488_RxByte return values:
enum {
  RX_DATA,                              // byte received, EOI not set
  RX_EOI,                               // byte received, EOI set
  RX_ATN                                // aborted by ATN
};

// listen_loop actions:
enum {
  LL_RECEIVE,                           // received data or command byte
  LL_OPEN,                              // received character for OPEN Filename
};


void    ieee488_Init(void);
void    ieee488_MainLoop(void);
uint8_t ieee488_ListenIsActive(void);
uint8_t ieee488_RxByte(char *c);
void    ieee488_BusIdle(void);
void    ieee488_CtrlPortsTalk(void);    // Switch bus driver to talk mode
void    ieee488_CtrlPortsListen(void);  // Switch bus driver to listen mode

static inline void ieee488_SetEOI(bool x);
static inline void ieee488_SetDAV(bool x);
static inline void ieee488_SetNDAC(bool x);
static inline void ieee488_SetNRFD(bool x);

static inline uint8_t ieee488_ATN(void) {
  return IEEE_INPUT_ATN & _BV(IEEE_PIN_ATN);
}


static inline uint8_t ieee488_NDAC(void) {
  return IEEE_INPUT_NDAC & _BV(IEEE_PIN_NDAC);
}


static inline uint8_t ieee488_NRFD(void) {
  return IEEE_INPUT_NRFD & _BV(IEEE_PIN_NRFD);
}


static inline uint8_t ieee488_DAV(void) {
  return IEEE_INPUT_DAV & _BV(IEEE_PIN_DAV);
}


static inline uint8_t ieee488_EOI(void) {
  return IEEE_INPUT_EOI & _BV(IEEE_PIN_EOI);
}

static inline void ieee488_SetEOI(bool x) {
  // bus driver changes flow direction of EOI from transmit
  // to receive on ATN, so simulate OC output here
  if (x) {
    IEEE_PORT_EOI |=  _BV(IEEE_PIN_EOI);        // enable pull-up for EOI
    IEEE_DDR_EOI  &= ~_BV(IEEE_PIN_EOI);        // EOI as input
  } else {
    IEEE_PORT_EOI &= ~_BV(IEEE_PIN_EOI);        // EOI = 0
    IEEE_DDR_EOI  |=  _BV(IEEE_PIN_EOI);        // EOI as output
  }
}


#ifdef HAVE_7516X
// Device with 75160/75161 bus drivers

#ifdef IEEE_PIN_D7

static inline void ieee488_SetData(uint8_t data) {
  IEEE_D_PORT &= 0b10000000;
  IEEE_D_PORT |= (~data) & 0b01111111;
  if (data & 128) IEEE_PORT_D7 &= ~_BV(IEEE_PIN_D7);
  else            IEEE_PORT_D7 |=  _BV(IEEE_PIN_D7);
}

static inline uint8_t ieee488_Data(void) {
   uint8_t data = IEEE_D_PIN;
   if (bit_is_set(IEEE_INPUT_D7, IEEE_PIN_D7))
      data |= 0b10000000;
   else
      data &= 0b01111111;
   return ~data;
}

static inline void ieee488_DataListen(void) {
  // This code assumes that D7 on this port is also used as input!
  IEEE_D_DDR  = 0x00;                   // data lines as input
  IEEE_DDR_D7 &= _BV(IEEE_PIN_D7);
  ieee488_TE75160 = TE_LISTEN;
}


static inline void ieee488_DataTalk(void) {
  IEEE_D_PORT = 0b01111111;             // release data lines
  IEEE_D_DDR  = 0b01111111;             // data lines as output
  IEEE_PORT_D7 |= _BV(IEEE_PIN_D7);
  IEEE_DDR_D7  |= _BV(IEEE_PIN_D7);
  ieee488_TE75160 = TE_TALK;
}
#else
#ifdef IEEE_D_DDR
static inline uint8_t ieee488_Data(void) {
  return ~IEEE_D_PIN;
}


static inline void ieee488_SetData(uint8_t data) {
  IEEE_D_PORT = ~data;
}


static inline void ieee488_DataListen(void) {
  IEEE_D_DDR  = 0x00;                   // data lines as input
  ieee488_TE75160 = TE_LISTEN;
}


static inline void ieee488_DataTalk(void) {
  IEEE_D_PORT = 0xFF;                   // release data lines
  IEEE_D_DDR  = 0xFF;                   // data lines as output
  ieee488_TE75160 = TE_TALK;
}
#else
#ifdef IEEE_DATA_READ
#include "MCP23S17.h"

static inline void ieee488_DataListen(void) {
  mcp23s17_Write(IEEE_DDR_DATA, 0xFF);  // data lines as input
  IEEE_PORT_TED &= ~_BV(IEEE_PIN_TED);  // TED=0 (listen)
  ieee488_TE75160 = TE_LISTEN;
}


static inline void ieee488_DataTalk(void) {
  IEEE_PORT_TED |= _BV(IEEE_PIN_TED);   // TED=1 (talk)
  mcp23s17_Write(IEEE_DDR_DATA, 0x00);  // data lines as output
  ieee488_TE75160 = TE_TALK;
}


static inline uint8_t ieee488_Data(void) {
  // MCP23S17 configured for inverted polarity input (IPOL),
  // so no need to invert here
  return mcp23s17_Read(IEEE_DATA_READ);
}


static inline void ieee488_SetData(uint8_t data) {
  mcp23s17_Write(IEEE_DATA_WRITE, ~data);
}

#else
#error ieee488 data functions undefined
#endif
#endif
#endif


#ifdef IEEE_PORT_DC
static inline void ieee488_InitDC(void) {
  IEEE_DDR_DC |= _BV(IEEE_PIN_DC);      // DC as output
}


static inline void ieee488_SetDC(bool x) {
  if (x)
    IEEE_PORT_DC |=  _BV(IEEE_PIN_DC);
  else
    IEEE_PORT_DC &= ~_BV(IEEE_PIN_DC);
}

#else
#ifdef IEEE_DC_MCP23S17
#include "MCP23S17.h"
static inline void ieee488_InitDC(void) {
  // intentionally left blank
  // DC is initialized by mcp23s17_Init()
}


static inline void ieee488_SetDC(bool x) {
  if (x)
    mcp23s17_SetBit(IEEE_DC_MCP23S17);
  else
    mcp23s17_ClearBit(IEEE_DC_MCP23S17);
}
#else
#ifndef IEEE_PORT_DC
static inline void ieee488_InitDC(void) {
   // intentionally left blank
}

static inline void ieee488_SetDC(bool x) {
   // intentionally left blank
}
#else
#error ieee488_SetDC() / ieee488_InitDC undefined
#endif // #ifndef IEEE_PORT_DC
#endif // #ifdef IEEE_DC_MCP23S17
#endif // #ifdef IEEE_PORT_DC


static inline void ieee488_SetNDAC(bool x) {
  if (x)
    IEEE_PORT_NDAC |=  _BV(IEEE_PIN_NDAC);
  else
    IEEE_PORT_NDAC &= ~_BV(IEEE_PIN_NDAC);
}


static inline void ieee488_SetNRFD(bool x) {
  if (x)
    IEEE_PORT_NRFD |=  _BV(IEEE_PIN_NRFD);
  else
    IEEE_PORT_NRFD &= ~_BV(IEEE_PIN_NRFD);
}


static inline void ieee488_SetDAV(bool x) {
  if (x)
    IEEE_PORT_DAV |=  _BV(IEEE_PIN_DAV);
  else
    IEEE_PORT_DAV &= ~_BV(IEEE_PIN_DAV);
}


static inline void ieee488_SetTE(bool x) {
  if (x)
    IEEE_PORT_TE |=  _BV(IEEE_PIN_TE);
  else
    IEEE_PORT_TE &= ~_BV(IEEE_PIN_TE);
}

void ieee488_CtrlPortsListen(void) {
  IEEE_DDR_EOI &= ~_BV(IEEE_PIN_EOI);           // EOI  as input
  IEEE_DDR_DAV &= ~_BV(IEEE_PIN_DAV);           // DAV  as input
  ieee488_SetTE(TE_LISTEN);
  IEEE_DDR_NDAC |= _BV(IEEE_PIN_NDAC);          // NDAC as output
  IEEE_DDR_NRFD |= _BV(IEEE_PIN_NRFD);          // NRFD as output
  ieee488_TE75161 = TE_LISTEN;
}


void ieee488_CtrlPortsTalk(void) {
  IEEE_DDR_NDAC &= ~_BV(IEEE_PIN_NDAC);         // NDAC as input
  IEEE_DDR_NRFD &= ~_BV(IEEE_PIN_NRFD);         // NRFD as input
  IEEE_PORT_DAV |= _BV(IEEE_PIN_DAV);           // DAV high
  IEEE_PORT_EOI |= _BV(IEEE_PIN_EOI);           // EOI high
  ieee488_SetTE(TE_TALK);                       // TE=1 (talk)
  IEEE_DDR_DAV |= _BV(IEEE_PIN_DAV);            // DAV as output
  IEEE_DDR_EOI |= _BV(IEEE_PIN_EOI);            // EOI as output
  ieee488_TE75161 = TE_TALK;
}

#else /* ifdef HAVE_7516X */
#ifdef HAVE_IEEE_POOR_MENS_VARIANT

// -----------------------------------------------------------------------
//  Poor men's variant without IEEE bus drivers
// -----------------------------------------------------------------------
static inline uint8_t ieee488_Data(void) {
  return ~IEEE_D_PIN;
}


static inline void ieee488_SetData(uint8_t data) {
  // Pull lines low by outputting zero
  // pull lines high by defining them as input and enabling the pull-up
  IEEE_D_DDR  = data;
  IEEE_D_PORT = ~data;
}


static inline void ieee488_DataListen(void) {
  IEEE_D_DDR  = 0x00;                           // data lines as input
  IEEE_D_PORT = 0xFF;                           // enable pull-ups
  ieee488_TE75160 = TE_LISTEN;
}


static inline void ieee488_DataTalk(void) {
  IEEE_D_DDR  = 0x00;                           // data lines as input
  IEEE_D_PORT = 0xFF;                           // enable pull-ups
  ieee488_TE75160 = TE_TALK;
}


void ieee488_CtrlPortsListen(void) {
  ieee488_SetEOI(1);
  ieee488_SetDAV(1);
  ieee488_SetNDAC(1);
  ieee488_SetNRFD(1);
  ieee488_TE75161 = TE_LISTEN;
}


void ieee488_CtrlPortsTalk(void) {
  ieee488_SetEOI(1);
  ieee488_SetDAV(1);
  ieee488_SetNDAC(1);
  ieee488_SetNRFD(1);
  ieee488_TE75161 = TE_TALK;
}


static inline void ieee488_SetNDAC(bool x) {
  if(x) {                                       // Set NDAC high
    IEEE_DDR_NDAC &= ~_BV(IEEE_PIN_NDAC);       // NDAC as input
    IEEE_PORT_NDAC |= _BV(IEEE_PIN_NDAC);       // Enable pull-up
  } else {                                      // Set NDAC low
    IEEE_PORT_NDAC &= ~_BV(IEEE_PIN_NDAC);      // NDAC low
    IEEE_DDR_NDAC |= _BV(IEEE_PIN_NDAC);        // NDAC as output
  }
}

static inline void ieee488_SetNRFD(bool x) {
  if(x) {                                       // Set NRFD high
    IEEE_DDR_NRFD &= ~_BV(IEEE_PIN_NRFD);       // NRFD as input
    IEEE_PORT_NRFD |= _BV(IEEE_PIN_NRFD);       // Enable pull-up
  } else {                                      // Set NRFD low
    IEEE_PORT_NRFD &= ~_BV(IEEE_PIN_NRFD);      // NRFD low
    IEEE_DDR_NRFD |= _BV(IEEE_PIN_NRFD);        // NRFD as output
  }
}

static inline void ieee488_SetDAV(bool x) {
  if(x) {                                       // Set DAV high
    IEEE_DDR_DAV &= ~_BV(IEEE_PIN_DAV);         // DAV as input
    IEEE_PORT_DAV |= _BV(IEEE_PIN_DAV);         // Enable pull-up
  } else {                                      // Set DAV low
    IEEE_PORT_DAV &= ~_BV(IEEE_PIN_DAV);        // DAV low
    IEEE_DDR_DAV |= _BV(IEEE_PIN_DAV);          // DAV as output
  }
}

static inline void ieee488_SetTE(bool x) {
  // left intentionally blank
}

static inline void ieee488_InitDC(void) {
  // left intentionally blank
}

static inline void ieee488_SetDC(bool x) {
  // left intentionally blank
}

#else
#error No IEEE-488 low level routines defined
#endif
#endif

#ifdef IEEE_ATN_INT0                    // ATN via INT0 interrupt
static inline void ieee488_EnableAtnInterrupt(void) {
  EIMSK |= _BV(INT0);
}


static inline void ieee488_DisableAtnInterrupt(void) {
  EIMSK &= ~_BV(INT0);
}


static inline void ieee488_InitAtnInterrupt(void) {
  IEEE_DDR_ATN &= ~_BV(IEEE_PIN_ATN);   // ATN as input
  IEEE_PORT_ATN |= _BV(IEEE_PIN_ATN);   // Enable pull-up for ATN
  ieee488_DisableAtnInterrupt();
  EICRA &= ~_BV(ISC00);                 // interrupt on falling edge of ATN
  EICRA |=  _BV(ISC01);
  ieee488_EnableAtnInterrupt();
}
#else
#ifdef IEEE_PCMSK
static inline void ieee488_InitAtnInterrupt(void) {
  PCIFR |= _BV(PCIF3);                  // clear interrupt flag
  IEEE_PCMSK |= _BV(IEEE_PCINT);        // enable ATN in pin change enable mask
  PCICR |= _BV(PCIE3);                  // enable pin change interrupt 3 (PCINT31..24)
}
#else
#error No interrupt definition for ATN
#endif
#endif


// TE=0: listen mode, TE=1: talk mode
volatile bool ieee488_TE75160; // bus driver for data lines
volatile bool ieee488_TE75161; // bus driver for control lines

void ieee488_BusIdle(void) {
  if (ieee488_TE75161 != TE_LISTEN)             // Assert listen mode
    ieee488_CtrlPortsListen();
  ieee488_SetNDAC(1);                           // NDAC high
  ieee488_SetNRFD(1);                           // NRFD high
  if (ieee488_TE75160 != TE_LISTEN)
    ieee488_DataListen();
}

/* Please note that the init-code is spread across two functions:
   ieee488_Init() follows below, but in src/avr/arch-config.h there
   is ieee_interface_init() also, which is aliased to bus_interface_init()
   there and called as such from main().
*/
void ieee488_Init(void) {
  device_hw_address_init();
  device_address = device_hw_address();
  ieee488_InitDC();
  ieee488_SetDC(DC_DEVICE);
#ifdef HAVE_7516X
  IEEE_DDR_TE  |= _BV(IEEE_PIN_TE);             // TE  as output
#endif
  IEEE_DDR_ATN &= ~_BV(IEEE_PIN_ATN);           // ATN as input
  ieee488_DataListen();
  ieee488_CtrlPortsListen();
  ieee488_BusIdle();
  ieee488_InitAtnInterrupt();
}

void bus_init(void) __attribute__((weak, alias("ieee488_Init")));

// Upper three bit commands with attached device number
#define IEEE_LISTEN      0x20    // 0x20 - 0x3E
#define IEEE_UNLISTEN    0x3F
#define IEEE_TALK        0x40    // 0x40 - 0x5E
#define IEEE_UNTALK      0x5F

// Upper four bit commands with attached secondary address
#define IEEE_SECONDARY   0x60
#define IEEE_CLOSE       0xE0
#define IEEE_OPEN        0xF0


uint8_t ieee488_ListenActive;     // device number
uint8_t ieee488_TalkingDevice;    // device number if we are talker
bool    command_received;
bool    open_active;
uint8_t open_sa;


uint8_t ieee488_RxByte(char *c) {
  uint8_t eoi = RX_DATA;

  do {
    ieee488_SetNRFD(1);
    ieee488_SetNDAC(0);
    if (ieee488_TE75160 != TE_LISTEN)
      ieee488_DataListen();
    do {
      if (!ieee488_ATN())
        return RX_ATN;                  // ATN became low, abort
    } while (ieee488_DAV());            // Wait for DAV low
    // DAV is now low, NDAC must be high in max. 64 ms
    ieee488_SetNRFD(0);
    if (!ieee488_EOI())
      eoi = RX_EOI;
    *c = ieee488_Data();
  } while (ieee488_DAV());              // If DAV is high again, we've
                                        // seen only a glitch
  ieee488_SetNDAC(1);
  do {
    if (!ieee488_ATN())
      return RX_ATN;                    // ATN became low, abort
  }
  while (!ieee488_DAV());               // wait for DAV high
  ieee488_SetNDAC(0);
  return eoi;
}


void RxChar(char c) {
  // Receive commands and filenames
  if (command_length < CONFIG_COMMAND_BUFFER_SIZE)
    command_buffer[command_length++] = c;
}



static void ieee488_IgnoreBytes(void) {
  // Fetch and ignore bytes in case of errors while saving
  uint8_t BusSignals;
  char c;

  uart_puts_P(PSTR("Ignoring data\n"));
  do {
    BusSignals = ieee488_RxByte(&c);
  } while (BusSignals != RX_ATN);
}


void ieee488_ListenLoop(uint8_t action, uint8_t sa) {
  char    c;
  uint8_t BusSignals;
  buffer_t *buf;

  buf = find_buffer(sa);
  // Abort if there is no buffer or it's not open for writing
  // and it isn't an OPEN command
  if ((buf == NULL || !buf->write) && (action != LL_OPEN)) {
    uart_puts_P(PSTR("LLabort\n"));
    ieee488_IgnoreBytes();
    return;
  }

  if (sa == 15)
    command_received = true;

  printf("LL %d\r\n", sa);

  for (;;) {
    BusSignals = ieee488_RxByte(&c);  // Read byte from IEEE bus

    if (BusSignals == RX_ATN) return; // ATN received, abort

    if (action == LL_OPEN || command_received) {
      RxChar(c);
      continue;
    }

    // Flush buffer if full
    if (buf->mustflush) {
      if (buf->refill(buf)) {
        uart_puts_P(PSTR("refill abort\r\n"));
        ieee488_IgnoreBytes();
        return;
      }
      // Search the buffer again,
      // it can change when using large buffers
      buf = find_buffer(sa);
    }

    buf->data[buf->position] = c;
    mark_buffer_dirty(buf);

#if DEBUG_BUS_DATA
    uart_puthex(c); uart_putc(' ');
#endif

    if (buf->lastused < buf->position) buf->lastused = buf->position;
    buf->position++;

    // Mark buffer for flushing if position wrapped
    if (buf->position == 0) buf->mustflush = 1;

    // REL files must be syncronized on EOI
    if (buf->recordlen && BusSignals == RX_EOI) {
      if (buf->refill(buf)) {
        uart_puts_P(PSTR("refill abort2\r\n"));
        ieee488_IgnoreBytes();
        return;
      }
    }
  }
}


void ieee488_TalkLoop(uint8_t sa) {
  int  c;
  bool LastByte;
  buffer_t *buf;

  // This function returns immediately on ATN low.
  // The ATN interrupt routine will handle the IEEE ports then
  // and here's nothing left to do.

  buf = find_buffer(sa);
  if (buf == NULL) {
    uart_puts_P(PSTR("T0\r\n"));
    return;
  }

  ieee488_CtrlPortsTalk();              // Set hardware to TALK mode

  while (buf->read) {
    do {
      ieee488_SetDAV(1);                // Release DAV and EOI
      ieee488_SetEOI(1);
      while (ieee488_NDAC())            // Wait for NDAC low
        if (!ieee488_ATN()) {
          uart_puts_P(PSTR("T1\r\n"));
          return;
        }
      while (!ieee488_NRFD())           // Wait for NRFD high
        if (!ieee488_ATN()) {
          uart_puts_P(PSTR("T2\r\n"));
          return;
        }

      // Fetch preloaded byte within less than 64 ms
      LastByte = (buf->position == buf->lastused);
      c = buf->data[buf->position];

      if (ieee488_NDAC() || !ieee488_ATN()) {   // NDAC must stay low
        uart_puts_P(PSTR("T3\r\n"));
        return;
      }

      ieee488_SetEOI(LastByte && buf->sendeoi ? 0 : 1);

      if (ieee488_TE75160 != TE_TALK)
        ieee488_DataTalk();
      ieee488_SetData(c);
      if (ieee488_NDAC() || !ieee488_ATN()) {
        uart_puts_P(PSTR("T4\r\n"));
        return;
      }
      ieee488_SetDAV(0);                // Say data valid

      // Wait for NRFD low, NDAC must stay low
      while (ieee488_NRFD())
        if (ieee488_NDAC() || !ieee488_ATN()) {
          ieee488_SetDAV(1);
          ieee488_SetEOI(1);            // Release DAV and EOI
          uart_puts_P(PSTR("T5\r\n"));
          return;
        }

      while (!ieee488_NDAC())           // Wait for NDAC high
        if (!ieee488_ATN()) {
          ieee488_SetDAV(1);
          ieee488_SetEOI(1);            // Release DAV and EOI
          uart_puts_P(PSTR("T6\r\n"));
          return;
        }

      // Listeners have received our byte

#if DEBUG_BUS_DATA
      uart_puthex(c); uart_putc(' ');
#endif

    } while (buf->position++ < buf->lastused);

    // PET/CBM-II wait here without timeout until DAV=1
    // Perfect for flushing buffers without hurry

    uart_puts_P(PSTR("T7\r\n"));

    if (buf->sendeoi && sa != 15 && !buf->recordlen &&
        buf->refill != directbuffer_refill) {
      buf->read = 0;
      ieee488_SetDAV(1);
      ieee488_SetEOI(1);                // Release DAV and EOI
      uart_puts_P(PSTR("T8\r\n"));
      break;
    }

    if (buf->refill(buf)) {             // Refill buffer
      ieee488_SetDAV(1);
      ieee488_SetEOI(1);                // Release DAV and EOI
      uart_puts_P(PSTR("T9\r\n"));
      return;
    }

    // Search the buffer again, it can change when using large buffers
    buf = find_buffer(sa);
  }
  ieee488_BusIdle();
  uart_puts_P(PSTR("TA\r\n"));
}


void ieee488_Unlisten(void) {
  ieee488_ListenActive = false;
  // If we received a command or a file name to open, process it now
  uart_puts_P(PSTR("ULN\r\n"));

  if (command_received) {
    command_received = open_active = false;
    parse_doscommand();
  } else if (open_active) {
    command_received = open_active = false;
    datacrc = 0xffff;                   // filename in command buffer
    file_open(open_sa);
  }

  command_length = 0;
}


void ieee488_Untalk(void) {
  uart_puts_P(PSTR("UTK\r\n"));
  ieee488_TalkingDevice = 0;            // we don't talk any more
}


void ieee488_Handler(void) {
  uint8_t cmd, cmd3, cmd4;              // Received IEEE-488 command byte
  uint8_t Device;                       // device number from cmd byte
  uint8_t sa;                           // secondary address from cmd byte

  //if (!(ieee488_ListenActive || ieee488_TalkingDevice))
  //   set_busy_led(0);

  if (ieee488_ATN())
    return;                             // Wait for ATN low

  // ATN interrupt routine switched to LISTEN mode, released NDAC
  // and pulled NRFD low. We can wait here any time long until we
  // release NRFD.

  ieee488_SetNDAC(0);
  ieee488_SetNRFD(1);                   // Say ready for data
  if (ieee488_TE75160 != TE_LISTEN) ieee488_DataListen();
  while (ieee488_DAV())                 // Wait for DAV low
    if (ieee488_ATN())
      return;
  ieee488_SetNRFD(0);                   // Say not ready for data
  cmd = ieee488_Data();
  ieee488_SetNDAC(1);                   // Say data accepted
  while (!ieee488_DAV());               // Wait for DAV high

  cmd3   = cmd & 0b11100000;
  cmd4   = cmd & 0b11110000;
  Device = cmd & 0b00011111;
  sa     = cmd & 0b00001111;

  uart_puthex(cmd); uart_putc(' ');

  if (cmd == IEEE_UNLISTEN)             // UNLISTEN
    ieee488_Unlisten();
  else if (cmd == IEEE_UNTALK)          // UNTALK
    ieee488_Untalk();
  else if (cmd3 == IEEE_LISTEN) {       // LISTEN
    if (Device == device_address) {
      uart_puts_P(PSTR("LSN\r\n"));
      ieee488_ListenActive = Device;
      //set_busy_led(1);
    }
  } else if (cmd3 == IEEE_TALK) {       // TALK
    if (Device == device_address) {
      uart_puts_P(PSTR("TLK\r\n"));
      ieee488_TalkingDevice = Device;
      //set_busy_led(1);
    }
  } else if (cmd4 == IEEE_SECONDARY) {  // DATA
    while (!ieee488_ATN());             // Wait for ATN high
    if (ieee488_ListenActive) {
      printf("DTA L %d\r\n", sa);
      ieee488_ListenLoop(LL_RECEIVE, sa);
    } else if (ieee488_TalkingDevice) {
      printf("DTA T %d\r\n", sa);
      ieee488_TalkLoop(sa);
    }
  } else if (cmd4 == IEEE_CLOSE) {      // CLOSE
    if (ieee488_ListenActive) {
      printf("CLO %d\r\n", sa);
      if (sa == 15)
        free_multiple_buffers(FMB_USER_CLEAN);
      else {
        buffer_t *buf;
        buf = find_buffer(sa);
        if (buf != NULL) {
          buf->cleanup(buf);
          free_buffer(buf);
        }
      }
    }
  } else if (cmd4 == IEEE_OPEN) {       // OPEN
    while (!ieee488_ATN());             // Wait for ATN high
    if (ieee488_ListenActive) {
      printf("OPN %d\r\n", sa);
      open_active = true;
      open_sa = sa;
      ieee488_ListenLoop(LL_OPEN, sa);
    }
  } else uart_puts_P(PSTR("UKN\r\n"));
}


void handle_card_changes(void) {
#ifdef HAVE_HOTPLUG
  if (disk_state != DISK_OK) {
    set_busy_led(1);
    // If the disk was changed the buffer contents are useless
    if (disk_state == DISK_CHANGED || disk_state == DISK_REMOVED) {
      free_multiple_buffers(FMB_ALL);
      change_init();
      filesystem_init(CHANGE_TO_ROOT_DIRECTORY);
    } else {
      // Disk state indicated an error, try to recover by initialising
      filesystem_init(PRESERVE_CURRENT_DIRECTORY);
    }
  }
#endif
}


void ieee_mainloop(void) {
  set_error(ERROR_DOSVERSION);
  for (;;) {
    ieee488_Handler();
    handle_card_changes();
    // We are allowed to do here whatever we want for any time long
    // as long as the ATN interrupt stays enabled
    // TODO: handle user input / LCD
  }
}


void bus_mainloop(void) __attribute__ ((weak, alias("ieee_mainloop")));
