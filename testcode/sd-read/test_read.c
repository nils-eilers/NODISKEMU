#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef bool BOOL;
typedef enum { RES_OK, RES_ERROR, RES_PARERR } DRESULT;
#define MAX_CARDS 1
#define CARD_MMCSD 0
#define CARD_SDHC 1
#define CONFIG_SD_AUTO_RETRIES 3
#define CONFIG_SD_BLOCKTRANSFER
#define READ_SINGLE_BLOCK 0x51
#define STA_NOINIT 1
#define STA_NODISK 2
#define STA_PROTECT 4
#define _FS_READONLY 0
#define FALSE false
#define TRUE true

enum { DISK_OK, DISK_ERROR };
enum { GOOD, COMMAND_ERROR, TOKEN_ERROR, CRC_ERROR };
static uint8_t cardtype[1], disk_state, outcome, script[32], init_result;
static unsigned reads, initializations, crc_byte;
static DWORD sectors[32];

static uint8_t send_command(BYTE drive, BYTE command, DWORD sector) {
  assert(drive == 0 && command == READ_SINGLE_BLOCK && reads < 32);
  sectors[reads] = sector;
  outcome = script[reads++];
  crc_byte = 0;
  return outcome == COMMAND_ERROR ? 0xff : 0;
}
static uint8_t expect_byte(uint8_t value) {
  assert(value == 0xfe);
  return outcome != TOKEN_ERROR;
}
static void deselect_card(void) {}
static void spi_rx_block(BYTE *buffer, unsigned size) {
  assert(size == 512);
  memset(buffer, 0x40 + initializations, size);
}
static uint8_t spi_rx_byte(void) {
  return crc_byte++ % 2 == 0 ? 0x12 : outcome == CRC_ERROR ? 0x35 : 0x34;
}
static uint16_t crc_xmodem_block(uint16_t crc, const BYTE *buffer, unsigned size) {
  (void)buffer;
  assert(crc == 0 && size == 512);
  return 0x1234;
}
static void uart_putc(char c) { assert(c == 'X'); }
static uint8_t sd_initialize(BYTE drive) {
  assert(drive == 0);
  initializations++;
  if (!(init_result & (STA_NOINIT | STA_NODISK))) disk_state = DISK_OK;
  return init_result;
}

#include "production.c"

typedef struct _FATFS FATFS;
typedef struct {
  DWORD sect;
  BYTE dirty;
#if _USE_1_BUF
  FATFS *fs;
#endif
  BYTE data[512];
} BUF;
struct _FATFS { BYTE drive, n_fats; DWORD fatbase, sects_fat; };
static unsigned window_reads;
static bool window_error, write_error;
static DRESULT disk_read(BYTE drive, BYTE *buffer, DWORD sector, BYTE count) {
  (void)drive;
  assert(count == 1);
  window_reads++;
  memset(buffer, window_error ? 0xee : (BYTE)sector, 512);
  return window_error ? RES_ERROR : RES_OK;
}
static DRESULT disk_write(BYTE drive, const BYTE *buffer, DWORD sector, BYTE count) {
  (void)drive; (void)buffer; (void)sector; (void)count;
  return write_error ? RES_ERROR : RES_OK;
}

#include "window.c"

static void setup(uint8_t type) {
  memset(script, 0, sizeof(script));
  memset(sectors, 0, sizeof(sectors));
  reads = initializations = 0;
  init_result = 0;
  cardtype[0] = type;
  disk_state = DISK_OK;
}

int main(void) {
  BYTE buffer[1024];
  for (uint8_t type = CARD_MMCSD; type <= CARD_SDHC; type++) {
    DWORD sector = type == CARD_SDHC ? 23 : 23 * 512;
    DWORD step = type == CARD_SDHC ? 1 : 512;
    setup(type);
    assert(sd_read(0, buffer, 23, 2) == RES_OK);
    assert(reads == 2 && initializations == 0);
    assert(sectors[0] == sector && sectors[1] == sector + step);

    for (uint8_t failure = COMMAND_ERROR; failure <= TOKEN_ERROR; failure++) {
      setup(type);
      script[1] = failure; /* Fail after the first sector was transferred. */
      assert(sd_read(0, buffer, 23, 2) == RES_OK);
      assert(reads == 4 && initializations == 1 && disk_state == DISK_OK);
      assert(sectors[0] == sector && sectors[1] == sector + step);
      assert(sectors[2] == sector && sectors[3] == sector + step);
      for (unsigned i = 0; i < sizeof(buffer); i++) assert(buffer[i] == 0x41);

      setup(type);
      script[0] = script[1] = failure;
      assert(sd_read(0, buffer, 23, 1) == RES_OK);
      assert(reads == 3 && initializations == 2);

      setup(type);
      memset(script, failure, sizeof(script));
      assert(sd_read(0, buffer, 23, 1) == RES_ERROR);
      assert(reads == 3 && initializations == 2 && disk_state == DISK_ERROR);
    }

    setup(type);
    script[0] = CRC_ERROR;
    assert(sd_read(0, buffer, 23, 1) == RES_OK);
    assert(reads == 2 && initializations == 0 && sectors[0] == sectors[1]);

    setup(type);
    memset(script, CRC_ERROR, sizeof(script));
    assert(sd_read(0, buffer, 23, 1) == RES_ERROR);
    assert(reads == CONFIG_SD_AUTO_RETRIES && initializations == 0);

    for (uint8_t status = STA_NOINIT; status <= STA_PROTECT; status *= 2) {
      setup(type);
      script[0] = TOKEN_ERROR;
      init_result = status;
      assert(sd_read(0, buffer, 23, 1) == (status == STA_PROTECT ? RES_OK : RES_ERROR));
      assert(reads == (status == STA_PROTECT ? 2 : 1) && initializations == 1);
    }
  }
  setup(CARD_SDHC);
  assert(sd_read(MAX_CARDS, buffer, 23, 1) == RES_PARERR);
  assert(reads == 0 && initializations == 0);

  FATFS fs = {.n_fats = 1};
  BUF window = {.sect = 10};
#if _USE_1_BUF
  window.fs = &fs;
#endif
  window_error = true;
  assert(!move_window(&fs, &window, 20));
  assert(window.sect == 0 && window.data[0] == 0xee);
  window_error = false;
  assert(move_window(&fs, &window, 10));
  assert(window_reads == 2 && window.sect == 10 && window.data[0] == 10);

  /* A failed dirty-window write must preserve the data and its sector tag. */
  window.dirty = true;
  write_error = true;
  assert(!move_window(&fs, &window, 20));
  assert(window.sect == 10 && window.dirty && window.data[0] == 10);
  assert(window_reads == 2);
  printf("SD read recovery and FAT cache tests passed (shared buffer %d)\n", _USE_1_BUF);
}
