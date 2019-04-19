/* almmmost_file.c: The shared file I/O interface module for Almmmost.
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

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#include <ini.h>

#include "almmmost.h"
#include "almmmost_image.h"
#include "almmmost_file.h"
#include "almmmost_special.h"
#include "almmmost_device.h"


struct file_status_t *fileinfo;

int alm_file_init() {

	fileinfo = calloc(MAXFILES, sizeof(struct file_status_t));
	if (!fileinfo)
		return -1;
	special_files = NULL;
	return 0;
}

int alm_file_exit() {

	if (fileinfo) {
		free(fileinfo);
		fileinfo = NULL;
	}
	// FIXME: free special_files LL
	return 0;
}

int alm_file_ini(struct INI *ini, const char *buf, size_t buflen) {
	return 0;
}

/* Received 10 byte req in
 * Get 36 byte FCB
 * Maybe get 128 byte DATA (if write)
 * Send 4 byte response
 * Send 36 byte FCB
 * Maybe send 128 byte DATA (if read)
 */
int alm_do_fileop(int portnum, void *reqbuf) {

	int retval;
	struct tvsp_file_request *freq = reqbuf;
	struct cpm_fcb_t fcbin, fcbout;
	struct tvsp_file_response fresp;
	uint8_t databuf[TVSP_DATA_SZ];
	int drivenum;
	int fop = freq->bdosfunc;
	int rpos = 0, spos = 0;

	memset(databuf, 0, TVSP_DATA_SZ);
	memset(&fcbin, 0, TVSP_FCB_SZ);
	memset(&fcbout, 0, TVSP_FCB_SZ);

	//printf("(FILEOP)waiting for CTS...\n");
	do {
		retval = alm_dev_check_cts(portnum);
		if (alm_do_locate || alm_do_abort) {
			printf("locate/abort: alm_do_fileop(%d,buf):\n", portnum);
			printf("  Get FCB, operation %d, drive %c, file %d\n", 
					fop, freq->curbdisk + 'A', get_zint16(freq->filenum));
			alm_do_locate = 0;
		}
		if (alm_do_abort) {
			fresp.retcode = 0xFF;
			fresp.err = ERR_BIOS_WRITE;
			alm_do_abort = 0;
			goto dofileop_exit;
		}

	} while (!retval);
	retval = alm_dev_read(&fcbin, TVSP_FCB_SZ, portnum);
	if (retval != TVSP_FCB_SZ) {
		printf("Read FCB error: %d (%d)\n", retval, errno);
	}

	if (!fcbin.drv) {
		drivenum = freq->curbdisk;
	} else {
		drivenum = fcbin.drv - 1;
	}

	//printf("TVSP_FILEOP request %d on user %d, drive %c, file number %d name ", fop, freq->usrcode, drivenum + 'A', get_zint16(freq->filenum));
	//print_cpm_filename(fcbin.fname, fcbin.fext);
	//putchar('\n');
	//printf("FCB: ");
	//print_hex((void *)&fcbin, TVSP_FCB_SZ);

	rpos = ((fcbin.rrec[2] << 16) + (fcbin.rrec[1] << 8) + fcbin.rrec[0]) * RECSIZE;
	spos = FULL_EXT(fcbin.s2, fcbin.curext) * 128 * RECSIZE + fcbin.currec * RECSIZE;

	/* Initial output fcb should be the same as the input one */
	memcpy(&fcbout, &fcbin, sizeof(fcbin));

	/* If we accept data from the client, do that now */
	if ((fop == TVSP_FILE_WRITESEQ) || (fop == TVSP_FILE_WRITERAND) || (fop == TVSP_FILE_WRITERANDZ)) {
		//printf("waiting for CTS...\n");
		do {
			retval = alm_dev_check_cts(portnum);
			if (alm_do_locate || alm_do_abort) {
				printf("locate/abort: alm_do_fileop(%d,buf):\n", portnum);
				printf("  Get Write data, operation %d, drive %c, file %d: ", 
						fop, drivenum + 'A', get_zint16(freq->filenum));
				print_cpm_filename(fcbin.fname, fcbin.fext);
				putchar('\n');
				printf("FCB: ");
				print_hex((void *)&fcbin, TVSP_FCB_SZ);
				alm_do_locate = 0;
			}
			if (alm_do_abort) {
				fresp.retcode = 0xFF;
				fresp.err = ERR_BIOS_WRITE;
				alm_do_abort = 0;
				goto dofileop_exit;
			}
		} while (!retval);
		retval = alm_dev_read(databuf, TVSP_DATA_SZ, portnum);
		if (retval != TVSP_DATA_SZ) {
			printf("Read data error: %d (%d)\n", retval, errno);
		}
	}

	switch (fop) {

		case TVSP_FILE_OPEN:
		case TVSP_FILE_MAKE:
			alm_file_doopen(portnum, freq, &fcbout, &fresp);
			printf("Open/Make %02x / err %02x, file no %d\n", fresp.retcode, fresp.err, get_zint16(fresp.fileno));
			break;

		case TVSP_FILE_CLOSE:
			alm_file_doclose(portnum, freq, &fcbout, &fresp);
			break;
			
		case TVSP_FILE_WRITESEQ:
			//printf("Write sequential at %d\n", spos);
			alm_file_dowrite(portnum, freq, &fcbout, &fresp, databuf);
			break;

		case TVSP_FILE_WRITERAND:
		case TVSP_FILE_WRITERANDZ:
			//printf("Write random at %d\n", rpos);
			alm_file_dowrite(portnum, freq, &fcbout, &fresp, databuf);
			break;

		case TVSP_FILE_READSEQ:
			//printf("Read sequential at %d\n", spos);
			alm_file_doread(portnum, freq, &fcbout, &fresp, databuf);
			break;
		case TVSP_FILE_READRAND:
			//printf("Read random at %d\n", rpos);
			alm_file_doread(portnum, freq, &fcbout, &fresp, databuf);
			break;

		case TVSP_FILE_SETRANDREC:
			printf("Set random record bytes in FCB\n");
			alm_file_dosetrandpos(portnum, freq, &fcbout, &fresp);
			break;

		case TVSP_FILE_DELETE:
			printf("Delete\n");
			alm_file_domoddir(portnum, freq, &fcbout, &fresp);
			break;

		case TVSP_FILE_RENAME:
			printf("Rename\n");
			alm_file_domoddir(portnum, freq, &fcbout, &fresp);
			break;

		case TVSP_FILE_SEARCH1ST:
			printf("Search for first\n");
			alm_file_dosearch(portnum, freq, &fcbout, &fresp);
			break;

		case TVSP_FILE_SETATTR:
			printf("Set attributes\n");
			alm_file_domoddir(portnum, freq, &fcbout, &fresp);
			break;

		case TVSP_FILE_GETSIZE:
			printf("Get file size\n");
			alm_file_dogetsize(portnum, freq, &fcbout, &fresp);
			break;

		default:
			printf("Unknown function %d.\n", fop);
			set_zint16(fresp.fileno, 0xFFFF);
			fresp.retcode = 0xFF;	// Error
			fresp.err = 1;		// Command fault
	
			break;
	}
	
dofileop_exit:
	usleep(100);
	alm_dev_write(&fresp, TVSP_RESP_SZ, portnum);

	usleep(100);
	alm_dev_write(&fcbout, TVSP_FCB_SZ, portnum);


	if (((fop == TVSP_FILE_READSEQ) || (fop == TVSP_FILE_READRAND)) && (fresp.retcode == 0)) {
		// Don't send data if error
		usleep(100);
		retval = alm_dev_write(databuf, TVSP_DATA_SZ, portnum);
	}
	return 0;
}

/* Open / Make, BDOS 15, 22 */
int alm_file_doopen(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {
	int disk, uc, fd, retval, fnum;
	// Handle multiple writers elsewhere some other time
	
	disk = (fcb->drv) ? (fcb->drv - 1) : freq->curbdisk;
	uc = freq->usrcode;
	if (disk >= mmm_numdisks) {
		resp->err = MMMERR_SELECT;
		goto open_error;
	}
	fd = drvparam[disk].image_fd[0];
	if (fd < 0) {
		resp->err = MMMERR_SELECT;
		goto open_error;
	}
	if (drvparam[disk].public_private != PUBLDIR) {
		resp->err = MMMERR_DRVTYPE;
		goto open_error;
	}

	// Find file on disk
	fnum = alm_file_finddentry(portnum, disk, uc, fcb);
	if (fnum < 0) {
		resp->err = MMMERR_CMDFAULT;
		goto open_error;
	}

	// Error if file exists, unless it's special, then allow it
	if (fileinfo[fnum].extent && freq->bdosfunc == TVSP_FILE_MAKE) {
		resp->err = MMMERR_OK;
		goto open_error;
	}

	// Error on OPEN if file doesn't exist and isn't specal
	if (!(fileinfo[fnum].extent || fileinfo[fnum].trap) && freq->bdosfunc == TVSP_FILE_OPEN) {
		resp->err = MMMERR_OK;
		goto open_error;
	}

	if (freq->bdosfunc == TVSP_FILE_MAKE && !fileinfo[fnum].trap) {
		memcpy(fileinfo[fnum].fname, fcb->fname, 8);
		memcpy(fileinfo[fnum].fext, fcb->fext, 3);
		retval = alm_alloc_dentry(disk, uc, fnum, fcb);
		if (retval < -2) {
			resp->err = MMMERR_NOSPACE;	// Out of space
			alm_file_closeentry(disk, fnum);
			goto open_error;
		} else if (retval < 0) {
			resp->err = MMMERR_CMDFAULT;	// Command fault
			alm_file_closeentry(disk, fnum);
			goto open_error;
		}
	}

	alm_file_blks2fcb(disk, fileinfo[fnum].extent, fcb);
	set_zint16(resp->fileno, fnum);
	if (fileinfo[fnum].extent)
		resp->retcode = (fileinfo[fnum].extent->denum & 0x3); // Directory code = place in dir record
	else 
		resp->retcode = 0;
	resp->err = MMMERR_OK;	// No error
	
	return 0;


open_error:
	set_zint16(resp->fileno, 0xFFFF);
	resp->retcode = RETCODE_MISCERR;
	alm_file_closeentry(disk, fnum);
	return -5;
}

/* Set Random Record, BDOS 36 */
int alm_file_dosetrandpos(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {

	int fnum, disk, maxsz, fcbpos, pos, uc;

	disk = (fcb->drv) ? (fcb->drv - 1 ) : freq->curbdisk;
	uc = freq->usrcode;
	// Verify file number
	fnum = get_zint16(freq->filenum);
	fnum = alm_file_getfnum(fnum, fcb, portnum, disk, uc);
	set_zint16(resp->fileno, fnum);
	if (fnum < 0) {
		resp->err = MMMERR_BADFILE;
		resp->retcode = RETCODE_MISCERR;
		goto srp_error;
	}

	maxsz = fileinfo[fnum].size; //FIXME these compute the location incorrectly (??)
	if (fcb->currec != 0x80)
		fcbpos = ((int)fcb->s2 * 32 + (fcb->curext & 0x1F)) * 128 + fcb->currec;
	else
		fcbpos = ((int)fcb->s2 * 32 + (fcb->curext & 0x1F) + drvparam[disk].EXM + 1) * 128;

	pos = (maxsz < fcbpos) ? maxsz : fcbpos;

	set_zint16(fcb->rrec, pos & 0xFFFF);
	fcb->rrec[3] = pos >> 16;

	resp->err = MMMERR_OK;
	resp->retcode = RETCODE_OK;

	return 0;

srp_error:
	set_zint16(resp->fileno, 0xFFFF);
	return -1;
}

/* Close, BDOS 16 */
int alm_file_doclose(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {
	int disk, uc, fnum;
	
	disk = (fcb->drv) ? (fcb->drv - 1) : freq->curbdisk;
	uc = freq->usrcode;
	fnum = get_zint16(freq->filenum);
	
	// Verify file number
	fnum = alm_file_getfnum(fnum, fcb, portnum, disk, uc);
	if (fnum < 0) {
		resp->err = MMMERR_BADFILE;
		resp->retcode = RETCODE_MISCERR;
		goto close_error;
	}
	//alm_file_rewrite_extents(fnum);
	
	alm_file_closeentry(disk, fnum);

	set_zint16(resp->fileno, 0xFFFF);
	resp->retcode = 0;
	resp->err = 0;
	
	return 0;

close_error:
	set_zint16(resp->fileno, 0xFFFF);
	alm_file_closeentry(disk, fnum);
	return -5;
}

/* Can be called from signal handler */
/* Close any open file from portnum */
int alm_file_clearfiles(int portnum) {
	int fnum;

	for (fnum = 0; fnum < MAXFILES; fnum++) {
		if (fileinfo[fnum].used && fileinfo[fnum].port == portnum) {
			alm_file_closeentry(fileinfo[fnum].drivenum, fnum);
		}
	}
	return 0;
}

/* Read seq / Read rand, BDOS 20, 33 */
int alm_file_doread(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp, uint8_t *readbuf) {
	int disk, uc, fnum, retval, pos, ext, blk, i, blkoff;
	struct ext_ll_t *extent;
	
	disk = (fcb->drv) ? (fcb->drv-1) : freq->curbdisk;
	uc = freq->usrcode;
	fnum = get_zint16(freq->filenum);
	memcpy(resp->fileno, freq->filenum, 2);

	// Verify file number
	fnum = alm_file_getfnum(fnum, fcb, portnum, disk, uc);
	if (fnum < 0) {
		resp->err = MMMERR_BADFILE;
		resp->retcode = RETCODE_MISCERR;
		goto doread_error;
	}

	// Convert fcb position to location in file -- rand vs seq
	if (freq->bdosfunc == TVSP_FILE_READRAND) {
		// If random, pull from r0-r2 in FCB
		pos = (fcb->rrec[2] << 16) + (fcb->rrec[1] << 8) + fcb->rrec[0];
	} else {
		// Otherwise, use sequential location
		pos = (fcb->s2 << 12) + (fcb->curext << 7) + fcb->currec; 
	}

	if (fileinfo[fnum].trap) {
		if (!fileinfo[fnum].special_buf || (alm_special_trapfileop(fnum, freq->bdosfunc, pos) < 0)) {
			// EOF error
			resp->err = MMMERR_OK;
			resp->retcode = RETCODE_UNWRITTEN_DATA;
			printf("Read err - special file EOF at %d\n", pos);
			goto doread_error;
		}
		memcpy(readbuf, fileinfo[fnum].special_buf, RECSIZE);
		goto doread_retok;
	}

	ext = pos / ((drvparam[disk].EXM + 1) << 7);
	blk = (pos % ((drvparam[disk].EXM + 1) << 7))  >> drvparam[disk].BSF;
	
	if (blk > 16) {
		printf("Block calculation error, blk should be < 16: pos %x ext %x blk %x\n", pos, ext, blk);
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		goto doread_error;
	}

	
	// Locate correct extent number
	extent = fileinfo[fnum].extent;
	for (i=0; i<ext; i++) {
		extent = extent->next;
		if (extent == NULL)
			break;
	}
	// If at end of file, return error
	if (extent == NULL ) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_EXTENT;
		printf("Read err - NULL extent\n");
		goto doread_error;
	}

	//Set reccnt
	fcb->reccnt = extent->extsize & 0x7F;

	if (extent->blocks[blk] == 0) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		printf("Read err - block %d == 0\n", blk);
		goto doread_error;
	}
	if (pos >= fileinfo[fnum].size) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		printf("Read err - pos %d >= size %d\n", pos, fileinfo[fnum].size);
		goto doread_error;
	}

	blkoff = ((pos & drvparam[disk].BLM) + (extent->blocks[blk]<<drvparam[disk].BSF) + drvparam[disk].dir_rec_min) * RECSIZE ;
	//printf("Read from disk offset %06x\n", blkoff);
	// Read data from the block
	retval = lseek(drvparam[disk].image_fd[0], 
			 blkoff,
			SEEK_SET);
	if (retval < 0) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		printf("Read err - seek fail %d errno = %d\n", retval, errno);
		goto doread_error;
	}
	retval = read(drvparam[disk].image_fd[0], readbuf, RECSIZE);
	if (retval < RECSIZE) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		printf("Read err - short read %d errno = %d\n", retval, errno);
		goto doread_error;
	}

doread_retok:
	// If we are reading sequentually, increment position in fcb. random -> don't change
	if (freq->bdosfunc == TVSP_FILE_READSEQ) {
		pos++;
		fcb->currec = pos & 0x7F;
		fcb->curext = EXT_EXTL(pos);
		fcb->s2 = EXT_S2(pos);
		// Don't update rrec position
		//fcb->rrec[0] = pos & 0xFF;
		//fcb->rrec[1] = (pos >> 8) & 0xFF;
		//fcb->rrec[2] = pos >> 16;
	}
	alm_file_blks2fcb(disk, extent, fcb);
	resp->err = MMMERR_OK;
	resp->retcode = RETCODE_OK;
	return 0;


doread_error:
	return -1;
}

/* Write seq / Write rand / Write rand zero block, BDOS 21, 34, 40 */
int alm_file_dowrite(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp, uint8_t *writebuf) {
	int disk, uc, fnum, retval, pos, ext, blk, i, blkoff;
	struct ext_ll_t **extentptr = NULL;
	uint8_t *blkbuf;
	
	disk = (fcb->drv) ? (fcb->drv-1) : freq->curbdisk;
	uc = freq->usrcode;
	fnum = get_zint16(freq->filenum);
	memcpy(resp->fileno, freq->filenum, 2);

	// Verify filenumber
	fnum = alm_file_getfnum(fnum, fcb, portnum, disk, uc);
	if (fnum < 0) {
		resp->err = MMMERR_BADFILE;
		resp->retcode = RETCODE_MISCERR;
		goto dowrite_error;
	}

	// Verify disk and file are not R/o
	if (drvparam[disk].is_ro[0]) {
		// Disk RO
		printf("Attempt to write to R/O disk %d\n", disk);
		resp->err = MMMERR_RO;
		resp->retcode = RETCODE_MISCERR;
		goto dowrite_error;
	}
	if (fileinfo[fnum].fext[0] & 0x80) {
		// File RO
		printf("Attempt to write to R/O file\n");
		resp->err = MMMERR_RO;
		resp->retcode = RETCODE_MISCERR;
		goto dowrite_error;
	}

	// Convert fcb position to location in file -- rand vs seq
	if (freq->bdosfunc != TVSP_FILE_WRITESEQ) {
		// If random, pull from r0-r2 in FCB
		pos = (fcb->rrec[2] << 16) + (fcb->rrec[1] << 8) + fcb->rrec[0];
	} else {
		// Otherwise, use sequential location
		pos = (fcb->s2 << 12) + (fcb->curext << 7) + fcb->currec; 
	}

	ext = pos / ((drvparam[disk].EXM + 1) << 7);
	blk = (pos % ((drvparam[disk].EXM + 1) << 7))  >> drvparam[disk].BSF;
	
	if (blk > 16) {
		printf("Block calculation error, blk should be < 16: pos %x ext %x blk %x\n", pos, ext, blk);
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		goto dowrite_error;
	}

	// Handle special file
	if (fileinfo[fnum].trap) {
		if (!fileinfo[fnum].special_buf) {
			resp->err = MMMERR_OK;
			resp->retcode = RETCODE_MISCERR;
			printf("Write err - special_buf NULL\n");
			goto dowrite_error;
		}
		memcpy(fileinfo[fnum].special_buf, writebuf, RECSIZE);
		if (alm_special_trapfileop(fnum, freq->bdosfunc, pos) < 0) {
			resp->err = MMMERR_OK;
			resp->retcode = RETCODE_MISCERR;
			printf("Write err - special file write error at %d\n", pos);
			goto dowrite_error;
		}
		goto dowrite_retok;
	}

	// Locate correct extent number
	extentptr = &fileinfo[fnum].extent;
	for (i=0; i<ext; i++) {
		extentptr = &((*extentptr)->next);
		if (*extentptr == NULL)
			retval = alm_alloc_dentry(disk, uc, fnum, fcb);
		if (*extentptr == NULL) {		// If still null, we failed to allocate one
			printf("Failed to allocate directory entry: %d\n", retval);
			resp->err = MMMERR_NOSPACE;
			resp->retcode = RETCODE_DIRFULL;
			goto dowrite_error;
		}
	}

	// We handle end-of-extent allocation of new extent in the above
	
	// Allocate a new block, add it to the block list
	if ((*extentptr)->blocks[blk] == 0) {
		retval = alm_file_allocblk(disk, (*extentptr)->denum);
		if (retval < 1) {
			printf("Couldn't allocate new block: %d\n", retval);
			resp->err = MMMERR_NOSPACE;
			resp->retcode = RETCODE_DIRFULL;
			goto dowrite_error;
		} else {
			(*extentptr)->blocks[blk] = retval;
		}

	}

	blkoff = ((pos & drvparam[disk].BLM) + ((*extentptr)->blocks[blk]<<drvparam[disk].BSF) + drvparam[disk].dir_rec_min) * RECSIZE;
	// Write data to the block
	retval = lseek(drvparam[disk].image_fd[0], 
			blkoff,
			SEEK_SET);
	if (retval < 0) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		goto dowrite_error;
	}
	if (freq->bdosfunc != TVSP_FILE_WRITERANDZ || (pos & drvparam[disk].BLM)) {
		retval = write(drvparam[disk].image_fd[0], writebuf, RECSIZE);
	} else {
		// Zero block
		blkbuf = alloca(drvparam[disk].blk_size);
		memset(blkbuf+RECSIZE, 0, drvparam[disk].blk_size-RECSIZE);
		memcpy(blkbuf, writebuf, RECSIZE);
		retval = write(drvparam[disk].image_fd[0], blkbuf, drvparam[disk].blk_size);
	}
	if (retval < 0) {
		resp->err = MMMERR_OK;
		resp->retcode = RETCODE_UNWRITTEN_DATA;
		goto dowrite_error;
	}

	// If we are at the end of the extent, allocate a new extent
	if ((((pos + 1) & ((drvparam[disk].EXM<<7)+0x7F)) == 0) && (*extentptr)->next == NULL) {
		alm_alloc_dentry(disk, uc, fnum, fcb);
	}

	// If we're past what the file size says, increase that
	if ((pos + 1) > fileinfo[fnum].size)
		fileinfo[fnum].size = pos+1;

	// Same for extent size
	if (((pos + 1) % ((drvparam[disk].EXM+1)<<7)) > (*extentptr)->extsize)
		(*extentptr)->extsize++;
	
	alm_file_blks2fcb(disk, *extentptr, fcb);

dowrite_retok:
	// If we are writing sequentually, increment position in fcb. otherwise if random -> don't change
	if (freq->bdosfunc == TVSP_FILE_WRITESEQ) {
		pos++;
		fcb->currec = pos & 0x7F;
		fcb->curext = EXT_EXTL(pos);
		fcb->s2 = EXT_S2(pos);
		// Don't update rrec position
		//fcb->rrec[0] = pos & 0xFF;
		//fcb->rrec[1] = (pos >> 8) & 0xFF;
		//fcb->rrec[2] = pos >> 16;
	}
	resp->err = MMMERR_OK;
	resp->retcode = RETCODE_OK;
	return 0;

dowrite_error:
	return -1;
}


/* Search for First, BDOS 17 -- note drive = ? means search for any user #, including e5's */
int alm_file_dosearch(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {

	uint8_t retcode = 0;
	struct tvsp_file_request freq2;

	memcpy(&freq2, freq, TVSP_REQ_SZ);
	freq2.bdosfunc = TVSP_FILE_OPEN;

	// Open returns if the file exists, and the right retcode.
	alm_file_doopen(portnum, &freq2, fcb, resp);
	retcode = resp->retcode;
	memcpy(freq2.filenum, resp->fileno, 2);
	
	if (retcode <= 3) {
		// Close if we succeeded
		freq2.bdosfunc = TVSP_FILE_CLOSE;
		alm_file_doclose(portnum, &freq2, fcb, resp);
		resp->retcode = retcode;
		resp->err = MMMERR_OK;
	}
	memcpy(resp->fileno, freq->filenum, 2);
	resp->err = MMMERR_OK;
	resp->retcode = RETCODE_OK;

	return 0;
}

/* Delete file BDOS 19 / Rename file BDOS 23 / Set attributes BDOS 30 */
int alm_file_domoddir(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {
	int disk;

	disk = (fcb->drv) ? (fcb->drv-1) : freq->curbdisk;

	return alm_modify_dir(freq->bdosfunc, disk, freq->usrcode, fcb, resp);
}

/* Get file size (set rrec to EOF), BDOS 35 */
int alm_file_dogetsize(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {

	int disk, uc, fnum, size;
	struct ext_ll_t *extent;
	
	disk = (fcb->drv) ? (fcb->drv - 1) : freq->curbdisk;
	uc = freq->usrcode;
	fnum = get_zint16(freq->filenum);

	// Verify filenumber
	fnum = alm_file_getfnum(fnum, fcb, portnum, disk, uc);
	if (fnum < 0) {
		resp->err = MMMERR_BADFILE;
		resp->retcode = RETCODE_MISCERR;
		goto dogetsz_error;
	}

	// Set random record based on known size
	size = fileinfo[fnum].size;
	fcb->rrec[0] = size & 0xFF;
	fcb->rrec[1] = (size >> 8) & 0xFF;
	fcb->rrec[2] = size >> 16;

	memcpy(resp->fileno, freq->filenum, 2);

	extent = fileinfo[fnum].extent;
	while (extent->next != NULL)
		extent = extent->next;
	alm_file_blks2fcb(disk, extent, fcb);
	resp->err = MMMERR_OK;
	resp->retcode = RETCODE_OK;

	return 0;
	
dogetsz_error:
	return -1;
}

int alm_file_loadbam(int disk) {

	int i, j, fd, DBM;
	uint16_t block;
	struct cpm_direntry_t de;

	// Read directory listing off disk and re-init bam
	if (disk >= MAXDISK)
		return -1;
	// Fail on not public disk
	if (drvparam[disk].public_private != PUBLDIR) 
		return -1;

	fd = drvparam[disk].image_fd[0];
	DBM = drvparam[disk].DBM;

	memset(drvparam[disk].bam, 0, sizeof(int) * MAXBLKS);
	lseek(fd, drvparam[disk].dir_rec_min * RECSIZE, SEEK_SET);
	for (i=0; i<=drvparam[disk].DBL; i++) {
		if (read(fd, &de, DIRENTRYSIZE)<DIRENTRYSIZE)
			return -1; // Error reading -> abort
		if (de.user == 0xe5)
			continue;  // Deleted entry
		for (j=0; j<16; j++) {
			if (DBM<256) {
				block = de.blknums[j];
			} else {
				block = get_zint16(de.blknums+j);
				j++;
			}
			if (block)
				drvparam[disk].bam[block] = i;
		}
	}

	return 0;
}

int alm_file_allocblk(int disk, int dentry) {

	int i;

	if (disk >= MAXDISK)
		return -1;
	// Fail on not public disk
	if (drvparam[disk].public_private != PUBLDIR) 
		return -2;

	// Find the first free (data) block
	for (i=((drvparam[disk].DBL/4)>>drvparam[disk].BSF) + 1; i<=drvparam[disk].DBM; i++)
		if (drvparam[disk].bam[i] < 1) {
			drvparam[disk].bam[i] = dentry;
			return i;
		}


	// If we get here, we couldn't find a free block
	return -3;
}

int alm_file_deallocblk(int disk, uint16_t block) {


	if (disk >= MAXDISK)
		return -1;
	// Fail on not public disk
	if (drvparam[disk].public_private != PUBLDIR) 
		return -1;

	// Fail if block number is too big
	if (block > drvparam[disk].DBM)
		return -2;

	drvparam[disk].bam[block] = 0;
	return 0;

}

int alm_file_finddentry(int port, int disk, int usrcode, struct cpm_fcb_t *fcb) {

	int denum, fnum, extnum, blk, extsz;
	int retval, fd;
	struct file_status_t *thisfile;
	struct ext_ll_t **thisextptr;
	struct cpm_direntry_t de;

	// Find file on disk for given cp/m user code and fcb. Fill in fileinfo[] and return entry #.
	// Dont use fnum=0, because we get that if the client ran out of filenum storage space
	for (fnum=1; fnum<MAXFILES; fnum++)
		if (!fileinfo[fnum].used)
			break;
	fd = drvparam[disk].image_fd[0];
	if (fd < 0)
		return -2;		// Image not open
	if (fnum == MAXFILES)
		return -3;		// Couldn't find a free file number
	thisfile = &fileinfo[fnum];
	thisfile->used = 1;
	thisfile->extent = NULL;
	thisfile->usrcode = usrcode;
	thisfile->drivenum = disk;
	thisfile->size = 0;
	thisfile->port = port;

	// First see if it's special
	if (alm_special_trapopen(fnum, fcb)) {
		return fnum;
	}

	thisextptr = &(thisfile->extent);
	
	extnum=0;
	do { 
		retval = lseek(fd, drvparam[disk].dir_rec_min * RECSIZE, SEEK_SET);
		if (retval < 0)
			goto findentry_err;
		for (denum=0; denum<=drvparam[disk].DBL; denum++) {
			retval = read(fd, &de, DIRENTRYSIZE);
			if (retval < DIRENTRYSIZE)
				goto findentry_err;
			// Check user code, file name and extent number matches
			if ((de.user == usrcode) && (alm_same_file(fcb, de.fname, de.fext)) 
					&& (PHY_EXT(de.ext_h,de.ext_l,drvparam[disk].EXM) == extnum)) {
				// Copy filename from directory entry so we get permission bits
				memcpy(thisfile->fname, de.fname, 8);
				memcpy(thisfile->fext, de.fext, 3);
				// Set record count for first extent since we start on fpos = 0
				if (extnum==0)
					fcb->reccnt = de.reccnt;
				// Allocate space for the extent info & fill it in
				*thisextptr = calloc(sizeof(struct ext_ll_t), 1);
				(*thisextptr)->next = NULL;
				(*thisextptr)->denum = denum;
				// Copy blocks over
				for (blk=0; blk < 16; blk++) {
					if (drvparam[disk].DBM < 256) {
						// 8 bit block #s
						(*thisextptr)->blocks[blk] = de.blknums[blk];
					} else {
						// 16 bit block #s
						(*thisextptr)->blocks[blk/2] = get_zint16(de.blknums + blk);
						blk++; // Increment by 2 because 16b not 8b
					}
				}
				// Fill in size
				if (de.reccnt == 0x80) {
					// Scan for more extents if full
					extsz = (drvparam[disk].EXM+1) * 128;
					thisfile->size += extsz;
					(*thisextptr)->extsize = extsz;

					thisextptr = &((*thisextptr)->next);
					break;
				} else {
					// Last extent if not full
					extsz = LOG_EXT(de.ext_l, drvparam[disk].EXM) * 128 + de.reccnt;
					thisfile->size += extsz;
					(*thisextptr)->extsize = extsz;

					thisextptr = &((*thisextptr)->next);
					goto findentry_done;
				}
			}
		}
		extnum++;
	} while (denum <= drvparam[disk].DBL);

findentry_done:
	return fnum;  // if we didn't find the file, thisfile->extent == NULL.
findentry_err:
	return -4;
}

/* Can be called from signal handler */
int alm_file_closeentry(int disk, int fnum) {


	struct ext_ll_t *extptr, *nextextptr;

	if (fnum >= MAXFILES || !fileinfo[fnum].used || fileinfo[fnum].drivenum != disk)
		return -1;
	// Special file trap (do here not doclose, so we catch automatic closing)
	alm_special_trapclose(fnum);
	// Re-write extents so things are saved..
	alm_file_rewrite_extents(fnum);
	extptr = fileinfo[fnum].extent;
	while (extptr) {
		nextextptr = extptr->next;
		free(extptr);
		extptr = nextextptr;
	}

	memset(&fileinfo[fnum], 0, sizeof(struct file_status_t));

	return 0;
}	

int alm_file_closeallondisk(int disk) {

	int fnum;

	for (fnum=1; fnum < MAXFILES; fnum++)
		alm_file_closeentry(disk, fnum);	// This checks we are doing the right disk/file is open

	return 0;
}


int alm_alloc_dentry(int disk, int usrcode, int fnum, struct cpm_fcb_t *fcb) {

	struct ext_ll_t **extptr;
	struct cpm_direntry_t de;
	int fd, retval, denum, extnum;

	if (disk >= MAXDISK)
		return -1;		// Bad disk #
	fd = drvparam[disk].image_fd[0];
	if (fd < 0)
		return -2;		// Disk not open
	if (fnum >= MAXFILES)
		return -3;		// No more file numbers

	retval = lseek(fd, drvparam[disk].dir_rec_min * RECSIZE, SEEK_SET);
	if (retval < 0)
		return -4;		// -ENOSPACE

	// Find empty directory entry (user = 0xe5)
	for (denum=0; denum <= drvparam[disk].DBL; denum++) {
		retval = read(fd, &de, DIRENTRYSIZE);
		if (retval < DIRENTRYSIZE)
			return -4;	// -ENOSPACE
		if (de.user == 0xe5) 
			break;
	}
	if (denum > drvparam[disk].DBL)
		return -3;		// -ENOSPACE

	// Set up directory entry
	de.user = usrcode;
	memcpy(de.fname, fcb->fname, 8);
	memcpy(de.fext, fcb->fext, 3);
	extnum = 0;
	de.s1 = 0;
	de.reccnt = 0;
	memset(de.blknums, 0, 16);

	// Find tail of extent on file fnum
	extptr = &(fileinfo[fnum].extent);
	while (*extptr != NULL) {
		extptr = &((*extptr)->next);
		extnum++;
	}
	*extptr = calloc(sizeof(struct ext_ll_t), 1);
	if (!*extptr)
		return -5;		// Can't allocate space
	(*extptr)->next = NULL;
	(*extptr)->denum = denum;

	de.ext_l = (extnum * (drvparam[disk].EXM + 1)) & 0x1F;
	de.ext_h = (extnum * (drvparam[disk].EXM + 1)) >> 5;

	printf("(a)Writing extent (entry %d) %d, s2 %d, uc %d, filename: ", denum, de.ext_l, de.ext_h, de.user); print_cpm_filename(fileinfo[fnum].fname, fileinfo[fnum].fext);
	putchar('\n');

	lseek(fd, -DIRENTRYSIZE, SEEK_CUR);
	retval = write(fd, &de, DIRENTRYSIZE);
	if (retval != DIRENTRYSIZE)
		return -6;		// Error writing

	return denum;

}

/* Can be called from signal handler */
int alm_file_rewrite_extents(int fnum) {

	int extnum;
	int de0pos;
	int fd;
	int disk;
	int blk;
	int retval;
	struct ext_ll_t *ext;
	struct cpm_direntry_t de;

	if (fnum >= MAXFILES)
		return -1;		// Bad filenum
	if (!fileinfo[fnum].used)
		return -3;		// File entry not open/used
	disk = fileinfo[fnum].drivenum;
	fd = drvparam[disk].image_fd[0];
	if (fd < 0)
		return -2;		// Disk not open

	de0pos = drvparam[disk].dir_rec_min * RECSIZE;

	ext = fileinfo[fnum].extent;
	extnum = 0;

	// Fill in stuff that doesn't change between entries
	de.user = fileinfo[fnum].usrcode;
	de.s1 = 0;
	memcpy(de.fname, fileinfo[fnum].fname, 8);
	memcpy(de.fext, fileinfo[fnum].fext, 3);

	while (ext != NULL) {
		int size = ext->extsize;
		int extsz;
		extsz = extnum * (drvparam[disk].EXM + 1); // Shift extent # over to make space for file size
		if (ext->extsize != ((drvparam[disk].EXM + 1) * 128)) {	// If extent size == max
			extsz = extsz | ((size >> 7) & drvparam[disk].EXM);
			de.reccnt = size & 0x7F;
		} else {			// Not last extent -> mark full
			de.reccnt = 0x80;
		}
		de.ext_l = extsz & 0x1F;
		de.ext_h = extsz >> 5;
		for (blk = 0; blk < 16; blk++) {
			if (drvparam[disk].DBM < 256) {
				// 8 bit block #s
				de.blknums[blk] = ext->blocks[blk];
			} else {
				// 16 bit block #s
				set_zint16(de.blknums + blk, ext->blocks[blk/2]);
				blk++; // Increment by 2 because 16b not 8b
			}
		}

		//printf("(b)Writing extent (num %d) %d, s2 %d, uc %d, filename ", ext->denum, de.ext_l, de.ext_h, de.user); print_cpm_filename(fileinfo[fnum].fname, fileinfo[fnum].fext);
		//putchar('\n');
		retval = lseek(fd, de0pos + (ext->denum)*DIRENTRYSIZE, SEEK_SET);
		if (retval < 0)
			return -4;		// Error writing an entry
		retval = write(fd, &de, DIRENTRYSIZE);
		if (retval < 0)
			return -4;		// Error writing an entry

		ext = ext->next;
		extnum++;
	}

	return 0;				// If we got here, we were successful

}

/* Can be called from signal handler */
int alm_same_file(const struct cpm_fcb_t *fcb, const uint8_t *fname, const uint8_t *fext) {

	int i;

	for (i=0;i<8;i++) 
		if ((fcb->fname[i] != '?') && (fcb->fname[i] & 0x7F) != (fname[i] & 0x7F))
			return 0;
	for (i=0;i<3;i++)
		if ((fcb->fext[i] != '?') && (fcb->fext[i] & 0x7F) != (fext[i] & 0x7F))
			return 0;

	return 1;
}


int alm_modify_dir(int fileop, int disk, int uc, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp) {

	int fd, fnum, rec;
	struct cpm_fcb_rename_t *fcbren = (struct cpm_fcb_rename_t *)fcb;
	struct cpm_direntry_t des[4];
	int foundfile=-1, entry, modified;

	if (disk >= mmm_numdisks) {
		resp->err = MMMERR_SELECT;
		goto modifydir_error;
	}
	fd = drvparam[disk].image_fd[0];
	if (fd < 0) {
		resp->err = MMMERR_SELECT;
		goto modifydir_error;
	}
	if (drvparam[disk].public_private != PUBLDIR) {
		resp->err = MMMERR_DRVTYPE;
		goto modifydir_error;
	}

	// Make sure file isn't open first
	for (fnum=1; fnum<MAXFILES; fnum++) {
		if (fileinfo[fnum].used && fileinfo[fnum].drivenum == disk &&
			uc == fileinfo[fnum].usrcode && alm_same_file(fcb, fileinfo[fnum].fname, fileinfo[fnum].fext)) {
			resp->err = MMMERR_OK;
			resp->retcode = RETCODE_MISCERR;
			goto modifydir_error;
		}
	}

	// If were doing rename, reject a file name/extention with ?s
	if (fileop == TVSP_FILE_RENAME) {
		int i, found=0;
		for (i=0; i<8; i++)
			if ((fcb->fname[i] & 0x7F) == '?')
				found=1;
		for (i=0; i<3; i++)
			if ((fcb->fext[i] & 0x7F) == '?')
				found=1;

		if (found) {
			resp->err = MMMERR_OK;
			goto modifydir_error;
		}
	}
			

	

	// Read in all the directory sectors, and modify them. 
	
	for (rec = drvparam[disk].dir_rec_min; rec <= drvparam[disk].dir_rec_max; rec++) {
		alm_img_readrec(disk, 0, rec, des);
		modified = 0;
		for (entry=0; entry<4; entry++)	{
			if ( des[entry].user == uc && alm_same_file(fcb, des[entry].fname, des[entry].fext)) {
				foundfile = entry;
				alm_file_modify_thisde(fileop, disk, &(des[entry]), fcbren);
				modified = 1;
			}
		}
		if (modified)
			alm_img_writerec(disk, 0, rec, des);
	}
	
	// We were successful

	set_zint16(resp->fileno, 0xFFFF);
	resp->err = MMMERR_OK;
	if (foundfile != -1) {
		resp->retcode = foundfile;
	} else {
		resp->retcode = RETCODE_MISCERR;
	}


	return 0;
modifydir_error:
	return -1;

}

int alm_file_modify_thisde(int fileop, int disk, struct cpm_direntry_t *de, struct cpm_fcb_rename_t *fcbren) {

	int blk;
	int i;

	switch (fileop) {

		case TVSP_FILE_DELETE:
			// Mark each extent as deleted and re-write them
			de->user = 0xe5;
			for (blk=0; blk<16; blk++) {
				if (drvparam[disk].DBM < 256) {
					if (de->blknums[blk] > 0)
						alm_file_deallocblk(disk, de->blknums[blk]);
				} else {
					if (get_zint16(&(de->blknums[blk])))
						alm_file_deallocblk(disk, get_zint16(&(de->blknums[blk])));
					blk++; // Every other one if 16b
				}
			}

			break;

		case TVSP_FILE_RENAME:
			// Set name from 2nd half of fcb
			// Save/restore permission bits? - Dont for now
			memcpy(de->fname, fcbren->dfname, 8);
			memcpy(de->fext, fcbren->dfext, 3);
			break;

		case TVSP_FILE_SETATTR:
			// Set permission bits from fcb
			for (i=0; i<8; i++)
				de->fname[i] = (de->fname[i] & 0x7F) | (fcbren->sfname[i] & 0x80);
			for (i=0; i<3; i++)
				de->fext[i] = (de->fext[i] & 0x7F) | (fcbren->sfext[i] & 0x80);
			break;
	}

	return 0;

}

int alm_file_blks2fcb(int disk, struct ext_ll_t *ext, struct cpm_fcb_t *fcb) {

	int i;

	for (i=0; i<16; i++) {
		if (drvparam[disk].DBM < 256) {
			if (ext) 
				fcb->al[i] = ext->blocks[i];
			else // special file
				fcb->al[i] = 0xff;
		} else {
			if (ext)
				set_zint16(&(fcb->al[i]), ext->blocks[i/2]);
			else // special file
				set_zint16(&(fcb->al[i]), 0xffff);
			i++;
		}
	}
	return 0;
}

/* Can be called from signal handler */
int alm_file_sync() {

	int fnum;

	for (fnum=1; fnum<MAXFILES; fnum++) {
		if (fileinfo[fnum].used && !fileinfo[fnum].trap) {
			alm_file_rewrite_extents(fnum);
		}
	}

	return 0;
}

/* Can be called from signal handler */
int alm_file_printopen() {

	int fnum, printedany=0;
	char drivestr[3] = {' ', ':', 0};

	for (fnum=1; fnum<MAXFILES; fnum++) {
		if (fileinfo[fnum].used) {
			printedany = 1;
			char filename[13];
			safe_print("File "); safe_print_num(fnum);
			safe_print(": ");
			safe_print_num(fileinfo[fnum].usrcode);
			drivestr[0] = 'A' +fileinfo[fnum].drivenum;
			safe_print(drivestr);
			get_pretty_filename(filename, fileinfo[fnum].fname, fileinfo[fnum].fext);
			safe_print(filename);
			safe_print("' recs ");
			safe_print_num(fileinfo[fnum].size);
			safe_print(" by user port ");
			safe_print_num(fileinfo[fnum].port);
			if (fileinfo[fnum].is_ro) {
				safe_print(" R/O\n");
			} else if (fileinfo[fnum].trap) {
				safe_print(" Special\n");
			} else {
				safe_print("\n");
			}
		}
	}

	if (!printedany)
		safe_print("No files open\n");

	return 0;
}


// Return the matching file number or -1 if we can't find a match/on error
// We need this because the client can only track ~10 open files at a time
// and things like PIP don't close the FCBs that they open
int alm_file_getfnum(int freq_fnum, const struct cpm_fcb_t *fcb, int portnum, int disk, int uc) {

	int fnum;

	if (freq_fnum >= MAXFILES) {
		fnum = -1;
		goto afgf_exit;
	} else if (freq_fnum > 0 && fileinfo[freq_fnum].used && fileinfo[freq_fnum].port == portnum && fileinfo[freq_fnum].drivenum == disk &&
			uc == fileinfo[freq_fnum].usrcode && alm_same_file(fcb, fileinfo[freq_fnum].fname, fileinfo[freq_fnum].fext) ) {
		fnum = freq_fnum;
		goto afgf_exit;
	} else {

		for (fnum = 1; fnum < MAXFILES; fnum++) {
			if (fileinfo[fnum].used && fileinfo[fnum].port == portnum && fileinfo[fnum].drivenum == disk &&
					uc == fileinfo[fnum].usrcode &&	alm_same_file(fcb, fileinfo[fnum].fname, fileinfo[fnum].fext) ) 

				goto afgf_exit;
		}
		fnum = -1;
	}

afgf_exit:
	return fnum;


}
