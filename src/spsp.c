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
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "channel.h"
#include "spsp.h"
#include "uart.h"
#include "timer.h"
#include "errormsg.h"
#include "progmem.h"
#include "led.h"
#include "devnumbers.h"
#include "ieee.h"
#include "system.h"

enum {
  ESC = 0x1B, EOT = 0x04, FS  = 0x1C,
  DC1 = 0x11, DC2 = 0x12, DC3 = 0x13,
  ENQ = 0x05, ACK = 0x06, NAK = 0x15,
};

#ifndef CONFIG_SPSP_CONNECT_TIMEOUT_TICKS
#define CONFIG_SPSP_CONNECT_TIMEOUT_TICKS      (HZ)
#endif

#ifndef CONFIG_SPSP_REPLY_TIMEOUT_TICKS
#define CONFIG_SPSP_REPLY_TIMEOUT_TICKS        (HZ * 20)
#endif

// Maximum number of characters in disk status
#ifndef CONFIG_STATUS_MAX
#define CONFIG_STATUS_MAX 40
#endif

#define RX_ESC  0x80
#define RX_EOT  0x40
#define MIN_ESC_EOT_SEQ 4

#define STATUS_CHANNEL 15

bool RemoteMode; // true if connected to server, false in stand-alone mode
uint8_t spsp_LastRxChar;        // used by UART RX function
uint8_t spsp_EscEotCounter;     // used by UART RX function

char Status[CONFIG_MAX_DEVICES][CONFIG_STATUS_MAX + 1];
uint8_t status_read_ptr[CONFIG_MAX_DEVICES];


// Private prototypes
static void spsp_FatalError_P(const char *msg);
#define spsp_FatalError(s) spsp_FatalError_P(PSTR(s))


void spsp_SendEscEot(void) {
  uart_putc(ESC);
  uart_putc(EOT);
}


/* spsp_Connect() is called by the UART receive interrupt routine
   when the magic string ESC EOT ... ENQ was received
 */

void spsp_Connect(void) {
  uint8_t i;
  char c, SelectedBus;
  tick_t timeout;

  RemoteMode = false;

  for (i = 0; i < 8; i++) spsp_SendEscEot();
  uart_putc(ENQ);

  // Expect ACK within 500 ms
  timeout = getticks() + CONFIG_SPSP_CONNECT_TIMEOUT_TICKS;
  while (uart_rxbuf_empty() && time_before(getticks(), timeout));
  if (uart_rxbuf_empty()) return;
  c = uart_getc();
  if (c != ACK) return;
  RemoteMode = true;

#if defined(CONFIG_HAVE_IEC) && defined(CONFIG_HAVE_IEEE)
  uart_putc(DC3);
#else
#if defined(CONFIG_HAVE_IEEE)
  uart_putc(DC1);
#else
#if defined(CONFIG_HAVE_IEC)
  uart_putc(DC2);
#else
#error Neigther CONFIG_HAVE_IEC nor CONFIG_HAVE_IEEE defined for this device
#endif
#endif
#endif

  // Send version number of this protocol
  uart_putc(0);

  // Tell our name
  uart_puts_P(HWNAME);
  spsp_SendEscEot();

  // Wait for server to select a bus
  timeout = getticks() + CONFIG_SPSP_CONNECT_TIMEOUT_TICKS;
  while (uart_rxbuf_empty() && time_before(getticks(), timeout));
  if (uart_rxbuf_empty()) return;
  SelectedBus = uart_getc();

  // TODO: close any open files left open from stand-alone mode
  // TODO: display remote symbol on LCD
  devnumbers_init();           // clear assigned device numbers
  led_state = 0;               // turn off all LEDs
  set_busy_led(0);
  set_dirty_led(0);
  // sniffer_Bus(false);
  uart_putc(ACK);
  spsp_PullConfCommands();     // fetch configuration

#ifdef CONFIG_HAVE_IEEE
  if (SelectedBus == DC1) for (;;) ieee_mainloop();
#endif
#if defined(CONFIG_HAVE_IEC)
  if (SelectedBus == DC2) for (;;) /* TODO: iec_main_loop() */ ;
#endif
}


char spsp_getc(void) {
  tick_t timeout = getticks() + CONFIG_SPSP_REPLY_TIMEOUT_TICKS;
  while (uart_rxbuf_empty() && time_before(getticks(), timeout));
  if (uart_rxbuf_empty())
  {
    uart_puts_P("timeout in spsp_getc()");
    return 0;
  }
  return uart_getc();
}


char spsp_Receive(char *p, uint16_t MaxChars) {
  uint16_t ReceivedChars = 0;
  char c;

  do {
    c = spsp_getc();
    if (c == ESC) {
      c = spsp_getc();
      switch (c) {
        case ESC:
          break;              // ESC ESC --> ESC

        case EOT:
          return EOT;         // end of transmission but not end of file

        case FS:
          return FS;          // end of transmission, end of file

        default:
          spsp_FatalError("Unknown ESC sequence received");
          break;
      }
    }
    *p++ = c;
  }
  while (++ReceivedChars <= MaxChars);
  spsp_FatalError("Server sends too many bytes");
  return 0; // Never reached but required to make GCC happy
}


/* spsp_GetAckStatus fetches first the status byte from the device.
   If the status is not 0 (ERROR_OK) it continues and receives
   the complete status message from the device, otherwise it
   sets the status to the default message "00, OK,00,00,0".
   This can result in a long waiting period, because the server
   may perform time consuming tasks before he acknowledges.
   Therefore we wait for the status byte and do not use the
   timeout option.
 */

uint8_t spsp_GetAckStatus(uint8_t device) {
  uint8_t dix = devnumbers_Idx(device);
  uint8_t ds;

  Status[dix][0] = 13;           // preset with CR
  status_read_ptr[dix] = 0;      // reset pointer
  ds = uart_getch();             // get status byte
  if (ds == ERROR_OK) strcpy(Status[dix],"00, OK,00,00,0\r");
  else                 spsp_Receive(Status[dix], CONFIG_STATUS_MAX);
  return ds;
}


uint8_t spsp_GetFullStatus(uint8_t device) {
  uart_putc(CMD_READ);
  uart_putc(device);
  uart_putc(STATUS_CHANNEL);
  uart_putc(CONFIG_STATUS_MAX);
  return spsp_GetAckStatus(device);
}


/* spsp_FatalError() sends an error message and resets the MCU.
   For devices with stand-alone mode, this causes returning to
   stand-alone mode. Devices without stand-alone mode will try
   to re-connect to the server after reset.
 */

static void spsp_FatalError_P(const char *msg) {
  uart_putc(CMD_LOG);
  uart_puts_P(msg);
  spsp_SendEscEot();
  system_reset();
}


void spsp_Message(const char *message) {
  uart_putc(CMD_LOG);
  uart_puts_P(message);
  uart_putc('\n');
  spsp_SendEscEot();
}


/* spsp_PullConfCommands() should get exectued several times per second.
   It is used to confirm that the connection to the server is still
   established and gives the server a chance to send its commands.
   Don't call this function when timing is critical since it may take
   up to the ServerReplyTimeout to execute.
 */

void spsp_PullConfCommands(void) {
  uint8_t cmd;

  uart_putc(CMD_SERV);
  tick_t timeout = getticks() + CONFIG_SPSP_REPLY_TIMEOUT_TICKS;
  while (uart_rxbuf_empty() && time_before(getticks(), timeout));
  if (uart_rxbuf_empty())
  {
     spsp_FatalError("CMD_SERV timeout");
     return;
  }
  cmd = uart_getc();
  if (cmd == NAK) return; // Server is alive and has no commands to send
  if (cmd != ACK) {
    // received illegal reply
    return;
  }

  // Fetch CONF-commands from server
  for (;;) {
    cmd = uart_getch();

    switch (cmd) {
      case CONF_DONE:
        break; // server has no more commands to send

      case CONF_ADD_DEVICE_NUMBER:
        uart_putc(devnumbers_Add(uart_getch()) ? NAK : ACK);
        break;

      case CONF_RM_DEVICE_NUMBER:
        devnumbers_Remove(uart_getch());
        uart_putc(ACK);
        break;

/*
      case CONF_SNIFF:
        sniffer_Bus(uart_getch());
        break;
*/

      default:
        spsp_FatalError("Unknown CONF-command received");
        break;
    }
    if (cmd == CONF_DONE) break; // exit forever loop
  }

  for (uint8_t i = 0; i < CONFIG_MAX_DEVICES; i++)
    if (MyDevNumbers[i] > 0) {
      spsp_GetAckStatus(MyDevNumbers[i]);
    }
}

/* open file on remote server, get file access mode */

uint8_t spsp_OpenFile(uint8_t sa, char *filename) {
  uint8_t device = ieee488_ListenIsActive();
  uint8_t access_mode;

  uart_putc(CMD_OPEN);
  uart_putc(device);
  uart_putc(sa);

  while (*filename) {
    uart_putc(*filename);
    if (*filename == ESC) uart_putc(ESC);
    ++filename;
  }
  spsp_SendEscEot();

  access_mode = uart_getch();

  spsp_GetAckStatus(device);
  return access_mode;
}


/* A DOS command may contain binary zeroes.
   we use therefore the parameter len
 */

void spsp_SendCommand(char *command, uint8_t len) {
  uint8_t device = ieee488_ListenIsActive();

  uart_putc(CMD_WRITE);
  uart_putc(device);
  uart_putc(COMMAND_CHANNEL);

  while (len--) {
    uart_putc(*command);
    if (*command == ESC) uart_putc(ESC);
    ++command;
  }
  spsp_SendEscEot();
  spsp_GetAckStatus(device);
}


void spsp_ListenLoop(uint8_t action, uint8_t sa) {
  uint8_t device;
  uint8_t receive_status;
  char    c;
  struct Buffer *buf;

  // If data is not for us and Sniffing is disabled: nothing to do
  // if (action == LL_SNIFFONLY && !Sniffing) return;

  /* FIXME: if there are multiple listeners, we would have to
     send multiple CMD_WRITE commands. We should fix this in the
     SPSP specs!
   */

  device = ieee488_ListenIsActive();
  buf = channel_BufferSearch(device, sa);

  // do we write into a direct buffer ?

  if (buf && buf->Mode == DOS_OPEN_DIRECT) {
    while ((receive_status = ieee488_RxByte(&c)) != RX_ATN) {
      buf->Data[buf->ptr++] = c;
      if (buf->ptr >= BUF_LEN) buf->ptr = 0;
      if (receive_status == RX_EOI) break;
    }
    return;
  }

  // do we write into a record buffer ?

  if (buf && buf->Mode == DOS_OPEN_RELATIVE) {
    while ((receive_status = ieee488_RxByte(&c)) != RX_ATN) {
      if (buf->ptr < BUF_LEN)
        buf->Data[buf->ptr++] = c;
      if (receive_status == RX_EOI) break;
    }
    buf->len = buf->ptr;      // length written
    if (buf->ptr < BUF_LEN)   // clear rest of record
      memset(buf->Data+buf->ptr,0,BUF_LEN-buf->ptr);
    spsp_PutRecord(buf);      // flush record to disk
    buf->rec++;               // advance record #
    spsp_GetRecord(buf);      // load next record
    return;
  }

  if (action == LL_RECEIVE)   uart_putc(CMD_WRITE);
  if (action == LL_SNIFFONLY) uart_putc(CMD_BUS_DATA);

  if (action == LL_RECEIVE) {
    uart_putc(device);
    uart_putc(sa);
  }

  while ((receive_status = ieee488_RxByte(&c)) != RX_ATN) {
    uart_putc(c);
    if (c == ESC) uart_putc(c);
    if (receive_status == RX_EOI) break;
  }
  spsp_SendEscEot();
  if (action == LL_RECEIVE) {
    spsp_GetAckStatus(device);
  }
}

void spsp_LoadBuffer(struct Buffer *buf) {
  char c;

  uart_putc(CMD_READ);
  uart_putc(buf->Device);
  uart_putc(buf->SecondaryAddress);
  uart_putc(0);        // 0: read max. 256 bytes
  buf->len = 0;
  buf->ptr = 0;
  buf->SendEoi = false;
  while (true) {
    c = spsp_getc();
    if (c == ESC) {
      c = spsp_getc();
      if (c == FS) buf->SendEoi = true;
      if (c == EOT || c == FS) break;
    }
    if (buf->len == BUF_LEN) break; // missing delimiter ?
    buf->Data[buf->len++] = c;
  }
  buf->EoiPosition  = buf->len - 1;
  buf->LoadRequired = false;
  buf->Load         = spsp_LoadBuffer;
  spsp_GetAckStatus(buf->Device);
}

void spsp_Advance(uint8_t device, uint8_t sa) {
  struct Buffer *buf = channel_BufferSearch(device, sa);
  if (buf) {
    uart_putc(CMD_ADVANCE);
    uart_putc(device);
    uart_putc(sa);
    uart_putc(buf->len); // the value 0 advances 256 bytes
  }
}


void spsp_GetBlock(struct Buffer *buf, uint8_t device, uint8_t drive,
        uint8_t track, uint8_t sector) {
  int i;

  uart_putc(CMD_GET_BLOCK);
  uart_putc(device);
  uart_putc(drive);
  uart_putc(track);
  uart_putc(sector);

  for (i=0 ; i < BUF_LEN ; ++i)
    buf->Data[i] = uart_getch();

  spsp_GetAckStatus(device);

  buf->ptr = 0;
  buf->len = BUF_LEN;
  buf->LoadRequired = false;
  buf->SendEoi = true;
  buf->EoiPosition = BUF_LEN-1;
}


void spsp_PutBlock(struct Buffer *buf, uint8_t device, uint8_t drive,
        uint8_t track, uint8_t sector) {
  int i;
  uart_putc(CMD_PUT_BLOCK);
  uart_putc(device);
  uart_putc(drive);
  uart_putc(track);
  uart_putc(sector);

  for (i=0 ; i < BUF_LEN ; ++i)
    uart_putc(buf->Data[i]);

  spsp_GetAckStatus(device);
}


void spsp_GetRecord(struct Buffer *buf) {
  uint8_t i;
  uart_putc(CMD_GET_RECORD);
  uart_putc(buf->Device);
  uart_putc(buf->SecondaryAddress);
  uart_putc(buf->rec);
  uart_putc(buf->rec>>8);

  buf->len = uart_getch();

  for (i=0 ; i < buf->len ; ++i)
    buf->Data[i] = uart_getch();

  spsp_GetAckStatus(buf->Device);

  // determine true record length by ignoring trailing zeroes

  for (i = buf->len-1 ; i > 0 ; --i)
    if (buf->Data[i]) break;

  buf->ptr          = 0;
  buf->EoiPosition  = i;
  buf->SendEoi      = true;
  buf->LoadRequired = false;
  buf->Load         = spsp_GetRecord;
}


void spsp_PutRecord(struct Buffer *buf) {
  uint8_t i;
  uart_putc(CMD_PUT_RECORD);
  uart_putc(buf->Device);
  uart_putc(buf->SecondaryAddress);
  uart_putc(buf->rec);
  uart_putc(buf->rec>>8);
  uart_putc(buf->len);

  for (i=0 ; i < buf->len ; ++i)
    uart_putc(buf->Data[i]);

  spsp_GetAckStatus(buf->Device);
}


void spsp_UserCommand(char *command) {
  int     channel,drive,track,sector;
  uint8_t device,args;
  char    action;
  struct  Buffer *buf = NULL;

  device = ieee488_ListenIsActive();
  action = command[1];

  if (action == '1' || action == 'A' || action == '2' || action == 'B')
  {
    args = sscanf(command+2, "%*[: ]%d%*[, ]%d%*[, ]%d%*[, ]%d",
        &channel, &drive, &track, &sector);

    if (args != 4) {
      set_error(ERROR_SYNTAX_UNKNOWN);
      return;
    }

    buf = channel_BufferSearch(device, channel);
    if (buf == NULL) {
      set_error(ERROR_NO_CHANNEL);
      return;
    }
  }

  switch (action) {
    case '0': break; // restore default user jump table

    case '1':
    case 'A': spsp_GetBlock(buf, device, drive, track, sector);
              break;
    case '2':
    case 'B': spsp_PutBlock(buf, device, drive, track, sector);
              break;
    case ':':
    case 'J': set_error(ERROR_DOSVERSION);
              break;
    default:  set_error(ERROR_SYNTAX_UNKNOWN);
  }
}


void spsp_BlockCommand(char *command, uint8_t Len) {
  int channel, pos;
  uint8_t args = 0;
  uint8_t device;
  struct  Buffer *buf;

  // Handle BUFFER-POINTER (B-P) in Emily, forward all other
  // like BLOCK-ALLOCATE, BLOCK-FREE, etc. to server

  if (!strncmp(command,"B-P",3))
    args = sscanf(command+ 3, "%*[: ]%d%*[, ]%d", &channel, &pos);
  else if (!strncmp(command,"BUFFER-POINTER",14))
    args = sscanf(command+14, "%*[: ]%d%*[, ]%d", &channel, &pos);
  else
  {
    spsp_SendCommand(command,Len);
    return;
  }

  device = ieee488_ListenIsActive();
  if (args != 2 || pos < 0 || pos > 255) {
    set_error(ERROR_SYNTAX_UNKNOWN);
    return;
  }
  buf = channel_BufferSearch(device, channel);
  if (buf == NULL) {
    set_error(ERROR_NO_CHANNEL);
    return;
  }
  buf->ptr = pos;
  set_error(ERROR_OK);
}

void spsp_PositionCommand(char *command, uint8_t Len) {
  uint16_t record;
  uint8_t  device;
  uint8_t  channel;
  uint8_t  offset = 0;
  struct   Buffer *buf;

  device = ieee488_ListenIsActive();

  // Handle RECORD command, aka "P" (channel) (rec #) (offset)

  if (Len < 4) {
    set_error(ERROR_SYNTAX_UNKNOWN);
    return;
  }
  channel = command[1] & 15;
  record  = command[2] + (command[3] << 8);
  if (Len > 4) offset = command[4];
  if (offset > 0) --offset;

  buf = channel_BufferSearch(device, channel);
  if (buf == NULL) {
    set_error(ERROR_NO_CHANNEL);
    return;
  }
  if (record != buf->rec) {     // load new record
    buf->rec   = record;
    spsp_GetRecord(buf);
  }
  buf->ptr = offset;
  set_error(ERROR_OK);
}

