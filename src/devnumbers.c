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
#include <string.h>

#include "config.h"
#include "devnumbers.h"

uint8_t MyDevNumbers[CONFIG_MAX_DEVICES];

bool
devnumbers_Add(uint8_t DevNumber)
{
   for (uint8_t i = 0; i < CONFIG_MAX_DEVICES; i++)
      if (MyDevNumbers[i] == 0)
      {
         MyDevNumbers[i] = DevNumber;
         return false;
      }
   return true;
}


bool
devnumbers_Remove(uint8_t DevNumber)
{
   for (uint8_t i = 0; i < CONFIG_MAX_DEVICES; i++)
      if (MyDevNumbers[i] == DevNumber)
      {
         MyDevNumbers[i] = 0;
         return false;
      }
   return true;
}


/* devnumbers_Idx() is used in most cases to search the table of assigned
   device numbers for a given device number. It returns the index in this
   table. For example, if the second device in the table has a device
   number of 9 (MyDevNumbers[1] = 9), calling devnumbers_Idx(9) will
   return the index value 1.

   If the table does not contain the given device number, devnumbers_Idx()
   returns -1.

   In rare cases, esp. during init, such a device number does not exist
   and functions whose API would require a device number should work
   with a given index instead. To do so, devnumbers_Idx() when called with
   negative device addresses does not search the table but passes the
   converted given index:

      devnumbers_Idx(-1) returns 0
      devnumbers_Idx(-2) returns 1
      devnumbers_Idx(-3) returns 2
      ...
 */

int8_t
devnumbers_Idx(int8_t AddressedDevice)
{
   if (AddressedDevice < 0)
      return -AddressedDevice - 1;
   for (uint8_t i = 0; i < CONFIG_MAX_DEVICES; i++)
      if (MyDevNumbers[i] == AddressedDevice)
         return i;
   return -1;
}


bool
devnumbers_MyDevNumber(uint8_t AddressedDevice)
{
   if (devnumbers_Idx(AddressedDevice) < 0)
      return false;
   else
      return true;
}


void
devnumbers_Init(bool restore)
{
   if (restore)
   {
      //TODO: get devnumber from EEPROM/switches
      devnumbers_Add(CONFIG_DEFAULT_ADDR);
   }
   else
      memset(MyDevNumbers, 0, CONFIG_MAX_DEVICES);
}
