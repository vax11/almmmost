/* almmmost_image.h: The disk image interface module for Almmmost.
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

#ifndef _ALMMMOST_IMAGE_H
#define _ALMMMOST_IMAGE_H

#define DISK_PARAM_HDR_BYTES (16)

/* CPM_DISK_PARAM_HDR
 * XLT_VECT	(  16b)
 * SCRATCH	(3x16b)
 * DIRBUF_ADDR	(  16b)
 * DPB_ADDR	(  16b)
 * CSV_ADDR	(  16b)
 * ALV_ADDR	(  16b)
 *
 * From USERCPMx on TS-806 example: (All in hex) Delta from start of host param section
 *
 * 		U(0)	U(1)	U(2)	1(0)	4(0)	5(0)	6(0)	7(0)	7(2)
 * Off in img	2AEA	2AFA	2B0A	3019	3415	36C7	2CCB	33FE	
 * Off in mem	F66A	F67A	F68A	F799	F795	FA47	F84B	F77E	f79e
 * XLT_VECT	0000	0000	0000	0000	0000	0000	0000	0000	=
 * SCRATCH	-
 * DIRBUF_ADDR	F6C7	=	=	f7f6	f7f2	faa4	f8a8	f7db	=
 * DPB_ADDR	f69a	f6a9	f6b8	f7c9	f7c5	fa77	f87b	f7ae	f7cc
 * CSV_ADDR	f747	f82c	f912	f876	f872	fb24	f928	f85b	fa26
 * ALV_ADDR	|	|	f922	|	|	|	|	f85b	fa36
 *
 *		deltas
 * Offset	008	018	028	008	008	008	008	008	028
 * DIRBUF_ADDR	065	=	=	065	065	065	065	65	65
 * DPB		038	047	056	038	038	038	038	38	56
 * CSV		E5	1CA	2B0	E5	E5	E5	E5 	E5	2B0
 * ALV		E5	1CA	2C0	|	|	|	|	|	2C0
 *
 *  Offset = 8 + (drive #)*10h
 *  DPB = 8 + drives*10h + (drive #)*Fh
 *  Dirbuf = 8 + drives*(1Fh), size=80h
 *  CSV base drive 0 = Dirbuf+dirbuf size
 *  CSV size = CKS
 *  CSV base drive n+1 = ALV base+size (drive n)
 *  ALV base(drive) = CSV base+size(drive)
 *  ALV size(drive) = ceil(datablocks/8)
 */


#define CSV_BYTES(DBL) ((DBL+4)/4)
#define ALV_BYTES(DBM) ((DBM+8)/8)
#define DIRBUF_BYTES (128)

#define DISK_PARAM_BLK_BYTES (15)

/* 
 * SPT (16b) - Sectors/track
 * BSF ( 8b) - Block Shift Factor - 2^BSF* Sector size = Block size
 * BLM ( 8b) - Block Mask - 2^BSF-1
 * EXM ( 8b) - Extent Mask - if DBM<256, (blksz/1024)-1 else >=256, (blksz/2048)-1 -- used for dir entries
 * DBM (16b) - Max data block #
 * DBL (16b) - Max director entry #
 * AL0 ( 8b) - \
 * AL1 ( 8b) - = bitfield marking directory blocks, AL0 bit 7 = block 0, AL1 bit 0 = block 15
 * CKS (16b) - Directory check vector size, 0=hdd, >0=fdd= ( ceil((DBL+1)/4) )
 * RES (16b) - Number or reserved tracks (boot/partitioning)
 *
 * From USERCPMx on TS-806 example: (All in hex)
 *
 * 		U(0)	U(1)	U(2)
 * Off in img	2B1A	2B29	2B38
 * Off in mem	F69A	F6A9	F6B8
 * SPT		0040	0040	0048
 * BSF		05	05	04
 * BLM		1f	1f	0f
 * EXM		01	01	00
 * DBM		0727	072b	00aa
 * DBL		007f	01ff	003f
 * AL0		ff	ff	80
 * AL1		ff	00	00
 * CKS		0000	0000	0010
 * RES		0002	0396	0002
 */

struct drive_param_t {
	unsigned int blk_size;		// Block size in bytes
	unsigned int dirs;		// Number of private directories
	unsigned int dir_ALx;		// Number of blocks reserved for directories
	unsigned int public_private;	// enum for private/public/pub. only
		 int image_fd[MAXDIRS];	// fds for image file (one per directory)
		 int is_ro[MAXDIRS];	// set to 1 if this dir's image is r/o
	unsigned int is_floppy;		// true if is a floppy (removable)
	unsigned int dir_rec_min;	// Minimum value for directory record
	unsigned int dir_rec_max;	// Max value for usable directory record
	unsigned int data_rec_min;	// Min value for data record
	unsigned int data_rec_max;	// Max value for data record
	unsigned int tracks;		// Number of tracks
	// Extent size -> (EXM+1) * 128 records
	// Values for CP/M we need later
	uint16_t SPT;			// Sectors per track
	uint16_t BSF;			// Block shift factor
	uint16_t BLM;			// Block Mask
	uint16_t EXM;			// Extent Mask
	uint16_t DBM;			// Max block # of data blocks
	uint16_t DBL;		 	// Max directory block #
	uint16_t res_tracks;		// Number of reserved tracks
	int *bam;			// Block allocation map for public drives only
};

#define PRIVDIR (0)
#define PUBLDIR (1)
#define PUBLONLYDIR (2)

extern struct drive_param_t drvparam[];
extern int mmm_pubdrv;	// Public drives bitfield
extern char *disk_image_dir;

/* Initialize variables */
int alm_img_init();

/* Free allocated memory/clear variables */
int alm_img_exit();

/* Process config file */
int alm_img_ini(struct INI *ini, const char *buf, size_t sectlen);

/* Read a record from disk, taking priv. directories into account */
int alm_img_readrec(int disk, int user, int rec, void *buf);

/* Write a record to a disk, taking priv. directories into account */
int alm_img_writerec(int disk, int user, int rec, void *buf);

/* Generate the drive parameter headers/blocks based on config file input */
int alm_generate_drv_param_hdrs(int ostype);

/* Display drive parameter table for debugging */
int alm_drv_disp_param_hdrs(int disk);

/* Calculate what value EXM has */
uint16_t find_exm(uint16_t datablocks, uint16_t blocksize);

/* Handle read request */
int alm_do_read(int portnum, void *reqbuf);

/* Handle write request */
int alm_do_write(int portnum, void *reqbuf);

/* Re-open drive with a new file */
int alm_img_reopen(int disk, int dir, char *filename);

#endif /* _ALMMMOST_IMAGE_H */
