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

extern bool RemoteMode;


enum SPSP_CMD_fields
{
   PET_CMD,
   PET_FA,
   PET_LOG = PET_FA,
   PET_SA,
   PET_DATA
};

enum SPSP_commands
{
   CMD_LOG,
   CMD_OPEN,
   CMD_CLOSE,
   CMD_GET_BLOCK,
   CMD_PUT_BLOCK,
   CMD_GET_RECORD,
   CMD_PUT_RECORD,
   CMD_READ,
   CMD_ADVANCE,
   CMD_WRITE,
   CMD_RESET,
   CMD_SERV,
   CMD_BUS_CMD,
   CMD_BUS_DATA
};

enum CONF_commands
{
   CONF_DONE,
   CONF_ADD_DEVICE_NUMBER,
   CONF_RM_DEVICE_NUMBER,
   CONF_SNIFF
};

// file access modes
enum
{
   DOS_OPEN_READ,
   DOS_OPEN_WRITE,
   DOS_OPEN_APPEND,
   DOS_OPEN_MODIFY,
   DOS_OPEN_RELATIVE,
   DOS_OPEN_DIRECT
};


void spsp_SendEscEot(void);
void spsp_Check(void);
void spsp_Connect(void);
void spsp_PullConfCommands(void);
uint8_t spsp_GetAckStatus(uint8_t device);
uint8_t spsp_GetFullStatus(uint8_t device);
void spsp_ListenLoop(uint8_t action, uint8_t sa);
uint8_t spsp_OpenFile(uint8_t sa, char *filename);
void spsp_SendCommand(char *command, uint8_t len);
void spsp_Advance(uint8_t device, uint8_t sa);
void spsp_Message(const char *message);
void spsp_LoadBuffer(struct Buffer *buf);
void spsp_UserCommand(char *command);
void spsp_BlockCommand(char *command, uint8_t len);
void spsp_PositionCommand(uint8_t *command, uint8_t len);
void spsp_GetBlock(struct Buffer *buf, uint8_t device, uint8_t drive, uint8_t t, uint8_t s);
void spsp_PutBlock(struct Buffer *buf, uint8_t device, uint8_t drive, uint8_t t, uint8_t s);
void spsp_GetRecord(struct Buffer *buf);
void spsp_PutRecord(struct Buffer *buf);

