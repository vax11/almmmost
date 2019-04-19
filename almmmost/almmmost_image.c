/* almmmost_image.c: The disk image interface module for Almmmost.
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
#include "almmmost_osload.h"
#include "almmmost_image.h"
#include "almmmost_device.h"
#include "almmmost_file.h"

struct drive_param_t  drvparam[MAXDISK];

int mmm_pubdrv;

char *disk_image_dir = NULL;

int alm_img_init() {
	int i,j;

	// Default mmm_maxdirs
	mmm_maxdirs = MAXDIRS;

	memset(&drvparam, 0, sizeof(drvparam));

	for (i=0;i<MAXDISK;i++) {
		for (j=0;j<MAXDIRS;j++) {
			drvparam[i].image_fd[j] = -1;
		}
	}

	return 0;
}

int alm_img_exit() {
	int i,j;

	for (i=0; i<MAXDISK; i++) {
		for (j=0; j<MAXDIRS; j++) {
			if (drvparam[i].image_fd[j] >= 0) {
				close(drvparam[i].image_fd[j]);
				drvparam[i].image_fd[j] = -1;
			}
		}
		if (drvparam[i].bam) {
			free(drvparam[i].bam);
			drvparam[i].bam = NULL;
		}

	}

	if (disk_image_dir) {
		free(disk_image_dir);
		disk_image_dir = NULL;
	}

	return 0;
}

int alm_img_ini(struct INI *ini, const char *buf, size_t sectlen) {

	char *section;
	int retval;

	section=alloca(sectlen + 1);
	string_copy(section, buf, sectlen);

	//printf("Evaluating section %s:\n", section);

	if (!strcasecmp(section, "Disks")) {

		do {
			const char *kbuf, *vbuf;
			size_t keylen, vallen;

			retval = ini_read_pair(ini, &kbuf, &keylen, &vbuf, &vallen);
			if (!retval) {
				break; /* End of section */
			} else if (retval<0) {
				//printf("Error reading from INI: %d\n", retval);
				break;
			}

			if (!strncasecmp(kbuf, "Image Dir", 9)) {
				// Directory name
				disk_image_dir = malloc(vallen+1);
				string_copy(disk_image_dir, vbuf, vallen);

			} else if (!strncasecmp(kbuf, "Num Disks", 9)) {
				// Max client #
				mmm_numdisks = strtol(vbuf, NULL, 0);
				if (mmm_numdisks > MAXDISK) {
					printf("Config specified number of disks is greater than MAXDISK limit: %d\n", MAXDISK);
					mmm_numdisks = MAXDISK;
				}

			} else if (!strncasecmp(kbuf, "Max Priv Dirs", 13)) {
				// Max private dir #
				mmm_maxdirs = strtol(vbuf, NULL, 0);
				if (mmm_maxdirs > MAXDIRS) {
					printf("Config specified number of private directories is greater than NUMDIRS limit: %d\n", MAXDIRS);
					mmm_maxdirs = MAXDIRS;
				}

			}
		} while (1);

	} else if (!strncasecmp(section, "Disk ", 5)) {

		int disk = strtol(section+5, NULL, 0);
		if (disk > mmm_numdisks) {
			printf("Disk %d > Num Disks specified %d, check config file.\n", disk, mmm_numdisks);
			return 1;
		}
		do {
			const char *kbuf, *vbuf;
			size_t keylen, vallen;

			retval = ini_read_pair(ini, &kbuf, &keylen, &vbuf, &vallen);
			if (!retval) {
				break; /* End of section */
			} else if (retval<0) {
				printf("Error reading from INI: %d\n", retval);
				break;
			}

			if (!strncasecmp(kbuf, "Image ", 6)) {
				// Disk image filename
				char *image_fname;
				int image_fd;
				int imgdirnum;
				int isro = 0;
				int path_len = strlen(disk_image_dir);

				imgdirnum = strtol(kbuf+6, NULL, 0);
				if (imgdirnum < 0 || imgdirnum >= mmm_maxdirs) {
					//printf("Disk %d Dir num %d > max priv dirs %d\n",
					//		disk, imgdirnum, mmm_maxdirs);
					continue;
				}
				// Default to RW unless it say it's RO
				if (vbuf[2] == ':') {
					isro = !strncasecmp(vbuf,"RO",2);
					// If there's a :, strip off that part
					vbuf += 3;
					vallen -= 3;
				}
				image_fname = alloca(vallen+2+path_len);
				strcpy(image_fname, disk_image_dir);
				image_fname[path_len] = '/';
				string_copy(image_fname+path_len+1, vbuf, vallen);

				if (isro)
					image_fd = open(image_fname, O_RDONLY);
				else
					image_fd = open(image_fname, O_RDWR);

				if (image_fd < 0) {
					perror(image_fname);
					continue;
				}
				drvparam[disk].image_fd[imgdirnum] = image_fd;
				drvparam[disk].is_ro[imgdirnum] = isro;

			} else if (!strncasecmp(kbuf, "Type", 4)) {
				if (!strncasecmp(vbuf, "PRIVATE", 7)) {		// Private
					drvparam[disk].public_private = PRIVDIR;
				} else if (strncasecmp(vbuf, "PUBLIC_ONLY", 11)) {	// Public
					drvparam[disk].public_private = PUBLDIR;
					mmm_pubdrv |= (0x8000 >> disk);
					drvparam[disk].bam = calloc(MAXBLKS, sizeof(int));
				} else {					// Public_only
					drvparam[disk].public_private = PUBLONLYDIR;
				}
			} else if (!strncasecmp(kbuf, "Floppy", 6)) {
				drvparam[disk].is_floppy = ((vbuf[0] & 0x5F) == 'Y');
			} else if (!strncasecmp(kbuf, "SPT", 3)) {
				drvparam[disk].SPT = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "BSF", 3)) {
				drvparam[disk].BSF = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "DBM", 3)) {
				drvparam[disk].DBM = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "DBL", 3)) {
				drvparam[disk].DBL = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "EXM", 3)) {
				drvparam[disk].EXM = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "ALx", 3)) {
				drvparam[disk].dir_ALx = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "RES", 3)) {
				drvparam[disk].res_tracks = strtol(vbuf, NULL,0);
			}
		} while (1);

	}

	return 0;
}

int alm_img_reopen(int disk, int dir, char *filename) {

	int newfd;

	// Check that the numbers are in range, and that the image is already open (ie, parameters are set). Don't do this on public drives.
	if (disk >= mmm_numdisks || dir >= MAXDIRS)
		return -3;
	if (drvparam[disk].image_fd[dir] < 0)
		return -5;
	if (drvparam[disk].public_private == PUBLDIR) 
		return -6;
	if (!filename || !strlen(filename))
		return -7;

	newfd = open(filename, O_RDWR);
	if (newfd < 0) {
		printf("Failed to open new image '%s' for disk %c[%d]\n", filename, 'A'+disk, dir);
		return -7;
	}
	if (drvparam[disk].public_private == PUBLDIR) {
		alm_file_closeallondisk(disk);
	}
	close(drvparam[disk].image_fd[dir]);
	drvparam[disk].image_fd[dir] = newfd;
	if (drvparam[disk].public_private == PUBLDIR) {
		alm_file_loadbam(disk);
	}

	return 0;

}

int alm_img_readrec(int disk, int user, int rec, void *buf) {

	int fd = 0;
	int dir;

	if (!buf)
		return -2;
	if (disk >= mmm_numdisks || user >= MAXUSER)
		return -3;
	if (rec > drvparam[disk].data_rec_max - drvparam[disk].dir_rec_min)
		return -4;
	
	if (drvparam[disk].public_private == PRIVDIR)
		dir = userinfo[user].drive_dir[disk];
	else
		dir = 0;
	fd = drvparam[disk].image_fd[dir];
	
	if (fd < 0)
		return -5;

	lseek(fd, rec*RECSIZE, SEEK_SET);
	return read(fd, buf, RECSIZE);

}

int alm_img_writerec(int disk, int user, int rec, void *buf) {

	int fd = 0;
	int dir;

	if (!buf)
		return -2;
	if (disk >= mmm_numdisks || user >= MAXUSER)
		return -3;
	if (rec > drvparam[disk].data_rec_max - drvparam[disk].dir_rec_min)
		return -4;

	if (drvparam[disk].public_private == PRIVDIR)
		dir = userinfo[user].drive_dir[disk];
	else
		dir = 0;
	fd = drvparam[disk].image_fd[dir];
	
	if (fd < 0)
		return -5;
	if (drvparam[disk].is_ro[userinfo[user].drive_dir[disk]])
		return -6;

	lseek(fd, rec*RECSIZE, SEEK_SET);
	return write(fd, buf, RECSIZE);

}

int alm_generate_drv_param_hdrs(int ostype) {

	int disk;

	if (ostype > max_ostype)
		return -1;

	uint8_t *osimg=bootinfo[ostype].os_image;
	uint16_t base_addr = bootinfo[ostype].os_base;
	uint16_t dph_addr = bootinfo[ostype].hpam_addr+8;
	uint16_t dpb_addr = dph_addr + mmm_numdisks * DISK_PARAM_HDR_BYTES;
	uint16_t dirbuf_addr = dpb_addr + mmm_numdisks * DISK_PARAM_BLK_BYTES;
	uint16_t csv_addr = dirbuf_addr + DIRBUF_BYTES;
	uint16_t alv_addr;
	uint16_t dpbs[MAXDISK];


	if (!osimg)
		return -2;

	/* Generate drvparam values */
	for (disk=0; disk<mmm_numdisks; disk++) {
		uint16_t RPB = 1 << drvparam[disk].BSF;		// Records per block

		drvparam[disk].blk_size = RECSIZE * RPB;
		drvparam[disk].dir_rec_min = drvparam[disk].res_tracks * drvparam[disk].SPT;
		drvparam[disk].dir_rec_max = drvparam[disk].dir_rec_min + drvparam[disk].DBL / 4 + 1;
		drvparam[disk].data_rec_min = drvparam[disk].dir_rec_min + drvparam[disk].dir_ALx * RPB;
		drvparam[disk].data_rec_max = drvparam[disk].dir_rec_min + drvparam[disk].DBM * RPB + (RPB-1);
		drvparam[disk].BLM = drvparam[disk].blk_size/RECSIZE - 1;
		//drvparam[disk].EXM = find_exm(drvparam[disk].DBM, drvparam[disk].blk_size);
		drvparam[disk].tracks = drvparam[disk].res_tracks + drvparam[disk].DBM*RPB/drvparam[disk].SPT + 1;

	}

	/* Add dph values first */
	for (disk=0; disk<mmm_numdisks; disk++) {
		uint16_t dph_off = dph_addr - base_addr;

		dpbs[disk] = dpb_addr - base_addr;

		// Set values in dph
		memset(osimg+dph_off, 0, 2*4); // Zero XLT_VECT and SCRATCH bytes
		set_zint16(osimg + dph_off + 8, dirbuf_addr);
		set_zint16(osimg + dph_off + 10, dpb_addr);
		set_zint16(osimg + dph_off + 12, csv_addr);
		// If drive is removable, it needs a csv
		if (!drvparam[disk].is_floppy)
			alv_addr = csv_addr;
		else
			alv_addr = csv_addr + CSV_BYTES(drvparam[disk].DBL);

		set_zint16(osimg + dph_off + 14, alv_addr);

		// Increment values for next disk
		dph_addr += DISK_PARAM_HDR_BYTES;
		dpb_addr += DISK_PARAM_BLK_BYTES;
		csv_addr = alv_addr + ALV_BYTES(drvparam[disk].DBM);
	}

	/* Now generate dpb values */
	for (disk=0; disk<mmm_numdisks; disk++) {
		uint16_t dpb_off = dpbs[disk];
		uint16_t dir_alloc_bits = 0xFFFF << (16 - drvparam[disk].dir_ALx);

		// 16b - SPT, 8b - BSF, 8b - BLM, 8b - EXM - 16b - DBM, 16b - DBL, 16b - ALx, 16b - CKS, 16b - RES */
		set_zint16(osimg + dpb_off + 0, drvparam[disk].SPT); 		// SPT
		osimg[dpb_off+2] = drvparam[disk].BSF;				// BSF
		osimg[dpb_off+3] = drvparam[disk].BLM;				// BLM
		osimg[dpb_off+4] = drvparam[disk].EXM;				// EXM
		set_zint16(osimg + dpb_off + 5, drvparam[disk].DBM);		// DBM
		set_zint16(osimg + dpb_off + 7, drvparam[disk].DBL);		// DBL
		osimg[dpb_off+9] = dir_alloc_bits >> 8;				// AL0
		osimg[dpb_off+10] = dir_alloc_bits & 0xFF;			// AL1
		if (drvparam[disk].is_floppy)
			set_zint16(osimg + dpb_off + 11, CSV_BYTES(drvparam[disk].DBL));// CKS
		else 
			set_zint16(osimg + dpb_off + 11, 0);			// CKS
		set_zint16(osimg + dpb_off + 13, drvparam[disk].res_tracks);	// RES

	}

	return 0;
}

uint16_t find_exm(uint16_t datablocks, uint16_t blocksize) {

	unsigned int exm, exm0_sz;

	exm0_sz = (datablocks > 255) ? 2048 : 1024;
	exm = (blocksize / exm0_sz) - 1;


	return exm;
}

/* Can be called from signal handler */
int alm_drv_disp_param_hdrs(int disk) {

	char drivestr[3] = {'A'+disk, ':', 0};

	safe_print("Disk parameters, drive "); safe_print(drivestr);
	safe_print("\nBlock size: "); safe_print_num(drvparam[disk].blk_size);
	safe_print("\nDirectories: "); safe_print_num(drvparam[disk].dirs);
	safe_print("\nALx bits: "); safe_print_num( drvparam[disk].dir_ALx);
	safe_print("\npublic_private: "); safe_print_num( drvparam[disk].public_private);
	safe_print("\nis_floppy: "); safe_print_num( drvparam[disk].is_floppy);
	safe_print("\ndir_rec_min: "); safe_print_hex( drvparam[disk].dir_rec_min);
	safe_print("h\ndir_rec_max: "); safe_print_hex( drvparam[disk].dir_rec_max);
	safe_print("h\ndata_rec_min: "); safe_print_hex( drvparam[disk].data_rec_min);
	safe_print("h\ndata_rec_max: "); safe_print_hex( drvparam[disk].data_rec_max);
	safe_print("h\ntracks: "); safe_print_num( drvparam[disk].tracks);
	safe_print("\nSPT: "); safe_print_num( drvparam[disk].SPT);
	safe_print("\nBSF: "); safe_print_num( drvparam[disk].BSF);
	safe_print("\nBLM: "); safe_print_hex( drvparam[disk].BLM);
	safe_print("h\nEXM: "); safe_print_hex( drvparam[disk].EXM);
	safe_print("h\nDBM: "); safe_print_hex( drvparam[disk].DBM);
	safe_print("h\nDBL: "); safe_print_hex( drvparam[disk].DBL);
	safe_print("h\nReserved Tracks: "); safe_print_num( drvparam[disk].res_tracks);
	safe_print("\n");
	return 0;
}

/* Read protocol:
 * Original 10 bytes in
 * 4 byte response out
 * if no error, 128 byte record out
 */
int alm_do_read(int portnum, void *reqbuf) {

	struct tvsp_ipc_response ipc_resp;
	struct tvsp_disk_request *dreqbuf = reqbuf;
	uint8_t readbuf[TVSP_DATA_SZ];

	int retval;
	int disknum = dreqbuf->ndisk;
	int sectnum = (dreqbuf->secth << 8) + dreqbuf->sectl;
	int tracknum = (dreqbuf->trk16h << 8) + dreqbuf->trk16l;
	//printf("Read request, drive %c, track %d, sect %d: ", 
	//		dreqbuf->ndisk + 'A', tracknum, sectnum);

	if ((disknum >= MAXDISK) || (sectnum > drvparam[disknum].SPT) || (tracknum > drvparam[disknum].tracks))  {
		ipc_resp.err = 1;
		ipc_resp.errcode = ERR_BIOS_SELECT;
		printf("Select ERR\n");
	} else {

		int rec = tracknum*drvparam[disknum].SPT + sectnum;

		memset(&ipc_resp, 0, TVSP_RESP_SZ);

		memset(readbuf, 0, TVSP_DATA_SZ);
		//printf("record %d = byte %x: ", rec, rec*RECSIZE);
		retval = alm_img_readrec(disknum, portnum, rec, readbuf);

		if (retval < 1) {
			ipc_resp.err = 1;
			ipc_resp.errcode = ERR_BIOS_READ;
			printf("Read ERR (%d)\n", retval);
		}
	}
	usleep(WRITEDELAY);
	alm_dev_write(&ipc_resp, TVSP_RESP_SZ, portnum);
	if (!ipc_resp.err) {
		//printf("OK\n");
		usleep(WRITEDELAY);
		alm_dev_write(readbuf, TVSP_DATA_SZ, portnum);
	}

	return 0;
}

/* Write protocol:
 * Original 10 bytes in
 * 128 byte buffer in to write to disk
 * 4 byte response out
 */
int alm_do_write(int portnum, void *reqbuf) {

	struct tvsp_ipc_response ipc_resp;
	struct tvsp_disk_request *dreqbuf = reqbuf;
	uint8_t writebuf[TVSP_DATA_SZ];

	int retval;
	int disknum = dreqbuf->ndisk;
	int sectnum = (dreqbuf->secth << 8) + dreqbuf->sectl;
	int tracknum = (dreqbuf->trk16h << 8) + dreqbuf->trk16l;

	memset(&ipc_resp, 0, TVSP_RESP_SZ);

	memset(writebuf, 0, TVSP_DATA_SZ);

	do {
		retval = alm_dev_check_cts(portnum);
		if (alm_do_locate || alm_do_abort) {
			printf("locate/abort: alm_do_write(%d,buf):\n", portnum);
			printf("  Write request, drive %c, write type %d, track %d, sect %d\n", 
					dreqbuf->ndisk + 'A', dreqbuf->wrtype, tracknum, sectnum);
			alm_do_locate = 0;
		}
		if (alm_do_abort) {
			ipc_resp.err = 1;
			ipc_resp.errcode = ERR_BIOS_WRITE;
			alm_do_abort = 0;
			goto dowrite_exit;
		}

	} while (!retval);
	retval = alm_dev_read(writebuf, TVSP_DATA_SZ, portnum);

	if (retval < TVSP_DATA_SZ) {
		ipc_resp.err = 1;
		ipc_resp.errcode = ERR_BIOS_READ;
		printf("Protocol ERR %d: %d\n", retval, errno);
	} else {

		if ((disknum >= MAXDISK) || (sectnum > drvparam[disknum].SPT) || (tracknum > drvparam[disknum].tracks) )  {
			ipc_resp.err = 1;
			ipc_resp.errcode = ERR_BIOS_SELECT;
			printf("Select ERR\n");
		} else {

			int rec = tracknum*drvparam[disknum].SPT + sectnum;
			//printf("record %d = byte %x: ", rec, rec*RECSIZE);
			retval = alm_img_writerec(disknum, portnum, rec, writebuf);
			if (retval < 0) {
				ipc_resp.err = 1;
				ipc_resp.errcode = ERR_BIOS_WRITE;
				printf("Write ERR (%d)\n", retval);
			}
		}
	}

dowrite_exit:
	usleep(WRITEDELAY);
	alm_dev_write(&ipc_resp, TVSP_RESP_SZ, portnum);

	return 0;
}
