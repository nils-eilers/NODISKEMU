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
#include "config.h"

#ifndef CONFIG_MAX_BUFFERS
#define CONFIG_MAX_BUFFERS 4
#endif

#define COMMAND_CHANNEL 15
#define STATUS_CHANNEL  15
#define BUF_LEN 256

struct Buffer
{
   uint8_t  Device;             // zero if buffer is not used
   uint8_t  SecondaryAddress;   // aka channel
   uint16_t ptr;                // index to next r/w position in buffer
   uint16_t len;                // valid bytes in buffer
   uint16_t rec;                // record # of relative file
   uint8_t  Mode;               // access mode
   bool     SendEoi;            // send Data[EoiPosition] with EOI
   bool     LoadRequired;       // call load() before next access
   uint8_t  EoiPosition;
   uint8_t  Data[BUF_LEN];
   void     (*Load)(struct Buffer *self);
};

extern struct Buffer Buffers[CONFIG_MAX_BUFFERS];

// Get next character but do not advance pointers because
// transmission to CBM may get interrupted by ATN
int channel_Get(uint8_t AddressedDevice, uint8_t sa);

// Confirm transmission: advance buffer pointers
void channel_Got(uint8_t device, uint8_t sa);
void channel_Put(char c, uint8_t AddressedDevice, uint8_t sa);

void channel_Open(char *s, uint8_t AddressedDevice, uint8_t sa);
void channel_Close(uint8_t AddressedDevice, uint8_t sa);

struct Buffer *channel_BufferSearch(uint8_t device, uint8_t sa);
struct Buffer *channel_BufferSearchOrAllocate(uint8_t device, uint8_t sa);
void channel_LoadBuffers(void);
void channel_FreeBuffer(uint8_t device, uint8_t sa);
void channel_OpenDirectBuffer(uint8_t device, uint8_t sa, char *filename);
