/* almmmost_osload.c: Module to handle bootloader/OS load requests for Almmmost.
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
#include "almmmost_osload.h"
#include "almmmost_device.h"


int mmm_genrev;
int mmm_spooldev;

struct host_boot_data_t bootinfo[MAXHOSTID+1];

char *os_image_dir = NULL;
int max_ostype;

/* Initialize variables */
int alm_osl_init() {
	memset(&bootinfo, 0, MAXHOSTID * sizeof(struct host_boot_data_t));
	max_ostype = -1;

	return 0;
}

/* Free allocated memory and clear variables */
int alm_osl_exit() {
	int i;

	for (i=0; i<MAXHOSTID+1; i++) {
		if (bootinfo[i].bootloader)
			free(bootinfo[i].bootloader);
		if (bootinfo[i].os_image)
			free(bootinfo[i].os_image);
	}

	memset(&bootinfo, 0, MAXHOSTID * sizeof(struct host_boot_data_t));

	if (os_image_dir)
		free(os_image_dir);
	os_image_dir = NULL;
	max_ostype = -1;

	return 0;
}

/* Parse the config file and read in os/bootloader images */
int alm_osl_ini(struct INI *ini, const char *buf, size_t sectlen) {

	char *section;
	int retval;

	section=alloca(sectlen + 1);
	string_copy(section, buf, sectlen);

	//printf("Evaluating section %s:\n", section);

	if (!strcasecmp(section, "Clients")) {

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

			if (!strncasecmp(kbuf, "Image Dir", 9)) {
				// Directory name
				os_image_dir = malloc(vallen+1);
				string_copy(os_image_dir, vbuf, vallen);

			} else if (!strncasecmp(kbuf, "Max Client", 10)) {
				// Max client #
				max_ostype = strtol(vbuf, NULL, 0);

			}
		} while (1);

	} else if (!strncasecmp(section, "Client OSTYPE ", 13)) {

		int ostype = strtol(section+13, NULL, 0);
		if (ostype > max_ostype) {
			printf("OSTYPE %d > Max Client %d, check config file.\n", ostype, max_ostype);
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

			if (!strncasecmp(kbuf, "Boot", 4)) {
				// Bootloader file name
				char *boot_fname;
				int boot_fd;
				int path_len = strlen(os_image_dir);

				boot_fname = alloca(vallen+2+path_len);
				strcpy(boot_fname, os_image_dir);
				boot_fname[path_len] = '/';
				string_copy(boot_fname+path_len+1, vbuf, vallen);

				printf("OS %d reading bootloader %s\n", ostype, boot_fname);
				boot_fd = open(boot_fname, O_RDONLY);
				if (boot_fd < 0) {
					perror(boot_fname);
					break;
				}

				bootinfo[ostype].bootloader = calloc(BOOTLOADER_SIZE,1);
				if (!bootinfo[ostype].bootloader) {
					perror("Allocating Bootloader");
					exit(1);
				}
				read(boot_fd, bootinfo[ostype].bootloader, BOOTLOADER_SIZE);
				close(boot_fd);
			} else if (!strncasecmp(kbuf, "OS", 2)) {
				// OS image filename
				char *os_fname;
				int os_fd;
				int path_len = strlen(os_image_dir);

				os_fname = alloca(vallen+2+path_len);
				strcpy(os_fname, os_image_dir);
				os_fname[path_len] = '/';
				string_copy(os_fname+path_len+1, vbuf, vallen);

				printf("OS %d reading OS %s\n", ostype, os_fname);
				os_fd = open(os_fname, O_RDONLY);
				if (os_fd < 0) {
					perror(os_fname);
					break;
				}

				bootinfo[ostype].os_image = calloc(OSIMAGE_SIZE,1);
				if (!bootinfo[ostype].os_image) {
					perror("Allocating OS IMAGE");
					exit(1);
				}
				read(os_fd, bootinfo[ostype].os_image, OSIMAGE_SIZE);
				close(os_fd);

			} else if (!strncasecmp(kbuf, "Base", 4)) {
				// OS Base address
				bootinfo[ostype].os_base = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "HPAM", 4)) {
				// Host parameters memory offset
				bootinfo[ostype].hpam_addr = strtol(vbuf, NULL,0);
			} else if (!strncasecmp(kbuf, "CONBUF", 6)) {
				// Console buffer address
				bootinfo[ostype].conbuf = strtol(vbuf, NULL,0);
			}
		} while (1);

	}

	return 0;
}

/* Add OS and drive parameters to the images */
int alm_osl_tailor_images() {

	int i;

	for (i=0; i<=max_ostype; i++) {
		uint8_t *buf=bootinfo[i].os_image;
		unsigned int hpam_off = bootinfo[i].hpam_addr - bootinfo[i].os_base;
		//printf("OS Type %d Header at 0x%x:", i, hpam_off);
		if (buf) {
			//printf("buffer allocated.\n");
			set_zint16(buf+hpam_off+0, bootinfo[i].conbuf);
			buf[hpam_off+2] = 0x69; /* Processor ID location */
			buf[hpam_off+3] = mmm_spooldrv;
			buf[hpam_off+4] = mmm_genrev;
			set_zint16(buf+hpam_off+5, mmm_pubdrv);
			buf[hpam_off+7] = mmm_numdisks;
			alm_generate_drv_param_hdrs(i);
		} else {
			//printf("buffer unallocated.\n");
		}
	}

	return 0;
}

/* Can be called from signal handler */
int alm_osl_print_imginfo() {
	
	int i,j;

	for (i=0; i<max_ostype; i++) {
		uint8_t *buf=bootinfo[i].os_image;
		unsigned int hpam_off = bootinfo[i].hpam_addr - bootinfo[i].os_base;
		if (buf) {

			safe_print("OS Type ");
			safe_print_num(i);
			safe_print("Header at ");
			safe_print_hex(hpam_off);
			safe_print("h:");
			for (j=0;j<8;j++) {
				safe_print(" ");
				safe_print_hex(buf[hpam_off+j]);
			}
			safe_print("\n");

		}
	}

	return 0;
}

/* Send the bootloader out port portnum */
int alm_osl_send_bootloader(int portnum, void *reqbuf) {

	int retval;
	struct tvsp_boot_request *bootreq = reqbuf;
	int ostype = bootreq->usr;

	if (ostype > max_ostype) {
		printf("OSTYPE out of range: %d\n", ostype);
		return -1;
	} else if (!bootinfo[ostype].bootloader) {
		printf("Bootloader not loaded for OSTYPE %d\n", ostype);
		return -1;
	} else {
		printf("Sending bootloader OSTYPE %d to port %d\n", ostype, portnum);

		usleep(WRITEDELAY);
		retval = alm_dev_write(bootinfo[ostype].bootloader, BOOTLOADER_SIZE, portnum);

		if (retval != BOOTLOADER_SIZE) {
			printf("Failed to send data: %d\n", errno);
			return -1;
		}
	}
	return 0;

}

/* Can be called from signal handler */
int alm_osl_savemodifiedos(int ostype, char *filename) {

	if (ostype >= max_ostype || !filename)
		return 0;

	int fd = open(filename, O_RDWR | O_CREAT);
	if (fd < 0)
		return 0;
	//printf("Writing modified OSIMAGE %d to %s.\n", ostype, filename);
	write(fd, bootinfo[ostype].os_image, OSIMAGE_SIZE);
	close(fd);
	return 0;
}

/* Send the os image out port portnum, and clear locks (locking not yet implemented) */
int alm_osl_send_os(int portnum, void *reqbuf) {
	
	int retval;
	int recnum;
	struct tvsp_boot_request *bootreq = reqbuf;
	int numsects = bootreq->sects+2;
	int ostype = bootreq->usr;

	if (ostype > max_ostype) {
		printf("OSTYPE out of range: %d\n", ostype);
		return -1;
	} else if (!bootinfo[ostype].os_image) {
		printf("Bootloader not loaded for OSTYPE %d\n", ostype);
		return -1;
	} else {
		uint8_t *osimg = bootinfo[ostype].os_image;

		//FIXME: alm_file_clear_locks(portnum);

		printf("Sending os image, machine ID %d, cboot=%d, start rec %d, length %d\n", 
				ostype, bootreq->cboot, bootreq->recnum, bootreq->sects);
		for (recnum = bootreq->recnum; recnum < (numsects + bootreq->recnum); recnum++) {

			usleep(5000);
			retval = alm_dev_write(osimg+(recnum*TVSP_DATA_SZ), TVSP_DATA_SZ, portnum);

			if (retval != TVSP_DATA_SZ) {
				printf("Failed to send record %d: %d\n", recnum, errno);
				return -1;
			}
		}
	}

	return 0;
}
