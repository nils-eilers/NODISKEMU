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


#define I2C_SLAVE_ADDRESS       0x4C

// Write registers
#define PWM_CONTRAST            1
#define PWM_BRIGHTNESS          2
#define IO_IEC                  3

// Read registers
#define I2C_SOFTWARE_VERSION    1

// Software version
#define CONFIG_VERSION          0x4E

// LCD contrast
#define PWM0_DEFAULT            2

// LCD brightness (0: brightest, 0xFF: dark)
#define PWM1_DEFAULT            0

// Default to IEEE-488
#define DIGI_DEFAULT            1
