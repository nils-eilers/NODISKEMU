"""Build a host adapter around the actual d64ops.c formatting/BAM functions."""
import ctypes
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(text, name):
    m = re.search(r'^[^\n;{}]*\b' + name + r'\([^;{}]*\)\s*\{', text, re.M)
    if not m:
        raise ValueError(name)
    pos = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[m.start():pos]


class Formatter:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        source = (ROOT/'src/d64ops.c').read_text()
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define '))
        defines += '\n' + '\n'.join(x for x in (ROOT/'src/d64ops.h').read_text().splitlines()
                                   if x.startswith('#define D64_TYPE_'))
        c = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
'''+defines+r'''
#define BUFFER_SEC_SYSTEM 100
#define BUFFER_SYS_BAM 101
#define ERROR_OK 0
#define PROGMEM
static unsigned max_part=1;
static unsigned current_error;
static uint8_t image[1066496];
static size_t image_size;
static int write_fail=-1, read_fail=-1;
typedef struct buffer_s {
 uint8_t data[256];
 uint8_t allocated,secondary,sticky,write,dirty,mustflush;
 uint8_t (*cleanup)(struct buffer_s *);
 struct { struct {uint8_t part,track,sector;} bam;} pvt;
} buffer_t;
static buffer_t pool[6], *bam_buffer, *bam_buffer2;
typedef struct {struct {uint8_t track,sector;} dxx;} dir_t;
typedef struct {uint8_t part;dir_t dir;} path_t;
typedef enum {DIR_TRACK,DIR_START_SECTOR,LAST_TRACK,LABEL_OFFSET,ID_OFFSET,
              FILE_INTERLEAVE,DIR_INTERLEAVE} param_t;
struct param_s {
 uint8_t dir_track,dir_start_sector,last_track,label_offset,id_offset;
 uint8_t file_interleave,dir_interleave;
 void (*format_function)(uint8_t,buffer_t *,uint8_t *,uint8_t *);
};
static struct {uint8_t imagetype;dir_t current_dir;struct param_s d64data;} partition[1];
typedef enum {BAM_BITFIELD,BAM_FREECOUNT} bamdata_t;
static void set_error(unsigned e) {current_error=e;}
static uint8_t image_write(uint8_t p,uint32_t offset,const void *data,unsigned n,uint8_t flush) {
 (void)p;(void)flush;
 assert(offset+n<=image_size);
 if(write_fail==0) {set_error(25);return 1;}
 if(write_fail>0)write_fail--;
 memcpy(image+offset,data,n);return 0;
}
static uint8_t image_read(uint8_t p,uint32_t offset,void *data,unsigned n) {
 (void)p;assert(offset+n<=image_size);
 if(read_fail==0){set_error(20);return 1;}
 if(read_fail>0)read_fail--;
 memcpy(data,image+offset,n);return 0;
}
static buffer_t *alloc_system_buffer(void) {
 for(unsigned i=0;i<6;i++)if(!pool[i].allocated) {
  memset(pool+i,0,sizeof(pool[i]));pool[i].allocated=1;return pool+i;
 }
 assert(0);return NULL;
}
static buffer_t *alloc_buffer(void) {return alloc_system_buffer();}
static void stick_buffer(buffer_t *b) {b->sticky=1;}
static void unstick_buffer(buffer_t *b) {b->sticky=0;}
static void mark_write_buffer(buffer_t *b) {b->write=1;}
static void mark_buffer_dirty(buffer_t *b) {b->dirty=1;}
static void strnsubst(uint8_t *s,unsigned n,uint8_t from,uint8_t to) {
 while(n--){if(*s==from)*s=to;s++;}
}
'''
        funcs = ['get_param','sector_lba','sector_offset','sectors_per_track',
                 'clear_dir_sector','bam_buffer_flush','d64_bam_commit',
                 'bam_buffer_alloc','bam_buffer_swap','bam_buffer_match','move_bam_window',
                 'is_free','sectors_free','allocate_sector','free_sector',
                 'read_string_from_dirheader','d64_getid','d64_freeblocks',
                 'format_copy_label','format_d80_image','format_d82_image','d64_format']
        c += '\n\n'.join(function(source, name) for name in funcs)
        c += r'''
void setup(int type,int fill) {
 memset(pool,0,sizeof(pool));bam_buffer=NULL;bam_buffer2=NULL;
 current_error=0;write_fail=read_fail=-1;
 partition[0].imagetype=type;
 partition[0].d64data=(struct param_s){39,1,type==D64_TYPE_D80?77:154,6,24,5,3,
  type==D64_TYPE_D80?format_d80_image:format_d82_image};
 image_size=type==D64_TYPE_D80?533248:1066496;
 memset(image,fill,image_size);assert(!bam_buffer_alloc(&bam_buffer));
}
void reset_cache(void) {
 assert(!d64_bam_commit());memset(pool,0,sizeof(pool));bam_buffer2=NULL;
 assert(!bam_buffer_alloc(&bam_buffer));
}
int run_format(uint8_t *name,uint8_t *id) {current_error=0;d64_format(0,name,id);return current_error;}
int commit(void) {return d64_bam_commit();}
unsigned blocks_free(void) {return d64_freeblocks(0);}
uint8_t *image_data(void) {return image;}
size_t image_length(void) {return image_size;}
void fail_write(int n) {write_fail=n;}
void fail_read(int n) {read_fail=n;}
int allocate(unsigned t,unsigned s) {return allocate_sector(0,t,s);}
'''
        path = Path(self.tmp.name)
        (path/'format.c').write_text(c)
        subprocess.run(['cc','-std=c99','-shared','-fPIC','-Wall','-Wextra','-Werror',
                        '-fsanitize=undefined','-fno-sanitize-recover=all',
                        str(path/'format.c'),'-o',str(path/'format.so')],check=True)
        self.lib = ctypes.CDLL(str(path/'format.so'))
        self.lib.run_format.argtypes = [ctypes.c_char_p,ctypes.c_char_p]
        self.lib.image_data.restype = ctypes.POINTER(ctypes.c_uint8)
        self.lib.image_length.restype = ctypes.c_size_t

    def image(self):
        return ctypes.string_at(self.lib.image_data(), self.lib.image_length())

    def load(self, data):
        assert len(data) == self.lib.image_length()
        ctypes.memmove(self.lib.image_data(), data, len(data))

    def close(self):
        self.tmp.cleanup()
