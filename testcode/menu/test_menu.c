/* Hardware/filesystem adapter for the actual menu.c browser. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_BUFFER_COUNT
#define CONFIG_BUFFER_COUNT 32
#endif
#define BUFFER_SEC_SYSTEM 100
#define ERROR_OK 0
#define ERROR_NO_CHANNEL 70
#define ERROR_DIR_ERROR 71
#define LCD_LINES 4
#define OPSTYPE_FAT 1
#define IMG_UNKNOWN 0
#define TYPE_DIR 6
#define EXT_TYPE_MASK 15
#define KEY_PREV 4
#define KEY_NEXT 2
#define KEY_SEL 1
#define PSTR(s) (s)
#define ustrncpy(d,s,n) strncpy((char *)(d),(const char *)(s),(n))
#define ustrcpy_P(d,s) strcpy((char *)(d),(s))
#define ustrlen(s) strlen((const char *)(s))
typedef uint16_t tick_t;
#define MS_TO_TICKS(ms) ((ms)/10)
#define time_before(a,b) ((int16_t)((tick_t)(a)-(tick_t)(b))<0)

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
static uint8_t active_buffers, dirty_buffers, current_error, current_part, command_length;
static uint8_t command_buffer[120];
static struct { dir_t current_dir; } partition[1];
static unsigned files, too_many, too_deep, waits, displayed;
static unsigned depth, deepest, commands, key_reads;
static bool navigation, open_error, native_order, varied_names, image_root;
static int read_error_at;
static unsigned scans,root_files,available_initial;
static bool seen[2000];
static uint8_t keys[10000];
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
static int check_imageext(const uint8_t *s) { return *s=='I' ? 1 : IMG_UNKNOWN; }
static void rom_menu_browse(uint8_t y) { assert(y < 2); }
static void lcd_print_dir_entry(uint16_t i);

static int opendir(dh_t *dh, const path_t *path) {
  (void)path;
  dh->position = 0;
  if (root_files) files=depth ? 1 : root_files;
  scans++;
  return open_error;
}
static int next_match(dh_t *dh, uint8_t *match, void *start, void *end,
                      uint8_t type, cbmdirent_t *dent) {
  (void)match; (void)start; (void)end; (void)type;
  if ((int)dh->position==read_error_at) {current_error=ERROR_DIR_ERROR; return 1;}
  if (dh->position >= files) return -1;
  unsigned ordinal=dh->position++;
  memset(dent, 0, sizeof(*dent));
  memset(dent->name, 'A', 16); /* Exercise non-terminated stored names. */
  dent->opstype = native_order || (image_root && depth) ? 0 : OPSTYPE_FAT;
  dent->typeflags = navigation ? TYPE_DIR : 0;
  if (image_root && !depth) dent->pvt.fat.realname[0]='I';
  if (varied_names) {
    /* Descending names, many exact duplicates, interleaved directories/images. */
    snprintf((char *)dent->name,sizeof(dent->name),"%016u",(files-ordinal)/3);
    if (ordinal%7==0) dent->typeflags=TYPE_DIR;
    if (ordinal%5==0) dent->pvt.fat.realname[0]='I';
  }
  dent->blocksize = ordinal;
  return 0;
}
static uint8_t key(uint8_t mask) {
  assert(++key_reads < 100000);
  assert(key_pos < key_count);
  if (keys[key_pos] == mask) { key_pos++; return mask; }
  return 0;
}
static tick_t getticks(void) {return (tick_t)key_reads;}
static uint8_t get_key_state(uint8_t mask) {
  assert(key_pos<key_count);
  return keys[key_pos] & mask;
}
static uint8_t get_key_rpt(uint8_t mask) {(void)mask; return 0;}
static uint8_t get_key_state_and_press(uint8_t mask, uint8_t *pressed) {
  uint8_t held=get_key_state(mask);
  *pressed=held;
  if (held) key_pos++;
  return held;
}
static uint8_t get_key_press(uint8_t mask) {
  if (mask==(KEY_PREV|KEY_NEXT)) {
    assert(key_pos<key_count);
    uint8_t pressed=keys[key_pos]&mask;
    if (pressed) key_pos++;
    return pressed;
  }
  return key(mask);
}
static void parse_doscommand(void) {
  commands++;
  for (unsigned i=0;i<CONFIG_BUFFER_COUNT;i++)
    assert(buffers[i].allocated==(i>=available_initial));
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
  assert(i < browser_page.count);
  assert(browser_entry(i)->ordinal < files);
  if (browser_entry(i)->ordinal<sizeof(seen)/sizeof(seen[0]))
    seen[browser_entry(i)->ordinal]=true;
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
  navigation = open_error = native_order = varied_names = false;
  image_root = false;
  read_error_at=-1; scans=root_files=0;
  available_initial=available;
  memset(seen,0,sizeof(seen));
}
static void verify_cleanup(unsigned available) {
  assert(active_buffers == 7);
  for (unsigned i = 0; i < CONFIG_BUFFER_COUNT; i++)
    assert(buffers[i].allocated == (i >= available));
  assert(browser_page.blocks == NULL && browser_page.capacity==0);
}

static void test_repeat(void) {
  browser_repeat_t repeat={0};
  assert(browser_repeat_update(&repeat,0,0,0)==0);
  assert(browser_repeat_update(&repeat,KEY_NEXT,KEY_NEXT,0)==KEY_NEXT);
  assert(browser_repeat_update(&repeat,KEY_NEXT,0,49)==0);
  for (unsigned t=50;t<200;t+=15) {
    assert(browser_repeat_update(&repeat,KEY_NEXT,0,t)==KEY_NEXT);
    assert(browser_repeat_update(&repeat,KEY_NEXT,0,t)==0);
    assert(repeat.level==0 && repeat.next==t+15);
  }
  assert(browser_repeat_update(&repeat,KEY_NEXT,0,199)==0);
  for (unsigned t=200;t<400;t+=8) {
    assert(browser_repeat_update(&repeat,KEY_NEXT,0,t)==KEY_NEXT);
    assert(repeat.level==1 && repeat.next==t+8);
  }
  assert(browser_repeat_update(&repeat,KEY_NEXT,0,399)==0);
  assert(browser_repeat_update(&repeat,KEY_NEXT,0,400)==KEY_NEXT);
  assert(repeat.level==2 && repeat.next==404);
  assert(browser_repeat_update(&repeat,KEY_NEXT,0,403)==0);
  assert(browser_repeat_update(&repeat,KEY_NEXT,0,404)==KEY_NEXT);

  assert(browser_repeat_update(&repeat,KEY_PREV,KEY_PREV,405)==KEY_PREV);
  assert(repeat.level==0 && repeat.next==455);
  assert(browser_repeat_update(&repeat,KEY_PREV,0,454)==0);
  assert(browser_repeat_update(&repeat,KEY_PREV,0,455)==KEY_PREV);
  assert(browser_repeat_update(&repeat,0,0,456)==0 && repeat.key==0);
  assert(browser_repeat_update(&repeat,KEY_PREV,KEY_PREV,457)==KEY_PREV);
  assert(repeat.level==0 && repeat.next==507);
  /* A quick tap may be released already when the LCD finishes drawing. */
  assert(browser_repeat_update(&repeat,0,KEY_NEXT,458)==KEY_NEXT);
  assert(repeat.key==0 && browser_repeat_update(&repeat,0,0,459)==0);
  assert(browser_repeat_update(&repeat,KEY_PREV|KEY_NEXT,0,460)==0);

  /* Time arithmetic must work across rollover and very long holds. */
  repeat=(browser_repeat_t){0};
  tick_t start=65520;
  assert(browser_repeat_update(&repeat,KEY_NEXT,KEY_NEXT,start)==KEY_NEXT);
  browser_repeat_update(&repeat,KEY_NEXT,0,(tick_t)(start+199));
  assert(repeat.level==0);
  browser_repeat_update(&repeat,KEY_NEXT,0,(tick_t)(start+200));
  assert(repeat.level==1);
  browser_repeat_update(&repeat,KEY_NEXT,0,(tick_t)(start+400));
  assert(repeat.level==2);
  for (unsigned t=404;t<140000;t+=4) {
    assert(browser_repeat_update(&repeat,KEY_NEXT,0,(tick_t)(start+t))==KEY_NEXT);
    assert(repeat.level==2);
  }
  /* A new press of the same direction restarts the initial delay. */
  tick_t now=repeat.next;
  assert(browser_repeat_update(&repeat,KEY_NEXT,KEY_NEXT,now)==KEY_NEXT);
  assert(repeat.level==0 && repeat.next==(tick_t)(now+50));
}

static int reference_compare(const void *aa,const void *bb) {
  const entry_t *a=aa,*b=bb;
  if (!native_order) {
    unsigned ga=(a->flags&E_DIR) ? 0 : (a->flags&E_IMAGE) ? 1 : 2;
    unsigned gb=(b->flags&E_DIR) ? 0 : (b->flags&E_IMAGE) ? 1 : 2;
    if (ga!=gb) return ga<gb ? -1 : 1;
    /* The production comparator also puts image-named directories first. */
    if (ga==0 && !!(a->flags&E_IMAGE)!=!!(b->flags&E_IMAGE))
      return (a->flags&E_IMAGE) ? -1 : 1;
    int name=strncmp((char *)a->filename,(char *)b->filename,16);
    if (name) return name;
  }
  return a->ordinal<b->ordinal ? -1 : a->ordinal>b->ordinal;
}
static void check_page(const entry_t *reference) {
  assert(browser_page.count<=browser_page.capacity);
  assert(browser_page.first+browser_page.count<=files);
  for (unsigned i=0;i<browser_page.count;i++)
    assert(!memcmp(browser_entry(i),reference+browser_page.first+i,sizeof(entry_t)));
}
static void test_pages(void) {
  for (unsigned mode=0;mode<3;mode++) {
    for (unsigned memory=0;memory<3;memory++) {
      setup(1000, memory==1 ? 1 : CONFIG_BUFFER_COUNT);
      native_order=mode==1; varied_names=mode!=2;
      if (memory==2) {
        for (unsigned i=0;i<CONFIG_BUFFER_COUNT;i++) buffers[i].allocated=i&1;
      }
      uint8_t original[CONFIG_BUFFER_COUNT]; unsigned free_count=0;
      for (unsigned i=0;i<CONFIG_BUFFER_COUNT;i++) {
        original[i]=buffers[i].allocated; free_count+=!original[i];
      }
      buffer_t *buf=alloc_system_buffer();
      assert(buf && !browser_allocate_cache(buf));
      assert(browser_page.capacity==free_count*ENTRIES_PER_BLOCK);
      if (CONFIG_BUFFER_COUNT==32 && memory==0) assert(browser_page.capacity>256);
      path_t path={0};
      entry_t *reference=calloc(files,sizeof(entry_t)); assert(reference);
      dh_t dh={0}; cbmdirent_t dent;
      for (unsigned i=0;i<files;i++) {
        assert(!next_match(&dh,NULL,NULL,NULL,0,&dent));
        reference[i].ordinal=i;
        reference[i].filesize=dent.blocksize;
        memcpy(reference[i].filename,dent.name,16);
        if (dent.typeflags==TYPE_DIR) reference[i].flags|=E_DIR;
        if (!native_order && dent.pvt.fat.realname[0]=='I') reference[i].flags|=E_IMAGE;
      }
      qsort(reference,files,sizeof(*reference),reference_compare);
      assert(!browser_load_page(&path,buf,NULL,false));
      uint32_t passed=0;
      for (;;) {
        assert(browser_page.total==files && browser_page.first==passed);
        check_page(reference);
        passed+=browser_page.count;
        if (passed==files) break;
        entry_t anchor=*browser_entry(browser_page.count-1);
        assert(!browser_load_page(&path,buf,&anchor,false));
      }
      assert(current_error==ERROR_OK);
      assert(!browser_load_page(&path,buf,NULL,true));
      for (;;) {
        check_page(reference);
        if (!browser_page.first) break;
        uint32_t end=browser_page.first;
        entry_t anchor=*browser_entry(0);
        assert(!browser_load_page(&path,buf,&anchor,true));
        assert(browser_page.first+browser_page.count==end);
      }
      uint32_t positions[]={1,2,1001,501,27,1000,0,UINT32_MAX};
      for (unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
        uint32_t mp=positions[i];
        assert(!browser_seek_position(&path,buf,&mp));
        uint32_t expected=positions[i]>files+1 ? files+1 : positions[i];
        assert(mp==expected);
        if (mp>=2) {
          assert(mp-2>=browser_page.first);
          assert(mp-2-browser_page.first<browser_page.count);
        }
        check_page(reference);
      }
      /* A directory shrinking beyond a former anchor is a defined fallback. */
      entry_t anchor=*browser_entry(browser_page.count-1);
      files=0;
      assert(!browser_load_page(&path,buf,&anchor,false));
      uint32_t mp=1234;
      assert(!browser_seek_position(&path,buf,&mp) && mp==1);
      browser_free_cache(); free_buffer(buf);
      for (unsigned i=0;i<CONFIG_BUFFER_COUNT;i++) assert(buffers[i].allocated==original[i]);
      assert(active_buffers==7 && current_error==ERROR_OK);
      free(reference);
    }
  }
  /* Drive the actual browser through every cache boundary in both directions. */
  setup(600,2);
  for (unsigned i=0;i<files+2;i++) push(KEY_NEXT);
  push(KEY_PREV);
  for (unsigned i=0;i<files+1;i++) push(KEY_PREV);
  push(KEY_SEL);
  menu_browse_files();
  assert(current_error==ERROR_OK && key_pos==key_count);
  assert(scans>=2*(files/(2*ENTRIES_PER_BLOCK)));
  for (unsigned i=0;i<files;i++) assert(seen[i]);
  verify_cleanup(2);

  /* Return from a subdirectory to an entry beyond the first cache window. */
  setup(600,2); root_files=600; navigation=true;
  for (unsigned i=0;i<502;i++) push(KEY_NEXT);
  push(KEY_SEL);
  push(KEY_NEXT); push(KEY_SEL); /* Parent entry in the child directory. */
  for (unsigned i=0;i<502;i++) push(KEY_PREV);
  push(KEY_SEL);
  menu_browse_files();
  assert(depth==0 && commands==2 && key_pos==key_count && current_error==ERROR_OK);
  verify_cleanup(2);
}

int main(void) {
  test_repeat();
  assert(sizeof(entry_t) == 23);
  /* A regular file closes the entire menu on FAT and inside an image. */
  for (unsigned image=0;image<2;image++) {
    setup(1, CONFIG_BUFFER_COUNT);
    native_order = image;
    push(KEY_NEXT); push(KEY_NEXT); push(KEY_SEL);
    assert(menu_browse_files());
    assert(commands==0 && key_pos==key_count && current_error==ERROR_OK);
    verify_cleanup(CONFIG_BUFFER_COUNT);
  }
  /* An image on FAT is entered; only selecting its regular file exits. */
  setup(1, CONFIG_BUFFER_COUNT);
  image_root = true;
  push(KEY_NEXT); push(KEY_NEXT); push(KEY_SEL);
  push(KEY_NEXT); push(KEY_NEXT); push(KEY_SEL);
  assert(menu_browse_files());
  assert(commands==1 && depth==1 && key_pos==key_count && current_error==ERROR_OK);
  verify_cleanup(CONFIG_BUFFER_COUNT);

  /* The explicit return entry still returns to the main menu. */
  setup(1, CONFIG_BUFFER_COUNT);
  push(KEY_SEL);
  assert(!menu_browse_files());
  verify_cleanup(CONFIG_BUFFER_COUNT);

  unsigned counts[] = {0, 1, 26, 27, 256, 342, 1000, 65540};
  for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
    setup(counts[i], CONFIG_BUFFER_COUNT);
    if (files) { push(KEY_PREV); push(KEY_NEXT); }
    push(KEY_SEL);
    menu_browse_files();
    assert(too_many==0 && current_error==ERROR_OK);
    if (files) assert(displayed>0);
    verify_cleanup(CONFIG_BUFFER_COUNT);
  }
  /* The handle shares its data area with entries: one free buffer suffices. */
  for (unsigned available=0;available<1;available++) {
    setup(27, available);
    menu_browse_files();
    assert(current_error == ERROR_NO_CHANNEL);
    verify_cleanup(available);
  }
  setup(1000,1);
  push(KEY_PREV); push(KEY_NEXT); push(KEY_SEL);
  menu_browse_files();
  assert(current_error==ERROR_OK && scans>=3);
  verify_cleanup(1);
  setup(1, CONFIG_BUFFER_COUNT);
  open_error = true;
  menu_browse_files();
  verify_cleanup(CONFIG_BUFFER_COUNT);
  setup(50, CONFIG_BUFFER_COUNT); read_error_at=7;
  menu_browse_files();
  assert(current_error==ERROR_DIR_ERROR);
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
  browser_page.sorted=true;
  assert(browser_compare(&a,&b)==0);
  test_pages();
  printf("LCD browser: %u buffers, paging, low memory, repeat, depth and sorting passed\n",
         CONFIG_BUFFER_COUNT);
}
