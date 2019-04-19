/* tvi_sdlc_ioctl.c: Use the tvi_sdlc kernel module to do things 
 * via its ioctls, and display the results
 *
 * Almmmost is a modern replacement for the TeleVideo MmmOST network 
 * operating system used on the TeleVideo TS-8xx Zilog Z80-based computers 
 * from the early 1980s.
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "tvi_sdlc.h"

int main(int argc, char **argv) {


	int fd;
	unsigned int cmdnum, ab, value, retval;

	if (argc != 5) {
		printf("Usage: %s <device> <command> <port> <value>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("Opening device");
		return 2;
	}

	cmdnum = strtoul(argv[2], NULL, 0);
	ab = strtoul(argv[3], NULL, 0) & 0xF;
	value = strtoul(argv[4], NULL, 0);

	retval = ioctl(fd, cmdnum, TVI_SDLC_IOCTL_DATA(ab,value));

	printf("ioctl 0x%08x to port %d with value %d returned %d (0x%x)\n", cmdnum, ab, value, retval, retval);
	
	close(fd);

	return 0;
}

