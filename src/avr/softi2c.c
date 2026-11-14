/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2026  Ingo Korb <ingo@akana.de>

   NODISKEMU is a fork of sd2iec by Ingo Korb (et al.), http://sd2iec.de

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   softi2c.c: Software I2C bus master

*/

#include <avr/io.h>
#include <util/delay.h>
#include "config.h"
#include "i2c.h"
#ifdef IEC_SLOW_IEEE_FAST
#include <avr/power.h>
#endif

#define SOFTI2C_SDA _BV(SOFTI2C_BIT_SDA)
#define SOFTI2C_SCL _BV(SOFTI2C_BIT_SCL)

/* Delay-based so the timeout also works before interrupts are enabled. */
#ifndef CONFIG_SOFTI2C_TIMEOUT_US
#define CONFIG_SOFTI2C_TIMEOUT_US 200000
#endif
#if CONFIG_SOFTI2C_TIMEOUT_US < 10 || CONFIG_SOFTI2C_TIMEOUT_US > 655350
#error CONFIG_SOFTI2C_TIMEOUT_US must be between 10 and 655350
#endif

/* Both open-drain lines need time to rise through their pull-ups. */
static uint8_t wait_high(uint8_t line) {
  uint16_t retries = CONFIG_SOFTI2C_TIMEOUT_US / 10;
  while (!(SOFTI2C_PIN & line)) {
    if (!retries--)
      return line == SOFTI2C_SCL ? I2C_ERROR_SCL_TIMEOUT : I2C_ERROR_SDA_STUCK;
#ifdef IEC_SLOW_IEEE_FAST
    /* F_CPU describes IEC speed; IEEE mode (and early boot) runs twice
     * as fast. Use the actual prescaler, not the selected bus mode. */
    if (clock_prescale_get() == clock_div_1)
      _delay_us(20);
    else
#endif
    _delay_us(10);
  }
  return 0;
}

static uint8_t set_scl(uint8_t x) {
  if (x) {
    SOFTI2C_DDR  &= (uint8_t)~SOFTI2C_SCL;
    SOFTI2C_PORT |= SOFTI2C_SCL;
    return wait_high(SOFTI2C_SCL);
  } else {
    SOFTI2C_DDR  |= SOFTI2C_SCL;
    SOFTI2C_PORT &= (uint8_t)~SOFTI2C_SCL;
  }
  return 0;
}

static void set_sda(uint8_t x) {
  if (x) {
    SOFTI2C_DDR  &= (uint8_t)~SOFTI2C_SDA;
    SOFTI2C_PORT |= SOFTI2C_SDA;
  } else {
    SOFTI2C_DDR  |= SOFTI2C_SDA;
    SOFTI2C_PORT &= (uint8_t)~SOFTI2C_SDA;
  }
}


static void release_bus(void) {
  set_sda(1);
  SOFTI2C_DDR  &= (uint8_t)~SOFTI2C_SCL;
  SOFTI2C_PORT |= SOFTI2C_SCL;
}

static uint8_t start_condition(void) {
  set_sda(1);
  /* Also provide SCL-low setup time before a repeated START. */
  _delay_us(SOFTI2C_DELAY);
  uint8_t error = set_scl(1);
  if (error) return error;
  error = wait_high(SOFTI2C_SDA);
  if (error) return error;
  _delay_us(SOFTI2C_DELAY);
  set_sda(0);
  _delay_us(SOFTI2C_DELAY);
  set_scl(0);
  return 0;
}

static uint8_t stop_condition(void) {
  set_sda(0);
  _delay_us(SOFTI2C_DELAY);
  uint8_t error = set_scl(1);
  if (error) {
    release_bus();
    return error;
  }
  _delay_us(SOFTI2C_DELAY);
  set_sda(1);
  _delay_us(SOFTI2C_DELAY);
  return wait_high(SOFTI2C_SDA);
}

/* Clear a slave left mid-byte before starting a new transaction. */
static uint8_t recover_bus(void) {
  uint8_t error;
  set_sda(1);
  if ((error = set_scl(1))) goto fail;
  /* Do not clock a healthy slave merely because SDA has not risen yet. */
  if (!wait_high(SOFTI2C_SDA)) return 0;

  for (uint8_t i = 0; i < 9; i++) {
    set_scl(0);
    _delay_us(SOFTI2C_DELAY);
    if ((error = set_scl(1))) goto fail;
    _delay_us(SOFTI2C_DELAY);
    if (SOFTI2C_PIN & SOFTI2C_SDA) break;
  }
  error = stop_condition();
  if (!error) return 0;

fail:
  release_bus();
  return error;
}

/* Always attempt STOP and release both lines, including on failure. */
static uint8_t finish_transfer(uint8_t error) {
  uint8_t stop_error = stop_condition();
  release_bus();
  return error ? error : stop_error;
}

/* Returns NACK or clock-timeout status, zero on success. */
static uint8_t i2c_send_byte(uint8_t value) {
  uint8_t i, error;

  for (i=8;i!=0;i--) {
    set_scl(0);
    _delay_us(SOFTI2C_DELAY/2);
    set_sda(value & 128);
    _delay_us(SOFTI2C_DELAY/2);
    if ((error = set_scl(1))) return error;
    _delay_us(SOFTI2C_DELAY);
    value <<= 1;
  }
  set_scl(0);
  _delay_us(SOFTI2C_DELAY/2);
  set_sda(1);
  _delay_us(SOFTI2C_DELAY/2);
  if ((error = set_scl(1))) return error;
  _delay_us(SOFTI2C_DELAY/2);
  i = !!(SOFTI2C_PIN & SOFTI2C_SDA);
  _delay_us(SOFTI2C_DELAY/2);
  set_scl(0);

  return i;
}

static uint8_t send_checked(uint8_t value, uint8_t nack_error) {
  uint8_t error = i2c_send_byte(value);
  return error == I2C_ERROR_ADDRESS_NACK ? nack_error : error;
}

/* Returns clock-timeout status; stores the byte only on success. */
static uint8_t i2c_recv_byte(uint8_t sendack, uint8_t *data) {
  uint8_t i, error;
  uint8_t value = 0;

  set_sda(1);
  _delay_us(SOFTI2C_DELAY/2);
  for (i=8;i!=0;i--) {
    _delay_us(SOFTI2C_DELAY/2);
    if ((error = set_scl(1))) return error;
    _delay_us(SOFTI2C_DELAY/2);
    value = (value << 1) + !!(SOFTI2C_PIN & SOFTI2C_SDA);
    _delay_us(SOFTI2C_DELAY/2);
    set_scl(0);
    _delay_us(SOFTI2C_DELAY/2);
  }
  set_sda(!sendack);
  _delay_us(SOFTI2C_DELAY/2);
  if ((error = set_scl(1))) return error;
  _delay_us(SOFTI2C_DELAY);
  set_scl(0);
  set_sda(1);

  *data = value;
  return 0;
}

/* Returns a nonzero status on NACK, timeout or a stuck bus. */
uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) {
  return i2c_write_registers(address, reg, 1, &val);
}

/* Returns a nonzero status on NACK, timeout or a stuck bus. */
uint8_t i2c_write_registers(uint8_t address, uint8_t startreg, uint8_t count, const void *data) {
  uint8_t i, error;

  if ((error = recover_bus())) return error;
  if ((error = start_condition()) ||
      (error = send_checked(address, I2C_ERROR_ADDRESS_NACK)) ||
      (error = send_checked(startreg, I2C_ERROR_REGISTER_NACK)))
    return finish_transfer(error);
  for (i=0;i<count;i++) {
    if ((error = send_checked(((const uint8_t *)data)[i], I2C_ERROR_DATA_NACK)))
      return finish_transfer(error);
  }
  return finish_transfer(0);
}

/* Returns -1 on error, register contents otherwise. */
int16_t i2c_read_register(uint8_t address, uint8_t reg) {
  uint8_t val;

  if (i2c_read_registers(address, reg, 1, &val)) return -1;
  return val;
}

/* Returns a nonzero status on NACK, timeout or a stuck bus. */
uint8_t i2c_read_registers(uint8_t address, uint8_t startreg, uint8_t count, void *data) {
  uint8_t i, error;

  if (!count) return I2C_ERROR_DATA_NACK;
  if ((error = recover_bus())) return error;
  if ((error = start_condition()) ||
      (error = send_checked(address, I2C_ERROR_ADDRESS_NACK)) ||
      (error = send_checked(startreg, I2C_ERROR_REGISTER_NACK)) ||
      (error = start_condition()) ||
      (error = send_checked(address|1, I2C_ERROR_ADDRESS_NACK)))
    return finish_transfer(error);
  for (i=0;i<count;i++) {
    if ((error = i2c_recv_byte(i < count-1, &((uint8_t *)data)[i])))
      return finish_transfer(error);
  }
  return finish_transfer(0);
}

void i2c_init(void) {
  /* Set I2C pins to input -> high with external pullups */
  SOFTI2C_DDR  &= (uint8_t)~(SOFTI2C_SCL|SOFTI2C_SDA);
  SOFTI2C_PORT &= (uint8_t)~(SOFTI2C_SCL|SOFTI2C_SDA);

  /* A failed recovery must not prevent the rest of the firmware booting. */
  recover_bus();
}
