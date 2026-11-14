/*
                      ATtiny 25
                 +------    ------+
          /RESET | 1    \__/    8 | Vcc
      PB3 / DIGI | 2            7 | SCL
      PB4 / OC1B | 3            6 | PB1 / OC0B
             GND | 4            5 | SDA
                 +----------------+

  This configuration file is shared by slave and master MCU

*/

#pragma once


// Eight-bit write address (seven-bit slave address 0x26).
// Keep compatible with deployed Tiny firmware.
#define I2C_SLAVE_ADDRESS       0x4C
#if I2C_SLAVE_ADDRESS & 1
#error I2C_SLAVE_ADDRESS must have its read/write bit cleared
#endif

// Write registers
#define PWM_CONTRAST            1
#define PWM_BRIGHTNESS          2
#define IO_IEC                  3

// Read registers
#define I2C_SOFTWARE_VERSION    1
// Alias used by masters working around the legacy register-state bug.
#define I2C_SOFTWARE_VERSION_COMPAT 0

// Software version
#define CONFIG_VERSION          0x69

// LCD contrast
#define PWM0_DEFAULT            2

// LCD brightness (0: brightest, 0xFF: dark)
#define PWM1_DEFAULT            0

// Default to IEEE-488
#define DIGI_DEFAULT            1
