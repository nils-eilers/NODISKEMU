/*************************************************************************

   Header file for the USI TWI Slave driver

   Created by Donald R. Blake <donblake at worldnet.att.net>
   Modified by Nils Eilers <nils.eilers@gmx.de>


   Created from Atmel source files for Application Note
   AVR312: Using the USI Module as an I2C slave.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

 ************************************************************************/


#pragma once

void usiTwiSlaveInit(uint8_t);
void usiTwiWriteReg(uint8_t reg, uint8_t value);
