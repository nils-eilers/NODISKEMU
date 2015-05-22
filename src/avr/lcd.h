#pragma once

extern uint8_t lcd_x; // 0..LCD_COLS-1
extern uint8_t lcd_y; // 0..LCD_LINES-1

void lcd_init(void);
void lcd_clear(void);
void lcd_home(void);
void lcd_locate(uint8_t x, uint8_t y);
void lcd_send_command(uint8_t cmd);
void lcd_putc(char c);
void lcd_puts(const char *s);
void lcd_puts_P(const char *progmem_s);
