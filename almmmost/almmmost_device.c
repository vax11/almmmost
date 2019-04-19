/* almmmost_device.c, the kernel device driver interface module for Almmmost.
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

#include <ini.h>
#include <tvi_sdlc.h>

#include "almmmost.h"
#include "almmmost_device.h"

int alm_dev_fd[MAXUSER];
int alm_dev_pnum[MAXUSER];
int alm_dev_ports;

int alm_dev_init() {

	int i;

	for (i=0;i<MAXUSER;i++) {
		alm_dev_fd[i] = -1;
		alm_dev_pnum[i] = -1;
	}
	alm_dev_ports = 0;

	return 0;

}

int alm_dev_ini(struct INI *ini, const char *buf, size_t sectlen) {

	char *section;
	int retval;

	int i;

	section=alloca(sectlen + 1);
	string_copy(section, buf, sectlen);

	//printf("Evaluating section %s:\n", section);

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

		if (!strncasecmp(kbuf, "User Dev ", 9)) {
			// Device file name
			char *dev_fname;
			int dev_fd;
			unsigned int devnum = strtoul(kbuf+9, NULL, 0);

			if (devnum >= MAXUSER || alm_dev_fd[devnum] >= 0) {
				printf("Bad or opened device number %d\n", devnum);
				break;
			}

			dev_fname = alloca(vallen+1);
			string_copy(dev_fname, vbuf, vallen);

			dev_fd = open(dev_fname, O_RDWR);
			if (dev_fd < 0) {
				perror("Opening device");
				break;
			}

			alm_dev_fd[devnum] = dev_fd;
			printf("User %d Device = %s\n", devnum, dev_fname);

		} else if (!strncasecmp(kbuf, "User Port ", 10)) {
			// 8530 port number
			unsigned int devnum = strtoul(kbuf+10, NULL, 0);
			int portnum;

			if (devnum >= MAXUSER || alm_dev_fd[devnum] < 0) {
				printf("Bad or unopened device number %d\n", devnum);
				break;
			}

			portnum = strtol(vbuf, NULL, 0);

			retval = ioctl(alm_dev_fd[devnum], TVI_SDLC_IOCTL_SET_PORT, TVI_SDLC_IOCTL_DATA(portnum,0));
			
			if (retval < 0) {
				perror("Setting port number");
				break;
			}
			alm_dev_pnum[devnum] = portnum;
			//printf("User %d port = %d\n", devnum, portnum);

		} else if (!strncasecmp(kbuf, "Ports", 5)) {
			// Number of ports
			alm_dev_ports = strtol(vbuf, NULL,0);
			//printf("Ports = %d\n", alm_dev_ports);
		}
	} while (1);

	// Check that we set 8530 port #s for each port
	for (i=0;i<MAXUSER;i++) {
		if (alm_dev_fd[i] >= 0 && alm_dev_pnum[i] < 0) {
			printf("Device %d opened but no port number set; closing.\n", i);
			close(alm_dev_fd[i]);
			alm_dev_fd[i] = -1;
		}
	}

	return 0;
}


int alm_dev_exit() {

	int i;

	for (i=0; i<alm_dev_ports; i++) {
		if (alm_dev_fd[i] >=0) {
			ioctl(alm_dev_fd[i], TVI_SDLC_IOCTL_RESET, TVI_SDLC_IOCTL_DATA(alm_dev_pnum[i],0));
			close(alm_dev_fd[i]);
			alm_dev_fd[i] = -1;
			alm_dev_pnum[i] = -1;
		}
	}

	return 0;

}

int alm_dev_reset(int portnum) {
	
	if (portnum < 0 || portnum >= MAXUSER)
		return -1;

	if (alm_dev_fd[portnum] < 0 || alm_dev_pnum[portnum] < 0)
		return -2;

	ioctl(alm_dev_fd[portnum], TVI_SDLC_IOCTL_RESET, TVI_SDLC_IOCTL_DATA(alm_dev_pnum[portnum],0));

	// Re-init both requested port and other port on same chip
	ioctl(alm_dev_fd[portnum], TVI_SDLC_IOCTL_INIT, TVI_SDLC_IOCTL_DATA(alm_dev_pnum[portnum & 0xFE],0));
	ioctl(alm_dev_fd[portnum], TVI_SDLC_IOCTL_INIT, TVI_SDLC_IOCTL_DATA(alm_dev_pnum[portnum | 0x1],0));

	return 0;

}

int alm_dev_check_cts(int portnum) {

	if (portnum < 0 || portnum >= MAXUSER)
		return -1;

	if (alm_dev_fd[portnum] < 0 || alm_dev_pnum[portnum] < 0)
		return -2;

	return ioctl(alm_dev_fd[portnum], TVI_SDLC_IOCTL_GET_CTS, TVI_SDLC_IOCTL_DATA(alm_dev_pnum[portnum],0));

}

int alm_dev_read(void *buf, size_t size, int portnum) {

	if (!buf || size < 1 || portnum < 0 || portnum >= MAXUSER)
		return -1;

	if (alm_dev_fd[portnum] < 0 || alm_dev_pnum[portnum] < 0)
		return -2;

	return read(alm_dev_fd[portnum], buf, size);
}

int alm_dev_write(void *buf, size_t size, int portnum) {

	if (!buf || size < 1 || portnum < 0 || portnum >= MAXUSER)
		return -1;

	if (alm_dev_fd[portnum] < 0 || alm_dev_pnum[portnum] < 0)
		return -2;

	return write(alm_dev_fd[portnum], buf, size);
}
