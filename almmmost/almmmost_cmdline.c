/* almmmost_cmdline.c, the server command-line interface module for Almmmost.
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>

#include <ini.h>

#include <tvi_sdlc.h>

#include "almmmost.h"
#include "almmmost_image.h"
#include "almmmost_device.h"
#include "almmmost_osload.h"
#include "almmmost_file.h"
#include "almmmost_misc.h"
#include "almmmost_special.h"
#include "almmmost_cmdline.h"

#define CMDBUFSIZE (1024)

/* Signal handler for SIGINT, which provides a command line to control the server */
void alm_cmd_sigint(int signal) {

	char cmdbuf[CMDBUFSIZE];
	char *cpretval;
	int i, retval;

	safe_print("\nAlmmmost> ");
	if (!safe_get_buf(cmdbuf, CMDBUFSIZE)) {
		safe_print("Empty command, returning.\n");
		return;
	}

	cmdbuf[CMDBUFSIZE-1] = 0;

	// Trim CR and whitespace off end of command -- so no filename ending in ' '...
	i=strlen(cmdbuf)-1;
	while(isspace(cmdbuf[i]))
		i--;
	// End the string after the last non-space character
	cmdbuf[i+1] = 0;
	// Skip the beginning space
	i=0;
	while(isspace(cmdbuf[i]) && cmdbuf[i])
		i++;

	if (!strncasecmp(cmdbuf+i, "abort", 5)) {
		// Abort the current do/while loop
		alm_do_abort = 1;
	} else if (!strncasecmp(cmdbuf+i, "locate", 6)) {
		// See what do/while loop we're stuck in
		alm_do_locate = 1;
	} else if (!strncasecmp(cmdbuf+i, "reopen ", 7)) {
		// Open a new image
		i+=7;
		while (isspace(cmdbuf[i]) && cmdbuf[i])
			i++;
		int disk = ((cmdbuf[i] & 0xDF) - 'A');
		int dir = 0;
		if (disk < 0 || disk > mmm_numdisks) {
			char diskstr[] = { cmdbuf[i],'\n',0};
			safe_print("disk out of range: ");
			safe_print(diskstr);
			return;
		}
		i++;
		if (cmdbuf[i] == ':') {  /* Directory number */
			dir = strtoul(cmdbuf+i, &cpretval, 0);
			if (!cpretval) {
				safe_print("Error with directory number parsing.\n");
				return;
			}
			if (dir < 0 || dir > mmm_maxdirs) {
				safe_print("Directory number ");
				safe_print_num(dir);
				safe_print(" out of range.\n");
				return;
			}
			i = (cpretval - cmdbuf);
		}
		while (isspace(cmdbuf[i]) && cmdbuf[i])
			i++;

		// Assume if first char isn't a /, then the path is relative to imagedir
		if (cmdbuf[i] == '/') {
			retval = alm_img_reopen(disk, dir, cmdbuf+i);
			if (retval < 0) {
				safe_print("Error re-opening file '");
				safe_print(cmdbuf+i);
				safe_print("'.\n");
			}
		} else {
			char pathbuf[INPBUFSIZE * 2];
			strcpy(pathbuf, disk_image_dir);
			strcat(pathbuf, "/");
			strcat(pathbuf, cmdbuf+i);
			retval = alm_img_reopen(disk, dir, cmdbuf+i);
			if (retval < 0) {
				safe_print("Error re-opening file '");
				safe_print(pathbuf);
				safe_print("'.\n");
			}
		}


	} else if (!strncasecmp(cmdbuf+i, "filein ", 7)) {
		// Change what file FILEIN.SYS comes from
		i+=7;
		while (isspace(cmdbuf[i]) && cmdbuf[i])
			i++;
		strcpy(fileinsys_name, cmdbuf+i);
	} else if (!strncasecmp(cmdbuf+i, "fileout ", 8)) {
		// Change what file FILEOUT.SYS goes to
		i+=8;
		while (isspace(cmdbuf[i]) && cmdbuf[i])
			i++;
		strcpy(fileoutsys_name, cmdbuf+i);
	} else if (!strncasecmp(cmdbuf+i, "closeport ", 10)) {
		// Close the files open on port n
		i+=10;
		int port = strtol(cmdbuf + i, NULL, 0);
		if (port < 0 || port >= alm_dev_ports) {
			safe_print("Port number out of range.\n");
			return;
		}
		alm_file_clearfiles(port);
	} else if (!strncasecmp(cmdbuf+i, "printfil", 8)) {
		alm_file_printopen();
	} else if (!strncasecmp(cmdbuf+i, "printspe", 8)) {
		alm_special_printlist();
	} else if (!strncasecmp(cmdbuf+i, "printdpb", 8)) {
		for (i=0; i<mmm_numdisks; i++)
			alm_drv_disp_param_hdrs(i);
	} else if (!strncasecmp(cmdbuf+i, "printhpb", 8)) {
		alm_osl_print_imginfo();
	} else if (!strncasecmp(cmdbuf+i, "saveos ", 7)) {
		i+= 7;

		int osnum=0;
		char *nexttok;

		osnum = strtol(cmdbuf+i, &nexttok, 0);
		if (!nexttok) {
			safe_print("Error getting OS number\n");
			return;
		}
		i = (nexttok - cmdbuf);
		while (cmdbuf[i] && isspace(cmdbuf[i]))
			i++;

		alm_osl_savemodifiedos(osnum, cmdbuf+i);
	} else if (!strncasecmp(cmdbuf+i, "sync", 4)) {
		alm_file_sync();
	} else if (!strncasecmp(cmdbuf+i, "exit", 4) || !strncasecmp(cmdbuf+i, "quit", 4)) {
		alm_file_sync();
		raise(SIGQUIT);
	} else {
		safe_print("Unknown command: '");
		safe_print(cmdbuf);
		safe_print("'.\n");
	}

	return;
}
