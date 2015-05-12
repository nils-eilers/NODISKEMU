#include <inttypes.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "lcd.h"
#include "config.h"

#define LCD_DELAY_US_DATA   46
#define LCD_DELAY_MS_CLEAR  1000

#define LCD_DDRAM 128

uint8_t lcd_x, lcd_y;


static inline void
lcd_SetDataMode(void)
{
   LCD_PORT_RS |= _BV(LCD_PIN_RS);
}


static inline void
lcd_SetCommandMode(void)
{
   LCD_PORT_RS &= ~_BV(LCD_PIN_RS);
}


static void
lcd_Pulse_E(void)
{
   LCD_PORT_E |= _BV(LCD_PIN_E);                // E high
   _delay_us(20);
   LCD_PORT_E &= ~_BV(LCD_PIN_E);               // E low
}


static void
lcd_Write(uint8_t v)
{
   LCD_PORT_DATA &= 0xF0;
   LCD_PORT_DATA |= ((v >> 4) & 0x0F);          // high nibble
   lcd_Pulse_E();
   _delay_us(LCD_DELAY_US_DATA);
   LCD_PORT_DATA &= 0xF0;
   LCD_PORT_DATA |= ( v       & 0x0F);          // low  nibble
   lcd_Pulse_E();
   _delay_us(LCD_DELAY_US_DATA);
}


void
lcd_SendCommand(uint8_t cmd)
{
   lcd_SetCommandMode();
   lcd_Write(cmd);
}


void lcd_Locate(uint8_t x, uint8_t y)
{
   if (x >= LCD_COLS) {
      x = 0;
      y++;
   }

   if (y >= LCD_LINES) y = 0;

   lcd_SetCommandMode();
   uint8_t StartAddress = 0;
   switch (y)
   {
      case 0: StartAddress = LCD_ADDR_LINE1; break;
      case 1: StartAddress = LCD_ADDR_LINE2; break;
      case 2: StartAddress = LCD_ADDR_LINE3; break;
      case 3: StartAddress = LCD_ADDR_LINE4; break;
      default: StartAddress = 0;
   }
   lcd_Write(LCD_DDRAM + StartAddress + x);
   lcd_x = x;
   lcd_y = y;
}



void
lcd_Clear(void)
{
   lcd_SendCommand(0x01);
   lcd_x = lcd_y = 0;
   _delay_ms(LCD_DELAY_MS_CLEAR);
}


void
lcd_Home(void)
{
   lcd_SendCommand(0x02);
   lcd_x = lcd_y = 0;
   _delay_us(LCD_DELAY_MS_CLEAR);
}


void
lcd_init(void)
{
   // LCD ports as output
   LCD_DDR_E  |= _BV(LCD_PIN_E);
   LCD_DDR_RS |= _BV(LCD_PIN_RS);
   LCD_DDR_DATA |= 0x0F;

   // Start with all outputs low
   LCD_PORT_E &= ~_BV(LCD_PIN_E);
   LCD_PORT_RS &= ~_BV(LCD_PIN_RS);
   LCD_PORT_DATA &= 0xF0;


   _delay_ms(15);               // allow LCD to init after power-on

   LCD_PORT_DATA &= 0xF0;
   LCD_PORT_DATA |= 0x03;       // send init value 0x30 three times
   lcd_Pulse_E(); _delay_ms(5);
   lcd_Pulse_E(); _delay_ms(1);
   lcd_Pulse_E(); _delay_ms(1);

   LCD_PORT_DATA &= 0xF2;       // select bus width: 4-bit
   lcd_Pulse_E(); _delay_ms(5);

   lcd_Write(0x28);             // 4 bit, 2 line, 5x7 dots
   lcd_Write(0x0C);             // Display on, cursor off
   lcd_Write(0x06);             // Automatic increment, no shift
   lcd_Clear();
}


void
lcd_putc(char c)
{
   if (c == '\n')
   {
      lcd_x = 0;
      lcd_y++;
   }

   lcd_Locate(lcd_x, lcd_y);
   if (c >= ' ')
   {
      lcd_SetDataMode();
      lcd_Write(c);
      lcd_x++;
   }
}


void
lcd_puts(const char *s)
{
   while (*s)
      lcd_putc(*s++);
}


void
lcd_puts_P(const char *s)
{
   char c;

   while ((c = pgm_read_byte(s++)))
      lcd_putc(c);
}



