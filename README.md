NODISKEMU
=========

SD/MMC to IEEE-488 interface/controller
---------------------------------------

Introduction
------------

NODISKEMU is firmware, used in hardware designs like petSD, petSD+ or
Dave Curran's pet microSD that allows the IEEE-488 bus to access
SD cards  - think of it as a CBM 2031 with a modern storage medium instead
of disks. The project was inspired by (and uses a few bits of code from)
MMC2IEC[1] by Lars Pontoppidan and once ran on the same hardware before it
grew too big for the ATmega32 used there.

Currently, the firmware provide good DOS and file-level compatibility with CBM
drives, but much work remains.  Unless specifically noted, anything that tries
to execute code on the device will not work, this includes every software
fastloader.

NODISKEMU is a fork of sd2iec[2] by Ingo Korb (et al.) and is not intended to
be used on devices equipped with a Commodore serial bus, such as MMC2IEC,
SD2IEC or uIEC. Though most code still persists, compilation for these targets
may break at any time or is already broken. You better keep using sd2iec
for those.

1. http://pontoppidan.info/lars/index.php?proj=mmc2iec
2. http://sd2iec.de


Are you kidding me?
-------------------

This actually **IS** a disk emulator, isn't?

NODISKEMU does not emulate any particular vintage Commodore disk drive.
It neither emulates a 6502 CPU nor does it run any Commodore DOS.
It's just a storage solution that is capable to interact on low level
functions such as OPEN or TALK and interprets commands sent to channel 15
in a way similar to other drives.

The deeper meaning of this name is to clarify that there actually are
and ever will be some differences compared to real floppy drives.


Supported cards
---------------
MMC, SD and SHDC cards (resp. microSD/microSDHC cards) are supported,
formatted with either FAT16 or FAT32.

SDXC, microSDXC or ex-FAT won't work.

If you card refuses to work, try to format it with SD Formatter 4.0,
freely available for Windows and Mac from
https://www.sdcard.org/downloads/formatter_4/


Compatibility (IEEE-488)
------------------------

### Known good ###

Devices with IEEE-488 bus are compatible with any CBM/PET computer
equipped with either BASIC 2, BASIC 4 or BSOS.

The CBM-II series are fully supported, however you might want to
upgrade to a recent KERNAL because the older ones contain a couple
of bugs. The PROXA 7000 extension is fully supported.

OS-9 on the SuperPET is reported to boot from the FAT filesystem, but not
from disk images.

### Issues ###

The original PET equipped with BASIC 1 is not compatible because of its
hopelessly broken IEEE routines.

There are known issues with printer interfaces by Ultra Eletronic or
several clones of them, including mine.

### Untested ###

Tests are still missing for the Z-RAM card (a Z80 CP/M board)
and the softbox by Small Systems Engineering, Ltd. which is a Z80 CP/M
computer attached to the IEEE-488 bus.


Directories
-----------

Displaying directories works as usual, either the BASIC 2 way with

```
LOAD"$",8
LIST
```

or `CATALOG` for BASIC 4. You can abbreviate to `cA` and specify the
unit address:

```
cAu9
```

There are however some more advanced features:

### Directory filters ###

To show only directories, both =B (CMD-compatible) and =D can be used.
On a real Commodore drive D matches everything.
To include hidden files in the directory, use *=H - on a 1541 this doesn't
do anything. NODISKEMU marks hidden files with an H after the lock mark,
i.e. "PRG<H" or "PRG H".

CMD-style "short" and "long" directory listings with timestamps are supported
("$=T"), including timestamp filters. Please read a CMD manual for a complete
description of the syntax, two basic examples are provided here:

Short format:
```
LOAD"$=T",8
LIST
```

Long format:
```
LOAD"$=T:*=L",8
LIST
```

### Partition directory ###

The CMD-style partition directory ($=P) is supported, including filters
($=P:S*). All partitions are listed with type "FAT", although this could
change to "NAT" later for compatibility.

### Printing the directory ###

This example assumes a printer attached as unit 4:

```
LOAD "$",8
OPEN 4,4:CMD 4
LIST
PRINT#4:CLOSE 4
```


Sending Disk Commands
---------------------

BASIC 2 has only a very limited number of disk commands built-in:
`LOAD, SAVE, VERIFY, OPEN, CLOSE, GET#, INPUT#` and `PRINT#`.
For anything else beyond basic loading and saving programs and displaying
the directory, you have to send disk commands to the command channel 15
of the drive.

This can be done in two ways, either by appending the command to the OPEN
command or by sending it separately with the PRINT# command. An an example:

```
OPEN1,8,15,"CD:DIRNAME":CLOSE1
```
This opens the logical channel 1 on device 8 and sends the string `CD:DIRNAME`
to the command channel 15.

BASIC 4 adds some more advanced commands such as `CATALOG`. BASIC parses the
given parameters and builds a command string which is then sent to the command
channel 15, so it's just a more convenient wrapper for sending disk commands
but it doesn't add any DOS functions, they're still all processed inside the
disk drive.

However, even though BASIC 4 knows some more disk commands, they're kind of
limited in their usage. The most advanced and most comfortable way is to use a
so called DOS wedge. It cannot be used inside programs, only in direct mode.

Preceed any disk command with a `@` or a `>`, everything after that will be
sent to the command channel 15, e.g.:

```
@CD:DIRNAME
```

A loadable DOS wedge (as a terminate and stay resident program) is available
from http://petsd.net/wedge.php for CBM/PET computers with BASIC 2 and BASIC 4
and from http://cbm2wedge.sourceforge.net for the CBM-II series.

Better yet, get an updated editor ROM with built-in wedge[1] or upgrade your
CBM 8296 to BSOS[2] to have the wedge available right from the start after
power-on.

1. http://www.6502.org/users/sjgray/projects/editrom
2. https://github.com/Edilbert/BSOS


Retrieving the Disk Status
--------------------------

If an error occurs, a red LED of the drive flashes to indicate an error. To
get the error message, you have to retrieve the disk status by reading from
channel 15.

Unfortunately, BASIC 2 has no built-in command for this purpose, so it's
rather cumbersome:

```
10 OPEN1,8,15
20 INPUT#1,E,E$,T,S
30 PRINT E;E$;T;S
40 CLOSE1
```

`INPUT` doesn't work in direct mode, so you'll really have to write a short
program to retrieve and display the disk status.

BASIC 4 does it better, it includes the pseudo variables `ds` and `ds$` which
it updates after disk operations from status channel 15. However, it misses
some changes occasionally so if you depend on really getting the current disk
status you're stuck with the old-fashioned way.

Again, the most comfortable way is to use a DOS wedge. Just hit `@` followed
by RETURN and you'll get always the current disk status.


Long File Names
---------------
Long file names (i.e names not within the 8.3 limits) are supported on
FAT, but for compatibility reasons the 8.3 name is used if the long
name exceeds 16 characters. If you use anything but ASCII characters
on the PC or their PETSCII equivalents on the Commodore you may
get strange characters on the other system because the LFN use
Unicode characters on disk, but NODISKEMU parses only the low byte
of each character in the name.


Valid FAT Filenames
-------------------
Valid characters for filenames on the FAT filesystem have an ASCII code
greater or equal 32 (space) and less or equal 126 ('~'). In general,
no control codes are allowed, only 7 bit ASCII characters minus some
special characters. These forbidden characters are:

        " * , / \ : = ? @


Furthermore, neither space nor the full stop ('.') are not allowed as first
or last character of a filename. If a filename contains any illegal
characters, the creation of X00 files is forced even if XE mode 0 is set.


Changing the Device Address
---------------------------
After power on, the device address gets restored from the EEPROM. If the
device has jumpers or switches to set the address, it will take this
setting if a change is detected compared to the last setting during power-on.

There are up to three ways to change the device address:

1. U0 command (see there)
2. Hardware (jumper or switches)
3. LCD menu system

Hardware jumpers are only read after power-on, a reset or power
cycle is required to apply any changes.

If there are conflicting situations (e.g. hardware jumpers set to address 10
but address 9 stored in EEPROM), the last change or command sets the address.


EEPROM file system
------------------
**WARNING**: The EEPROM file system is a newly-implemented file system
that may still contain bugs. Do not store data on it that you cannot
afford to lose. Always make sure that you have a backup. Also, the
format may change in later releases, so please expect that the
partition may need to be erased in the future.

Devices running NODISKEMU always have an EEPROM to store the system
configuration, but on some devices this EEPROM is much larger than
required. To utilize the empty space on these devices (currently any
microcontroller with at least 128K of flash), a special EEPROM file
system has been implemented. This can for example be used to store a
small file browser or fast loader so it can be used independent of the
storage medium that is currently inserted.

The EEPROM file system will always register itself on the last
partition number (see "Partitions" below). You can check the list of
partitions ("$=P") to find the current partition number of the EEPROM
file system or use the alias function (see below) to access it.

To simplify calculations, block numbers on the EEPROMFS are calculated
using 256 bytes per block instead of the usual 254 bytes as used by
Commodore drives. Internally, the allocation is even more fine-grained
(using 64 byte sectors), which means that the number of free blocks
shown on an empty file system may be less than the sum of the number
of blocks of all files on a full file system.

The EEPROM file system does not support subdirectories. It can be
formatted using the N: command as usual, but the disk name and ID are
ignored. The capacity of the EEPROM file system varies between
devices: On AVR devices it is 3.25 KBytes and at most 8 files can be
stored on it. On a2iec, the file system can hold 7 KBytes and at most
16 files can be stored on it. The actual number of files that can be
stored depends on the length of the files, longer files need more than
one directory entry.


Partitions
----------

NODISKEMU features a multi-partition support similar to that of the CMD
drives. The partitions (which may be on separate drives for some hardware
configurations) are accessed using the drive number of the commands
sent from the computer and are numbered starting with 1. Partition 0
is a special case: Because most software doesn't support drive numbers
or always sends drive number 0, this partition points to the currently
selected partition. By default, accesses to partition 0 will access
partition 1, this can be changed by sending "CP<num>" over the command
channel with <num> being an ASCII number from 1 to 255. "C<Shift-P"
(0x42 0xd0) works the same, but expects a binary partition number as the
third character of the command.

To allow a "stable" access to the EEPROM file system no matter how
many partitions are currently available, a special character has been
introduced that will always access the EEPROM file system (if
available). When NODISKEMU sees a "!" character where it expects a
partition number and the "!" character is directly followed by a colon
(i.e. "!:"), it will access the EEPROMFS if available. Direct access
using the assigned partition number is of course still
available. Additionally "$!" will always load the directory of the
EEPROM file system partition (if available), similar to "$1" to "$9"
for partitions 1 to 9.


x00 files
---------

P00/S00/U00/R00 files are transparently supported, that means they show
up in the directory listing with their internal file name instead of the
FAT file name. Renaming them only changes the internal name. The XE
command defines if x00 extensions are used when writing files, by
default NODISKEMU doesn't create x00 container files.

The creation of x00 files is forced if the filename contains
characters that would be illegal for FAT filenames. Spaces and dots are only
allowed within the filename but not as the first or last character.

Parsing of x00 files is always enabled even when writing them is not.

x00 files are recognized by checking both the extension of the file
(P/S/U/R with a two-digit suffix) and the header signature.


Disk Images
-----------

Disk images are recognized by their file extension (.D64, .D41, .D71, .D81,
.DNP) and their file size (must be one of 174848, 175531, 349696, 351062,
819200 or a multiple of 65536 for DNP). If the image has an error info block
appended it will be used to simulate read errors. Writing to a sector with
an error will always work, but it will not clear the indicated error.
D81 images with error info blocks are not supported.

Warning: There is at least one program out there (DirMaster v2.1/Style by
THE WIZ) which generates broken DNP files. The usual symptom is that
moving from a subdirectory that was created with this program back to
its parent directory using CD:_ (left arrow) sets the current directory
not to the parent directory, but to an incorrect sector instead. A
workaround for this problem in NODISKEMU would require an unreasonable
amount of system resources, so it is recommended to avoid creating
subdirectories with this version of DirMaster. It is possible to fix
this problem using a hex editor, but the exact process is beyond the scope
of this document.

REL files
---------

Partial REL file support is implemented. It should work fine for existing
files, but creating new files and/or adding records to existing files
may fail. REL files in disk images are not supported yet, only as files
on a FAT medium. When x00 support is disabled the first byte of a REL
file is assumed to be the record length.


Large buffers
-------------

To support commands which directly access the storage devices support
for larger buffers was added. A large buffer can be allocated by
opening a file named "##<d>" (exactly three characters" with <d> replaced
by a single digit specifying the number of 256-byte buffers to be
chained into one large buffer - e.g. "##2" for a 512 byte buffer,
"##4" for 1024 bytes etc. Unlike a standard buffer where the read/write
pointer is set to byte 1, a large buffer will start with the r/w pointer
pointing to byte 0 because that seems to be more sensible to the author.

If there aren't enough free buffers to support the size you requested
a 70,NO CHANNEL message is set in the error channel and no file is
opened. If the file name isn't exactly three bytes long a standard
buffer ("#") will be allocated instead for compatibility.

The B-P command supports a third parameter that holds the high byte
of the buffer position, For example, "B-P 9 4 1" positions to byte
260 (1\*256+4) of the buffer on secondary address 9.


Diagnostics
-----------

Because some SD slots seem to suffer from bad/unreliable card detect
switches a test mode for this has been implemented on the units that
have SD card support. To enable this test mode, hold down the PREV
button during power-up.

Devices with built-in LCD support (read: petSD+) will show their
diagnostics on the display if the menu system is enabled (see also:
XM command).

The further notes apply only for devices without built-in LCD support
(read: old petSD and pet microSD):

The red (dirty) LED will reflect the card detect status - if the LED
is on the card detect switch is closed. Please note that this does not
indicate successful communication with the card but merely that the
mechanical switch in the SD card slot is closed.

On units with two NODISKEMU-controlled LEDs, the green (busy) LED will
indicate the state of the write protect switch - if the LED is lit,
the write protection is on. Due to the way the write protect notch
works on SD cards, the indication is only valid when the card is fully
inserted into the slot.

To exit from the diagnostic mode, power-cycle the device or push the
NEXT button once.


Other important notes
---------------------

- When you hold down the disk change (forward) button during power
  on the software will use default values instead of those stored
  in the EEPROM. (TODO: currently deactivated)
- File overwrite (@foo) is implemented by deleting the file first.
- File sizes in the directory are in blocks (of 254 bytes), but
  the blocks free message actually reports free clusters. It is
  a compromise of compatibility, accuracy and code size.
- If known, the low byte of the next line link pointer of the directory
  listing will be set to (filesize MOD 254)+2, so you can calculate the
  true size of the file if required. The 2 is added so it can never be
  mistaken for an end marker (0) or for the default value (1, used by at
  least the 1541 and 1571 disk drives).
- If your hardware supports more than one SD card, changing either one
  will reset the current partition to 1 and the current directory of
  all partitions to the root drive. Doing this just for the card that
  was changed would cause lots of problems if the number of partitions
  on the previous and the newly inserted cards are different.
- If you are the author of a program that needs to detect NODISKEMU for
  some reason, DO NOT use M-R for this purpose. Use the UI command
  instead and check the message you get for "nodiskemu" instead.


Reference of Disk Commands
--------------------------

### B-P ##

        B-P: channel position

Set the Buffer-Pointer associated with the given channel to a new value.
`B-P: 2 1` sets channel 2 pointer to the beginning of the data area in the
direct access buffer.


### B-R/B-W ###

        B-R: channel drive track sector
        B-W: channel drive track sector

Reads and writes blocks the clumsy way. Use U1/U2 instead for new programs.


### C ###

File copy command, should be CMD compatible. The syntax is

        C[partition][path]:targetname=[[partition][path]:]sourcename[,[[p][p]:]sourcename...]

You can use this command to copy multiple files into a single target
file in which case all source files will be appended into the target
file. Parsing restarts for every source file name which means that
every source name is assumed to be relative to the current directory.
You can use wildcards in the source names, but only the first
file matching will be copied.

Copying REL files should work, but isn't tested well. Mixing REL and
non-REL files in an append operation isn't supported.


### CD ###

CD (change directory) is used to select the current directory and to
mount or unmount disk images. Subdirectory access is compatible to the
syntax used by the CMD drives, although drive/partition numbers are
completely ignored.

Quick syntax overview:

Command        | Remarks
---------------|------------------------------------------------------------
`CD:_`         |changes into the parent dir (_ is the left arrow on the PET)
`CD_`          |ditto
`CD:foo`       |changes into foo
`CD/foo`       |ditto
`CD//foo`      |changes into \foo
`CD/foo/:bar`  |changes into foo\bar
`CD/foo/bar`   |ditto

You can use wildcards anywhere in the path. To change into an disk
image the image file must be the last component in the path, either
after a slash or a colon character.

To mount/unmount image files, change into them as if they were a directory
and use CD:_ (left arrow on the PET) to leave.
Please note that image files are detected by file extension and file size
and there is no reliable way to see if a file is a valid image file.

### CP, C\<Shift-P> ###

This changes the current partition, see "Partitions" below for details.

### D ###

Direct sector access, this is a command group introduced by NODISKEMU.
Some Commodore drives use D for disk duplication between two drives
in the same unit, an attempt to use that command with NODISKEMU should
result in an error message.

D has three subcommands: DI (Info), DR (Read) and DW (Write).
Each of those commands requires a buffer to be opened (similar
to U1/U2), but due to the larger sector size of the storage devices
used by NODISKEMU it needs to be a large buffer of size 2 (512 bytes)
or larger. The exception is the DI command with page set to 0,
its result will always fit into a standard 256 byte buffer.
If you try to use one of the commands with a buffer that is too
small a new error message is returned, "78,BUFFER TOO SMALL,00,00".

In the following paragraphs the secondary address that was used
to open the buffer is called "bufchan".

### DI ###
In BASIC notation the command format is

        "DI"+chr$(bufchan)+chr$(device)+chr$(page)

`device` is the number of the physical device to be queried,
`page` the information page to be retrieved. Currently the
only page implemented is page 0 which will return the
following data structure:

     1 byte : Number of valid bytes in this structure
              This includes this byte and is meant to provide
              backwards compatibility if this structure is extended
              at a later time. New fields will always be added to
              the end so old programs can still read the fields
              they know about.
     1 byte : Highest diskinfo page supported
              Always 0 for now, will increase if more information
              pages are added (planned: Complete ATA IDENTIFY
              output for IDE and CSD for SD)
     1 byte : Disk type
              This field identifies the device type, currently
              implemented values are:
                0  IDE
                2  SD
                3  (reserved)
     1 byte : Sector size divided by 256
              This field holds the sector size of the storage device
              divided by 256.
     4 bytes: Number of sectors on the device
              A little-endian (same byte order as the 6502) value
              of the number of sectors on the storage device.
              If there is ever a need to increase the reported
              capacity beyond 2TB (for 512 byte sectors) this
              field will return 0 and a 64-bit value will be added
              to this diskinfo page.

If you want to determine if there is a device that responds
to a given number, read info page 0 for it. If there is no
device present that corresponds to the number you will see
a DRIVE NOT READY error on the error channel and the
"number of valid bytes" entry in the structure will be 0.

Do not assume that device numbers are stable between releases
and do not assume that they are continuous either. To scan
for all present devices you should query at least 0-7 for now,
but this limit may increase in later releases.


### DR/DW ###

In BASIC notation the command format would be
```
      "DR"+chr$(bufchan)+chr$(device)
          +chr$(sector AND 255)
          +chr$((sector/256) AND 255)
          +chr$((sector/65536) AND 255)
          +chr$((sector/16777216) AND 255)
```
(or "DW" instead of "DR)

but this won't work on the C64 because AND does not accept
parameters larger than 32767. The principle should be clear
though, the last four bytes are a 32 bit sector number in
little-endian byte order.

DR reads the sector to the buffer, DW writes the contents
of the buffer to the sector. Both commands will update the
error channel if an error occurs, for DR the 20,READ ERROR
was chosen to represent read errors; for write problems
during DW it sets 25,WRITE ERROR for errors and 26,WRITE
PROTECT ON if the device is read-only.


### G-P ###

Get partition info, see CMD FD/HD manual for details. The reported
information is partially faked, feedback is welcome.


### MD ###

Makes (creates) a directory. MD uses a syntax similar to CD and
will create the directory listed after the colon (:) relative to
any directory listed before it.

`MD/foo/:bar` creates bar in foo

`MD//foo/:bar` creates bar in \foo


### M-R, M-W, M-E ###

When no file is set up using XR, M-R will check a small internal
table of common drive-detection addresses and return data that
forces most of the supported fast loaders into a compatible mode
(e.g. 1541 mode for Dreamload and ULoad Model 3, disabled
fastloader for Action Replay 6). If the address is not
recognized, more-or-less random data will be returned.

  Unfortunately GEOS reads rather large parts of the drive ROM
using M-R to detect the drive, which cannot be reasonably added
into the internal table. To enable the GEOS drive detection to
work properly with NODISKEMU and to allow switching between
1541/71/81 modes, file-based M-R emulation has been implemented.
If a file has been set up as M-R data source using the XR
command, its contents will be returned for M-R commands that try
to read an address in the range of $8000-$ffff. The ROM file
should be a copy of the ROM contents of a 1541/71/81 drive (any
headers will be skipped automatically), its name must be 16
characters or less. When an M-R command is received, the file
will be searched in three locations on the storage medium:

1. in the current directory of the current partition
2. in the root directory of the current partition
3. in the root directory of the first partition

The internal emulation table will be used if the file wasn't found
in any of those locations or an error occurred while reading
it. Please be aware that the ROM file is ONLY used for M-R
commands. Except for some very specific situations where drive
detection fails (e.g. GEOS) it will probably decrease compatibility
of NODISKEMU because most of the implemented fast loaders will only
recognize the 1541 variation of the loader.

Memory writing knows about the address used for changing the device
address on a 1541 and will change the address of NODISKEMU to the
requested value. It will also check if the transmitted data
corresponds to any of the known software fastloaders so the correct
emulation code can be used when M-E is called.


### N ###

New (format) a disk image. The corresponding BASIC 4 command is
`HEADER`.

Format works only if a disk image is already mounted. This command will
be ignored for DNP images unless the current directory is the root
directory of the DNP image.

Formatting D80 and D82 disk images is not supported yet.


### P ###

Positioning doesn't just work for REL files but also for regular
files on a FAT partition. When used for regular files the format
is `"P"+chr$(channel)+chr$(lo)+chr$(midlo)+chr$(midhi)+chr$(hi)`
which will seek to the 0-based offset `hi*2^24+midhi*65536+256*midlo+lo`
in the file. If you send less than four bytes for the offset, the
missing bytes are assumed to be zero.


### R ###

Renames a file. Notice that the new name comes first!

        R:NEWNAME=OLDNAME

Renaming files should work the same as it does on CMD drives, although
the errors flagged for invalid characters in the name may differ.


### RD ###

RD (remove directory) can only remove subdirectories of the current directory.

`RD:foo` deletes foo


### S ###

Scratches (deletes) a file.

`S:DELETEME,METOO' deletes `DELETEME` and `METOO`.

Name matching is fully supported, directories are ignored.
Scratching of multiple files separated by , is also supported with no
limit to the number of files except for the maximum command line length
(usually 100 to 120 characters).


### T-R and T-W ###

If your hardware features RTC support the commands T-R (time read) and T-W
(time write) are available. If the RTC isn't present, both commands return
30,SYNTAX ERROR,00,00; if the RTC is present but not set correctly T-R will
return 31,SYNTAX ERROR,00,00.

Both commands expect a fourth character that specifies the time format
to be used. T-W expects that the new time follows that character
with no space or other characters in between. For the A, B and D
formats, the expected input format is exactly the same as returned
by T-R with the same format character; for the I format the day of
week is ignored and calculated based on the date instead.

The possible formats are:

- "A"SCII: "SUN. 01/20/08 01:23:45 PM"+CHR$(13)
  The day-of-week string can be any of "SUN.", "MON.", "TUES", "WED.",
  "THUR", "FRI.", "SAT.". The year field is modulo 100.

- "B"CD or "D"ecimal:
   Both these formats use 9 bytes to specify the time. For BCD everything
   is BCD-encoded, for Decimal the numbers are sent/parsed as-is.

Byte        | Value
------------|----------------------------------------------------------------
           0| Day of the week (0 for Sunday)
           1| Year (modulo 100 for BCD; -1900 for Decimal, i.e. 108 for 2008)
           2| Month (1-based)
           3| Day (1-based)
           4| Hour   (1-12)
           5| Minute (0-59)
           6| Second (0-59)
           7| AM/PM-Flag (0 is AM, everything else is PM)
           8| CHR$(13)


When the time is set a year less than 80 is interpreted as 20xx.

- "I"SO 8601 subset: "2008-01-20T13:23:45 SUN"+CHR$(13)
  This format complies with ISO 8601 and adds a day of week
  abbreviation using the same table as the A format, but omitting
  the fourth character. When it is used with T-W, anything beyond
  the seconds field is ignored and the day of week is calculated
  based on the specified date. The year must always be specified
  including the century if this format is used to set the time.
  To save space, NODISKEMU only accepts this particular date/time
  representation when setting the time with T-WI and no other ISO
  8601-compliant representation.


### U0 ###

  Device address changing with "U0>"+chr$(new address) is supported,
  other U0 commands are currently not implemented.


### U1/U2 ###

Block reading and writing is fully supported while a disk image is mounted.

`U1 channel drive track sector` reads a block,
`U2 channel drive track sector` writes a block


### UI/UJ ###

Soft/Hard reset - UI just sets the "73,..." message on the error channel,
UJ closes all active buffers but doesn't reset the current directory,
mounted image, swap list or anything else.


### U < Shift-J > ###

Real hard reset - this command causes a restart of the AVR processor
(skipping the bootloader if installed). < Shift-J > is character code 202.


X: Extended commands
--------------------

### XEnum ###

Sets the "file extension mode". This setting controls if
files on FAT are written with an x00 header and extension or not.
Possible values for num are:


        0: Never write x00 format files.
        1: Write x00 format files for SEQ/USR/REL, but not for PRG
        2: Always write x00 format files.
        3: Use SEQ/USR/REL file extensions, no x00 header
        4: Same as 3, but also for PRG

If you set mode 3 or 4, extension hiding is automatically enabled.
This setting can be saved in the EEPROM using XW, the default
value is 0.

For compatibility with existing programs that write D64 files,
PRG files that have D64, D41, D71, D81, D80, D82 or DNP as an extension
will always be written without an x00 header and without
any additional PRG file extension.


### XE+ / XE- ###
Enable/disable extension hiding. If enabled, files in FAT with
a PRG/SEQ/USR/REL extension will have their extension removed
and the file type changed to the type specified by the file
extension - e.g. APPLICATION.PRG will become a PRG file named
"APPLICATION", "README.SEQ" will become a SEQ file named "README".
This flag can be saved in the EEPROM using XW, the default
value is disabled (-).


### XInum ###
Switches the display mode for disk images.
num can be 0, in which case the file will be shown
with its normal type in the directory or 1 which will show all
mountable files as DIRectory entries (but they can still be
accessed as files too) or 2 in which case they will show up
twice - once with its normal type and once as directory.
The default value is 0 and this setting can be stored
permanently using XW.

It may be useful to set it to 1 or 2 when using software that
was originally written for CMD devices and which wouldn't
recognize disk images files as mountable on its own.
However, due to limitations of the current implementation of
the CD command such software may still fail to mount a disk
image with this option enabled.


### X\*+ / X\*-  ###
Enable/disable 1581-style * matching. If enabled, characters
after a * will be matched against the end of the file name.
If disabled, any characters after a * will be ignored.
This flag can be saved in the EEPROM using XW, the default value
is enabled (+).


###XDdrv=val ###
Configure drives.  On ATA-based units or units with multiple
drive types, this command can be used to enable or reorder
the drives.  drv is the drive slot (0-7), while val is one
of:

                0: Master ATA device
                1: Slave ATA device
                4: Primary SD/MMC device
                5: Secondary SD/MMC device
                6: (reserved)
               15: no device

Note that only devices supported by the specific hardware
can be selected.  Unsupported device types will return an
error if requested.  Also, note that you cannot select a device
in multiple drive slots.  Finally, while it is possible to
re-order ATA devices using this functionality, it is strongly
discouraged.  Use the master/slave jumpers on the ATA devices
instead.  To reset the drive configuration, set all drive slots
to "no device".  This value can be permanently saved in the
EEPROM using XW.


### XD? ###

View the current drive configuration.  Example result:
`03,D:00=04:01=00:02=01,10,01`  The track indicates the
current device address, while the sector indicates extended
drive configuration status information.

### XM+ / XM- ###
Enable/disable the LCD menu system, only available on some devices. If
disabled, signal lines used for buttons may get used to read the device
address from DIP switches. This command automatically saves the settings
into the EEPROM and restarts the device to apply them. The default value
is enabled (+).


### X ###
X without any following characters reports the current state
of all extended parameters via the error channel, similar
to DolphinDOS. Example result: "03,J-:C152:E01+:B+:*+,08,00"
The track indicates the current device address.


### XS:name ###

Set up a swap list - see "Changing Disk Images" below.


### XS ###
Disable swap list


### XR:name ###
Set the file used for file-based M-R emulation.


### XR ###
Disable file-based M-R emulation.

See "M-R, M-W, M-E" below. This setting can be
 permanently saved in the EEPROM using XW.


### XW ###

Store configuration to EEPROM

This commands stores the current configuration in the EEPROM.
It will automatically be read when the AVR is reset, so
any changes you made will persist even after turning off
the hardware.

The stored configuration include the extension mode,
drive configuration and the current device address.
If you have changed the device address by software,
NODISKEMU will power up with that address unless
you have changed the device address jumpers (if available) to
a different setting than the one active at the time the
configuration was saved. You can think of this feature as
changing the meaning of one specific setting of the jumpers
to a different address if this sounds logical enough to you.

The "hardware overrides software overrides hardware" priority
was chosen to allow accessing NODISKEMU even when it is soft-
configured for a device number that is already taken by
another device on the bus without having to remove that
device to reconfigure NODISKEMU (e.g. when using a C128D).

### X? ###
Extended version query


This commands returns the extended version string which
consists of the version, the processor type set at build time
and the suffix of the configuration file (usually corresponds
to the short name of the hardware NODISKEMU was compiled for).


Pre-built binaries
------------------

The development sources are compiled for all supported hardware
variants once a day if there are any changes. These binaries are
available from http://petsd.net/nightlies.php?lang=en including instructions
for updating.


Compilation notes
-----------------
NODISKEMU requires avr-libc version 1.8.x.

NODISKEMU is set up to be compiled in multiple configurations, controlled by
configuration files. By default the Makefile looks for a file named
'config', but you can override it by providing the name on the make
command line with "make CONFIG=filename[,filename...]", e.g.:

        make CONFIG=configs/config-petSD+

The AVRDUDE settings use the Atmel ISP mkII programmer as default.
If you're using another programmer, you'd have to change the settings
in `scripts/avr/variables.mk`, section *Programming Options (avrdude)*.

To initially set or fix your fuses (required only once), run the Makefile's
fuses target, e.g.:

        make CONFIG=configs/config-petSD+ fuses

To program your firmware, run the Makefile's program target, e.g.:

        make CONFIG=configs/config-petSD+ program

An example configuration file named "config-example" is provided with
the source code, as well as abridged files corresponding to the
release binaries. If you want to compile NODISKEMU for a custom hardware
you may have to edit arch-config.h too to change the port definitions.


Copyright
---------

Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

Copyright (C) 2015-2017 Nils Eilers <nils.eilers@gmx.de>

NODISKEMU is a fork of sd2iec by Ingo Korb (et al.), http://sd2iec.de

Parts based on code from others, see comments in main.c for details.

- JiffyDos send based on code by M.Kiesel
- Fat LFN support and lots of other ideas+code by Jim Brain
- Final Cartridge III fastloader support by Thomas Giesel
- IEEE488 support by Nils Eilers

Free software under GPL version 2 ONLY, see comments in main.c and
COPYING for details.


Deprecation notices
-------------------
The following feature(s) will or may be removed in the future:

- large buffers
- partition support

#### M2I support ####
M2I support has been redundant since the introduction of transparent
P00 support. To continue to use your M2I-format software, convert
your files to P00 format (e.g. with m2itopc64.c) and set your device
to extension mode 2 (XE2). I'm not aware of any M2I files to be used
with a PET so this shouldn't hurt anybody.

#### Large buffers ####
Seriously, would you like to destroy your FAT filesystem? Accessing blocks
outside a disk image is not only for the brave, it shouldn't be possible.

#### Partition support ####
Commodore's hard drives didn't support them, Windows is unable to cope
with multiple partitions on a single SD card and they're conflicting with
support for multiple drives per disk unit.

Further Reading
---------------

- https://en.wikipedia.org/wiki/Commodore_DOS
- http://lmgtfy.com/?q=commodore+dos+floppy+manual+8050
