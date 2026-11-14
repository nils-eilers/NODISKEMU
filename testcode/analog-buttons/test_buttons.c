#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t DDRA, PORTA, ADMUX, ADCSRA, ADCSRB, DIDR0, mock_prescaler;
static uint16_t ADCW;
static void fake_delay_ms(unsigned ms);
#include "analog-buttons.c"

static const uint16_t centers[]={572,606,652,697,776,839,931,1023};
static const uint8_t masks[]={0,4,2,6,1,5,3,7};
static uint16_t input;
static unsigned elapsed, scenario;
enum {STEADY, EARLY_PRESS, LATE_PRESS, EXCURSION, NO_TIMER, STUCK_ADC};

static uint8_t feed(uint16_t value) {
  ADCW=value;
  ADCSRA &= (uint8_t)~_BV(ADSC);
  return analog_buttons_poll();
}
static uint8_t fill(uint16_t value) {
  uint8_t result=0;
  for (unsigned i=0;i<8;i++) result=feed(value);
  return result;
}
static void reset(unsigned clock) {
  DDRA=PORTA=ADMUX=ADCSRA=ADCSRB=DIDR0=0xff;
  mock_prescaler=clock;
  analog_buttons_init();
  elapsed=0; scenario=STEADY; input=572;
  assert(DDRA==0x7f && PORTA==0x7f);
  assert(ADMUX==(_BV(REFS0)|7) && ADCSRB==0);
  assert(ADCSRA==(_BV(ADPS2)|_BV(ADPS1)|_BV(ADPS0)|_BV(ADEN)|_BV(ADSC)));
  assert(boot_keys==ANALOG_BUTTON_INVALID);
}
static void fake_delay_ms(unsigned ms) {
  if (mock_prescaler==clock_div_1) ms/=2;
  assert(ms==10);
  elapsed+=ms;
  assert(elapsed<=400);
  if (scenario==NO_TIMER) return;
  if (scenario==STUCK_ADC) {analog_buttons_poll(); return;}
  uint16_t adc=input;
  if (scenario==EARLY_PRESS) adc=elapsed<=80 ? 606 : 572;
  if (scenario==LATE_PRESS) adc=elapsed>=200 ? 606 : 572;
  /* Both transitions occur between foreground snapshots. The sequence
   * must still prevent this from becoming a continuous boot key hold. */
  if (scenario==EXCURSION && elapsed==250) {
    fill(652); fill(606);
  }
  feed(adc);
}

static bool menu_system_enabled;
static unsigned diagnoses, contrasts, event_reads;
static void lcd_init(void) {}
static void lcd_bootscreen(void) {}
static void board_diagnose(void) {diagnoses++;}
static void menu_adjust_contrast(void) {contrasts++;}
static void lcd_set_brightness(uint8_t value) {assert(value==0xff);}
static uint8_t get_key_press(uint8_t mask) {assert(mask==KEY_ANY); event_reads++; return 0;}
static uint8_t get_key_rpt(uint8_t mask) {assert(mask==KEY_ANY); event_reads++; return 0;}
#include "boot_under_test.h"

int main(void) {
  /* All eight button/jumper combinations have a distinct nominal result. */
  for (unsigned i=0;i<8;i++) {
    assert(analog_buttons_decode(centers[i],ANALOG_BUTTON_INVALID)==masks[i]);
    reset(clock_div_2);
    assert(fill(centers[i])==masks[i]);
  }
  for (unsigned held=0;held<8;held++) {
    for (uint32_t adc=0;adc<=UINT16_MAX;adc++) {
      uint8_t keys=analog_buttons_decode(adc,held);
      assert(keys<=7 || keys==ANALOG_BUTTON_INVALID);
      if (adc<540 || adc>1023) assert(keys==ANALOG_BUTTON_INVALID);
    }
  }
  /* The old boot-only PREV range must never trigger at ADC 581 or 590. */
  assert(analog_buttons_decode(581,ANALOG_BUTTON_INVALID)==0);
  assert(analog_buttons_decode(590,ANALOG_BUTTON_INVALID)==ANALOG_BUTTON_INVALID);
  assert(analog_buttons_decode(589,0)==0);
  assert(analog_buttons_decode(590,0)==ANALOG_BUTTON_INVALID);
  assert(analog_buttons_decode(590,KEY_PREV)==KEY_PREV);
  assert(analog_buttons_decode(593,0)==KEY_PREV);
  assert(analog_buttons_decode(585,KEY_PREV)==0);

  /* Discard the first conversion; do not publish an unfilled median. */
  reset(clock_div_2);
  assert(feed(1023)==0 && boot_keys==ANALOG_BUTTON_INVALID);
  for (unsigned i=0;i<4;i++) assert(feed(606)==0);
  assert(boot_keys==ANALOG_BUTTON_INVALID);
  assert(feed(606)==KEY_PREV);
  assert(feed(652)==KEY_PREV && feed(652)==KEY_PREV);
  assert(feed(652)==KEY_NEXT);
  assert(fill(606)==KEY_PREV);
  assert(feed(0)==0 && boot_keys==ANALOG_BUTTON_INVALID);
  for (unsigned i=0;i<4;i++) assert(feed(606)==0);
  assert(feed(606)==KEY_PREV);
  ADCSRA &= (uint8_t)~_BV(ADEN);
  assert(analog_buttons_poll()==0 && boot_keys==ANALOG_BUTTON_INVALID);

  for (unsigned clock=clock_div_1;clock<=clock_div_2;clock++) {
    for (unsigned i=0;i<8;i++) {
      reset(clock); input=centers[i];
      assert(analog_buttons_boot_keys()==masks[i] && elapsed==400);
    }
    const uint16_t invalid[]={0,539,590,629,675,737,808,977,1024};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
      reset(clock); input=invalid[i];
      assert(analog_buttons_boot_keys()==0 && elapsed==400);
    }
    for (unsigned mode=EARLY_PRESS;mode<=STUCK_ADC;mode++) {
      reset(clock); input=606; scenario=mode;
      /* A stale formerly valid result must not pass with no new samples. */
      if (mode==NO_TIMER || mode==STUCK_ADC) assert(fill(606)==KEY_PREV);
      assert(analog_buttons_boot_keys()==0 && elapsed==400);
    }
    reset(clock);
    assert(fill(606)==KEY_PREV);
    input=590; /* Hysteresis may hold PREV, but boot requires a strict window. */
    assert(analog_buttons_boot_keys()==0);
    reset(clock); input=606;
    assert(analog_buttons_boot_keys()==KEY_PREV);
    reset(clock); input=652;
    assert(analog_buttons_boot_keys()==KEY_NEXT);
  }
  /* Exercise both real board startup hooks, including menu-off jumpers. */
  void (*startup[])(void)={boot_petsd,boot_mini};
  for (unsigned board=0;board<2;board++) {
    for (unsigned i=0;i<8;i++) {
      reset(clock_div_1); input=centers[i]; menu_system_enabled=true;
      diagnoses=contrasts=event_reads=0;
      startup[board]();
      assert(diagnoses==(unsigned)(masks[i]==KEY_PREV));
      assert(contrasts==(unsigned)(masks[i]==KEY_NEXT));
      assert(event_reads==2 && elapsed==400);
    }
    reset(clock_div_2); input=581; menu_system_enabled=true;
    diagnoses=contrasts=event_reads=0;
    startup[board]();
    assert(diagnoses==0 && contrasts==0 && elapsed==400);
    reset(clock_div_2); input=652; menu_system_enabled=false;
    diagnoses=contrasts=event_reads=0;
    startup[board]();
    assert(diagnoses==0 && contrasts==0 && event_reads==0 && elapsed==0);
  }
  puts("Analog buttons: all ADC inputs, median, hysteresis, boot holds, faults and both clock speeds passed.");
}
