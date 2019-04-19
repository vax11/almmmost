/* almmmost.h: the main() module and misc functions for Almmmost.
 *
 * Almmmost is a modern replacement for the TeleVideo MmmOST network 
 * operating system used on the TeleVideo TS-8xx Zilog Z80-based computers 
 * from the early 1980s.
 *
 * This software uses the tvi_sdlc kernel module to interface with a
 * Zilog Z85C30 chip for the hardware interface to the TeleVideo Z-80
 * computers over an 800K baud RS-422 SDLC interface.
 *
 * Copyright (C) 2019 Patrick Finnegan <pat@vax11.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _ALMMMOST_H
#define _ALMMMOST_H

#define TVSP_SOR1 1
#define TVSP_SOR0 0
#define TVSP_BOOT 'L'
#define TVSP_CHECK 'C'
#define TVSP_CHECK_SPOOLDRV 'P'
#define TVSP_CHECK_AUTOLDPROC 'S'
#define TVSP_CHECK_GENREV 'R'
#define TVSP_CHECK_HIJACK 'H'
#define TVSP_FILEOP 'F'

#define MMM_SPOOL_DRIVE (13)
#define MMM_AUTOLOG (0)
#define MMM_PROCID (3)
#define MMM_GENREV (1)
#define TVSP_BRKSPOOL 'N'
#define TVSP_READSECT 'R'
#define TVSP_WRITESECT 'W'

#define TVSP_WRTYPE_ASYNC (0)
#define TVSP_WRTYPE_SYNC (1)
#define TVSP_WRTYPE_DESTROYBLOCK (2)

#define TVSP_REQ_SZ (10)
#define TVSP_RESP_SZ (4)
#define TVSP_FCB_SZ (36)
#define TVSP_DATA_SZ (128)

struct tvsp_spool_request {
	uint8_t sor;
	uint8_t req;
	uint8_t x[2];
	uint8_t sizel;
	uint8_t sizeh;
	uint8_t y[2];
	uint8_t iobyte;
	uint8_t z;
};

struct tvsp_check_request {
	uint8_t sor;
	uint8_t req;
	uint8_t drv;
	uint8_t subreq;
	uint8_t y[6];
};

struct tvsp_file_request {
	uint8_t sor;
	uint8_t req;
	uint8_t logdrv;
	uint8_t bdosfunc;
	uint8_t usrcode;
	uint8_t filenum[2];
	uint8_t curbdisk;
	uint8_t curbfunc;
	uint8_t x;

};

struct tvsp_file_response {
	uint8_t fileno[2];
	uint8_t retcode;
	uint8_t err;
};

struct tvsp_disk_request {
	uint8_t sor;
	uint8_t req;
	uint8_t ndisk;
	uint8_t trk8;
	uint8_t sectl;
	uint8_t secth;
	uint8_t trk16l;
	uint8_t trk16h;
	uint8_t wrtype;
	uint8_t selflg;

};

struct tvsp_ipc_response {
	uint8_t retcode;
	uint8_t x;
	uint8_t errcode;
	uint8_t err;
};

struct cpm_fcb_t {
	uint8_t drv;	/* Drive code - 0=default, 1-16 = drive A-P */
	uint8_t fname[8];
	uint8_t fext[3];	/* fext[0] & 0x80 = read/only, fexit[1] & 0x80 = system */
	uint8_t curext;	/* Extent number - 0-31 - (locn % 524288) / 16384 */
	uint8_t s1;	/* Reserved for system */
	uint8_t s2;	/* Extent high - zero by OPEN, MAKE, SEARCH - locn / 524288  */
	uint8_t reccnt;	/* Record count (w/in extent) - 0-127 */
	uint8_t al[16];	/* Blocks allocated to file */
	uint8_t currec;	/* Current record - (locn % 16384) / 128 */
	uint8_t rrec[3];/* Rand access rec num */
};

struct cpm_fcb_rename_t {
	uint8_t drv;	/* Drive code - 0=default, 1-16 = drive A-P */
	uint8_t sfname[8];
	uint8_t sfext[3];	/* fext[0] & 0x80 = read/only, fexit[1] & 0x80 = system */
	uint8_t padding[4];
	uint8_t ddrv;
	uint8_t dfname[8];
	uint8_t dfext[3];
	uint8_t padding2[8];
};
	

struct file_status {

	int in_use;
	int drivenum;
	int usernum;
	uint8_t fname[8];
	uint8_t fext[3];
	int extent;
	int records;
	int currecord;
	int randrecord;
};

struct cpm_direntry_t {

	uint8_t user;
	uint8_t fname[8];
	uint8_t fext[3];	/* fext[0] & 0x80 = read/only, fexit[1] & 0x80 = system */
	uint8_t ext_l;		/* Directory entry extent # - Low byte 0-31 */
	uint8_t s1; 		/* "reserved" */
	uint8_t ext_h;		/* Directory entry extent # - High byte */
				/* Extent # = (32*ext_h + ext_l) / (exm+1)) -- exm = mask from drive param */

	uint8_t reccnt;		/* Record count - low .. total records = ( ext_l & exm) * 128 + reccnd */
	uint8_t blknums[16];	/* Block numbers - if >256 blocks use 16b numbers, low byte first */

};

#define DIRENTRYSIZE (32)

#define MAXDISK (6)
#define MAXDIRS (32)
#define MAXUSER (16)
#define MAXBLKS (65536)
#define MAXFILES (65024)
#define RECSIZE (128)
#define CPMMAXSIZE (MAXBLKS * RECSIZE)

#define ERR_BIOS_SELECT (0)
#define ERR_BIOS_READ (1)
#define ERR_BIOS_WRITE (2)

#define BUFFER_SIZE (128)
#define NUMFILES (1024)
#define WRITEDELAY (1000)
#define INPBUFSIZE (1024)

struct user_port_data_t {
	unsigned int drive_dir[MAXDISK];
	int autologon;
	int defdrive;
};

extern struct user_port_data_t userinfo[];

extern int alm_do_abort; // Set to 1 if we should abort waiting on the client, and reset to 0 after abort
extern int alm_do_locate; // Set to 1 to locate what do/while loop we are spinning in
extern int mmm_genrev;
extern int mmm_spooldrv;
extern int mmm_numdisks;// Number of remote disks
extern int mmm_maxdirs;	// Maximum numer of private directories

/* Print a string of bytes in hex */
void print_hex(unsigned char *buffer, int count);

/* Print a string, escaping special characters */
void print_safe_ascii(unsigned char *buffer, int count);

/* Print a filename from an FCB, directory entry, etc in 8.3 format */
void print_cpm_filename(uint8_t *fname, uint8_t *ext);

/* Main function to handle sections in INI file */
void parse_args(struct INI *ini);

/* Parse General INI section */
void alm_gen_ini(struct INI *ini, const char *buf, size_t sectlen);

/* Parse Port INI section */
void alm_port_ini(struct INI *ini, const char *buf, size_t sectlen);

/* Copy a string for a max len, and ensure it's null terminated */
void string_copy(char *dest, const char *src, size_t len);

/* Convert endianness between Z80 and server as necessary */
uint16_t get_zint16(uint8_t *src);
void set_zint16(uint8_t *dest, uint16_t value);

int get_direntry_fname(char *dest, void *direntry);

/* Print out a directory entry */
int print_direntry(void *direntry, int disk);

// Functions to print things which are safe to use from a signal handler
int safe_print(char *buffer);

int safe_print_num(int i);

int safe_print_hex(int i);

int safe_get_buf(char *buffer, int len);

/* Convert file name from FCB/directory entry to 8.3 string */
int get_pretty_filename(char *dest, uint8_t *fname, uint8_t *fext);

#endif /* _ALMMMOST_H */
