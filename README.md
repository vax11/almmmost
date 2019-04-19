# Almmmost

Almmmost is a reimplementation of TeleVideo, Inc's MmmOST network operating
system, which provided a small business network to a set of Z80 or 8088-based
TeleVideo computers in the early 1980s.

Almmmost reimplements the core functions of file and disk sharing, along with
"special file" handling, which allows programs to interact with the server to
request dynamic data, or data from other sources.

The hardware part of Almmmost depends on a Next Thing Co. CHIP single board
computer, and a custom GPIO interface to a Zilog Z85C30 serial chip. This
provides an RS-422 SDLC interface running at 800K baud, for the client systems
to talk to.  A custom Linux kernel driver is provided to interface with the 
hardware, and allow easy porting to other similar single-board computers.

The kernel driver is in the "tvi\_sdlc" directory, and depends on kernel
sources to be built. If building on a NTC Chip, I recommend getting a copy
of the 4.4.138-chip kernel from here: 
https://github.com/kaplan2539/CHIP-Debian-Kernel

The needed dtb to enable the PWM, which provides the serial Tx clock, is
provided as well, and should be copied to /boot/sun5i-r8-chip.dtb

libini is required, and can be acquired from here:
https://github.com/pcercuei/libini

To compile libini, run "cmake ." in the source directory, and then the usual
make / make install.  On Debian, you'll probably first need to edit 
CMakeLists.txt to change the required cmake version to 3.0.0. I had no issues
with changing that requirement in my testing.

To build Almmmost, once you have the other things built, just cd into the 
directory and run "make".  There are a set of Z80 programs that can be built
with "z80asm" from the like named Debian/Ubuntu package.  To copy files onto
the client, you can point the "filein.sys" link to the file on the server,
and on the client just do:
```
pip a:whatever.com=b:filein.sys
```

Almmmost requires OS images for the client systems. The stock config file 
uses MmmOST 2.1 versions of the client OSes.  These are available in the
TS806 install images "tv806.zip" on Dave Dunfield's site, which will need
to be extracted from the Imagedisk images. You can use Imagedisk to convert
to a raw image, and then 22DISK, UNIFORM, or simlilar to copy the files
out. The OS images are named like "USERCPM.DAT":
http://www.classiccmp.org/dunfield/img/index.htm

To generate the binary versions of the bootloaders, you can convert the HEX
version to a binary version, doing something like this, on linux:
```
objcopy -i ihex -o binary XPDUBOOT.HEX XPDUBOOT.BIN
```

You'll next need to generate disk images. CP/M expects empty disks to
contain all e5 (hex) bytes, so I've included a program, "make\_e5.c", which
generates a stream of e5 bytes. For an 8MB image, do:
```
./make\_e5 | dd of=drive\_a.img bs=1M count=8
```

For floppy disk images, you can use Imagedisk images converted to raw images
on DOS:
```
IMDU /BIN BLAH.IMD BLAH.BIN
```

Running almmmost is as simple as:
```
./almmmost almmmost.ini
```

## Configuration file
almmmost.ini is an ini-format config file to set most of the parameters for
Almmmost.  A lot of it won't need to be touched to make this work, but here's
the main things you may want to modify:

```
[Device]
```
Set Ports= to the number of ports you have, and for each one (numbered 0 and up),
have a User Dev n = line pointing to the device name, and a User Port n = line
with the number of the ports on the card.  Each Z85C30 card has two ports, and
are numbered based on the board number * 2. So a board with the dip switches
set for the first select, would be ports 0 (A) and 1 (B). The second select is
ports 2 (A) and 3 (B).

```
[General]
```
This sets the MmmOST general revisison number and the disk number (0=A) for the
print spool.  Print spooling is not yet implemented.

```
[Port n]
```
This sets whether or not the client on this port number (these are based on the
numbers you set in the device section, not the physical numbers) runs an 
autologon command, and which private directory number it should use for its 
private drive.

```
[Clients]
```
This sets where the OS images and bootloaders are kept on the disk, and the
max number for the hardware type

```
[Client OSTYPE n]
```
This sets the filenames for the boot loader and OS image for each Client's 
hardware type.  This is the hardware type sent by the Boot rom to the server, 
and generally is:

```
0 = U = TS-800A, TS-801, TS-802
1 = TS-802H
2 = TS-1602
3 = TS-1603
4 = TS-803
5 = TS-803H
6 = TS-800R
7 = TPC-I
```

Values are listed for the base address that byte 0 of the OS image is loaded at,
the memory address for the Hardware Parameter Table (HPAM), and the address
of the Console Buffer (CONBUF). These are somewhat documented in almmmost\_osload.h

```
[Disks] 
```
This lists the number of drives that are presented to the client, and the
directory that they're stored in, along with the maximum private directory number

```
[Disk n]
```
This gives the disk type, image file names/rw status, and parameters for CP/M:
```
Type : either PRIVATE, PUBLIC, or PUBLIC\_ONLY, matching MmmOST's definitions.
Image n : one Image is used per private directory on a disk. 0 is used for
	public/public only disks
Floppy : Y or N depending on if it's emulating a floppy disk that can be changed
SPT : CP/M Sectors per track
BSF : CP/M Block shift factor
DBM : CP/M Max data block number
DBL : Maximum directory entry number
ALx : The number of blocks reserved for the directory entries. This matches the
	number of 1 bits in AL0 / AL1
RES : The number of reserved tracks (for OS or disk partitioning)
EXM : CP/M Extent Mask
```

Most of those values are based on the CP/M Disk Paramaeter Block information to
describe a disk image. The example config gives A: as a private drive of 8MB 
(the max in CP/M 2.2), B: as a public (shared file) drive of 8MB, and C: matching
the TeleVideo 360K CP/M floppy format.

## Command interface

Pressing ^C while running will halt the server and bring up a command line,
with commands available for debugging problems, or making some changes to the
system state.  The commands are listed below:

```
abort
```

Abort the current command, useful if Almmmost is waiting for the client to 
send data, but the client is hung up for some reason.

```
locate
```
Prints out the current do/while loop that the server is in, waiting for the
client to do something.

```
reopen <Disk>[:DIR] <filename>
```

eg, reopen C /root/floppy2.img ... or reopen A:0 driveA-dir0.img
Closes the current image file, and opens a new one in its place.  You will 
want to hit ^C on the client after doing this, or it might corrupt the new 
disk.

If filename starts with a "/", it assumes an absolute path, otherwise it 
looks starting in the directory specified in the config file.

```
filein <filename>
```
Changes what file on the server is used to supply data for the "filein.sys"
special file

```
fileout <filename>
```
Changes what file on the server is written to by wiriting to the "fileout.sys"
special file.

```
closeport <n>
```
Closes all open files on port #n.  Useful if the client died, and left files
open. Ensures that data is written back to disk.

```
printfil
```
Print all open files on shared drives

```
printspe
```
Print names of all special files that are handled by Almmmost

```
printdpb
```
Print the CP/M Drive Parameter Blocks that were generated by Almmmost for the
disk images.

```
printhpb
```
Print the Hardware Parameter Blocks that were generated by Almmmost based on
the config file values, for each hardware type

```
saveos <filename>
```
Saves the modified OS image, with the generated HPB/DPH/DPBs that were inserted
into it

```
sync
```
Save all open files on shared drives to the image in their current state 
(makes sure directory entries are saved)

```
exit
quit
```
Runs "sync", and then exits Almmmost.
