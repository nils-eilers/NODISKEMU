/* Hardware/filesystem adapter for the actual menu.c browser. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_BUFFER_COUNT 32
#define BUFFER_SEC_SYSTEM 100
#define ERROR_OK 0
#define ERROR_NO_CHANNEL 70
#define LCD_LINES 4
#define OPSTYPE_FAT 1
#define IMG_UNKNOWN 0
#define TYPE_DIR 6
#define EXT_TYPE_MASK 15
#define KEY_PREV 1
#define KEY_NEXT 2
#define KEY_SEL 4
#define PSTR(s) (s)
#define ustrncpy(d,s,n) strncpy((char *)(d),(const char *)(s),(n))
#define ustrcpy_P(d,s) strcpy((char *)(d),(s))
#define ustrlen(s) strlen((const char *)(s))

typedef int dir_t;
typedef struct { uint8_t part; dir_t dir; } path_t;
typedef struct { unsigned position; } dh_t;
typedef struct buffer_s {
  uint8_t *data;
  uint8_t allocated, secondary, dirty;
  uint8_t (*refill)(struct buffer_s *), (*cleanup)(struct buffer_s *);
  union {
    struct {
      dh_t dh;
      uint8_t *matchstr;
      void *match_start, *match_end;
      uint8_t filetype;
    } dir;
    struct { struct buffer_s *next, *first; uint8_t size; } buffer;
  } pvt;
} buffer_t;
typedef struct {
  uint8_t name[17], opstype, typeflags;
  uint16_t blocksize;
  struct { struct { uint8_t realname[13]; } fat; } pvt;
} cbmdirent_t;

static buffer_t buffers[CONFIG_BUFFER_COUNT];
static union { void *align; uint8_t bytes[CONFIG_BUFFER_COUNT * 256]; } storage;
static uint8_t active_buffers, current_error, current_part, command_length;
static uint8_t command_buffer[120];
static struct { dir_t current_dir; } partition[1];
static unsigned files, too_many, too_deep, waits, displayed;
static unsigned depth, deepest, commands, key_reads;
static bool navigation, open_error;
static uint8_t keys[512];
static unsigned key_count, key_pos;

static void set_error(uint8_t e) { current_error = e; }
static void set_busy_led(bool b) { (void)b; }
static void set_dirty_led(bool b) { (void)b; }
static void update_leds(void) {}
static void lcd_clear(void) {}
static void lcd_locate(unsigned x, unsigned y) { (void)x; (void)y; }
static void lcd_cursor(bool b) { (void)b; }
static void lcd_puts_P(const char *s) {
  too_many += strstr(s, "Too many entries") != NULL;
  too_deep += strstr(s, "Maximum menu depth") != NULL;
}
static void wait_anykey(void) { waits++; }
static void uart_trace(const void *p, int a, unsigned n) { (void)p; (void)a; (void)n; }
static void uart_putcrlf(void) {}
static void uart_flush(void) {}
static void uart_puts_P(const char *s) { (void)s; }
static void uart_putc(char c) { (void)c; }
static void pet2asc(uint8_t *s) { (void)s; }
static int check_imageext(const uint8_t *s) { (void)s; return IMG_UNKNOWN; }
static void rom_menu_browse(uint8_t y) { assert(y < 2); }
static void lcd_print_dir_entry(uint16_t i);

static int opendir(dh_t *dh, const path_t *path) {
  (void)path;
  dh->position = 0;
  return open_error;
}
static int next_match(dh_t *dh, uint8_t *match, void *start, void *end,
                      uint8_t type, cbmdirent_t *dent) {
  (void)match; (void)start; (void)end; (void)type;
  if (dh->position++ >= files) return 1;
  memset(dent, 0, sizeof(*dent));
  memset(dent->name, 'A', 16); /* Exercise non-terminated stored names. */
  dent->opstype = OPSTYPE_FAT;
  dent->typeflags = navigation ? TYPE_DIR : 0;
  dent->blocksize = dh->position;
  return 0;
}
static uint8_t key(uint8_t mask) {
  assert(++key_reads < 100000);
  assert(key_pos < key_count);
  if (keys[key_pos] == mask) { key_pos++; return mask; }
  return 0;
}
static uint8_t get_key_autorepeat(uint8_t mask) { return key(mask); }
static uint8_t get_key_press(uint8_t mask) { return key(mask); }
static void parse_doscommand(void) {
  commands++;
  if (!strcmp((char *)command_buffer, "CD_")) {
    assert(depth > 0);
    depth--;
  } else {
    assert(!strncmp((char *)command_buffer, "CD:", 3));
    depth++;
    if (depth > deepest) deepest = depth;
  }
}

/* Suppress the firmware's serial debug output. */
#define printf(...) ((void)0)
#include "production.c"
#undef printf

static void lcd_print_dir_entry(uint16_t i) {
  assert(i < files && i < MAX_DIR_ENTRIES);
  assert(ep[i]->filename[15] == 'A');
  displayed++;
}
static void push(uint8_t k) {
  assert(key_count < sizeof(keys));
  keys[key_count++] = k;
}
static void setup(unsigned count, unsigned available) {
  memset(buffers, 0, sizeof(buffers));
  memset(&storage, 0, sizeof(storage));
  for (unsigned i = 0; i < CONFIG_BUFFER_COUNT; i++) {
    buffers[i].data = storage.bytes + 256 * i;
    buffers[i].allocated = i >= available;
    buffers[i].secondary = BUFFER_SEC_SYSTEM;
  }
  active_buffers = 7; /* Existing channel count must survive every exit. */
  current_error = ERROR_OK;
  files = count;
  too_many = too_deep = waits = displayed = 0;
  depth = deepest = commands = key_reads = 0;
  key_count = key_pos = 0;
  navigation = open_error = false;
}
static void verify_cleanup(unsigned available) {
  assert(active_buffers == 7);
  for (unsigned i = 0; i < CONFIG_BUFFER_COUNT; i++)
    assert(buffers[i].allocated == (i >= available));
  assert(ep == NULL);
}

int main(void) {
  assert(sizeof(entry_t) == 19);
  /* Read, sort and browse past the old 26-entry overflow and to capacity. */
  unsigned counts[] = {0, 1, 26, 27, MAX_DIR_ENTRIES, MAX_DIR_ENTRIES + 1};
  for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
    setup(counts[i], CONFIG_BUFFER_COUNT);
    if (files <= MAX_DIR_ENTRIES) {
      /* Wrap to the last entry, validating the end of the sorted table. */
      if (files) { push(KEY_PREV); push(KEY_NEXT); }
      push(KEY_SEL);
    }
    menu_browse_files();
    assert(too_many == (files > MAX_DIR_ENTRIES));
    if (files && files <= MAX_DIR_ENTRIES) assert(displayed > 0);
    verify_cleanup(CONFIG_BUFFER_COUNT);
  }
  /* First buffer, table, first data block, and later data block failures. */
  unsigned available[] = {0, 1, CONFIG_DIR_BUFFERS + 1, CONFIG_DIR_BUFFERS + 2};
  for (unsigned i = 0; i < sizeof(available) / sizeof(available[0]); i++) {
    setup(27, available[i]);
    menu_browse_files();
    assert(current_error == ERROR_NO_CHANNEL);
    verify_cleanup(available[i]);
  }
  setup(1, CONFIG_BUFFER_COUNT);
  open_error = true;
  menu_browse_files();
  verify_cleanup(CONFIG_BUFFER_COUNT);

  /* Refuse the 16th descent, then navigate all the way back and exit. */
  setup(1, CONFIG_BUFFER_COUNT);
  navigation = true;
  for (unsigned i = 0; i < MAX_LASTPOS; i++) {
    push(KEY_NEXT); push(KEY_NEXT); push(KEY_SEL);
  }
  for (unsigned i = 0; i < MAX_LASTPOS - 1; i++) {
    push(KEY_PREV); push(KEY_SEL);
  }
  push(KEY_PREV); push(KEY_PREV); push(KEY_SEL);
  menu_browse_files();
  assert(too_deep == 1 && waits == 1);
  assert(deepest == MAX_LASTPOS - 1 && depth == 0);
  assert(commands == 2 * (MAX_LASTPOS - 1));
  assert(key_pos == key_count);
  verify_cleanup(CONFIG_BUFFER_COUNT);

  entry_t a = {.filesize = 1}, b = {.filesize = 2};
  memset(a.filename, 'A', sizeof(a.filename));
  memset(b.filename, 'A', sizeof(b.filename));
  entry_t *pa = &a, *pb = &b;
  assert(compare(&pa, &pb) == 0);
  printf("LCD browser: capacity %zu, allocation failures, depth and sorting passed\n",
         (size_t)MAX_DIR_ENTRIES);
}
