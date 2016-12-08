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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "config.h"
#include "channel.h"
#include "system.h"
#include "uart.h"
#include "ieee.h"          // IEEE_NO_DATA, IEEE_EOI
#include "devnumbers.h"
#include "errormsg.h"
#include "spsp.h"

struct Buffer Buffers[CONFIG_MAX_BUFFERS];

/* Data transfer from our device to the CBM may get interrupted by ATN,
   so channel_Get() returns the current character whithout advancing
   any pointers to enable restarting the transmission.
   If the transmission is confirmed, channel_Got() then increases the
   pointer and marks buffer for loading..
 */

void
channel_Close(uint8_t device, uint8_t sa)
{
   if (RemoteMode)
   {
      uart_putc(CMD_CLOSE);
      uart_putc(device);
      uart_putc(sa);
      spsp_GetAckStatus(device);
      channel_FreeBuffer(device, sa);
      return;
   }
}


// return pointer to buffer in use or NULL if not found
struct Buffer *
channel_BufferSearch(uint8_t device, uint8_t sa)
{
   uint8_t i;

   for (i=0 ; i < CONFIG_MAX_BUFFERS ; ++i)
   {
      if (Buffers[i].Device == device && Buffers[i].SecondaryAddress == sa)
         return &Buffers[i];
   }
   return NULL;
}


// return pointer to buffer, if not already in use: allocate one
struct Buffer *
channel_BufferSearchOrAllocate(uint8_t device, uint8_t sa)
{
   uint8_t i;
   struct Buffer *buf = channel_BufferSearch(device, sa);
   if (buf) return buf;

   for (i=0 ; i < CONFIG_MAX_BUFFERS ; ++i)
   {
      if (Buffers[i].Device == 0)
      {
         memset (&Buffers[i], 0, sizeof(struct Buffer));
         Buffers[i].Device = device;
         Buffers[i].SecondaryAddress = sa;
         return &Buffers[i];
      }
   }
   set_error(ERROR_NO_CHANNEL);
   return NULL;
}


void
channel_FreeBuffer(uint8_t device, uint8_t sa)
{
   struct Buffer *buf = channel_BufferSearch(device, sa);
   if (buf == NULL) return;
   memset (buf, 0, sizeof(struct Buffer));
}


void
channel_LoadBuffers(void)
{
   for (uint8_t i = 0; i < CONFIG_MAX_BUFFERS; i++)
   {
      if (Buffers[i].LoadRequired)
      {
         Buffers[i].Load(&Buffers[i]);
      }
   }
}


void
channel_OpenDirectBuffer(uint8_t device, uint8_t sa, char *filename)
{
   uint8_t bufnum;
   struct  Buffer *buf = NULL;
   char Message[40];

   if (filename[1] == 0) // allocate next free buffer
   {
      buf = channel_BufferSearchOrAllocate(device, sa);
      buf->Mode = DOS_OPEN_DIRECT;
      buf->len  = BUF_LEN;
      spsp_Message("assigned direct buffer");
   }
   else
   {
      if (isdigit(filename[1]))
      {
         bufnum = atoi(filename+1);
         if (bufnum < CONFIG_MAX_BUFFERS && Buffers[bufnum].Device == 0)
         {
            memset (&Buffers[bufnum], 0, sizeof(struct Buffer));
            Buffers[bufnum].Device           = device;
            Buffers[bufnum].SecondaryAddress = sa;
            Buffers[bufnum].Mode             = DOS_OPEN_DIRECT;
            Buffers[bufnum].len              = BUF_LEN;
            buf = &Buffers[bufnum];
            sprintf(Message,"assigned buffer # %d",bufnum);
            spsp_Message(Message);
         }
      }
   }
   if (buf == NULL) set_error(ERROR_NO_CHANNEL);
}

