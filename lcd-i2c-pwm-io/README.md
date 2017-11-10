I²C-LCD-PWM-I/O
===============


The petSD+ rev. 2.x boards come with a further Atmel ATtiny slave MCU to support the main MCU.
It serves three purposes:

- output a PWM signal to adjust the LCD contrast
- output a PWM signal to adjust the background display's brightness
- output a digital signal to switch between IEEE-488 and IEC bus


ATtiny 25 Pinout:
```
                     ATtiny 25
                 +------    ------+
          /RESET | 1    \__/    8 | Vcc
      PB3 / DIGI | 2            7 | SCL
      PB4 / OC1B | 3            6 | PB1 / OC0B
             GND | 4            5 | SDA
                 +----------------+
```

The MCUs communicate over the I²C bus, the slave uses address 0x4C (that is 0x26 unshifted).

These registers are provided:

| Register | Read             | Write                                                                    |
|----------|------------------|--------------------------------------------------------------------------|
| 1        | Software Version | PWM value LCD contrast (0..255)                                          |
| 2        | --               | PWM value brightness of LCD background display (0: brightest, 255: dark) |
| 3        | --               | Digital Output: 0=IEC bus, 1=IEEE-488 bus                                |
