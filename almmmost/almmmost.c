/* almmmost.c: the main() module and misc functions for Almmmost.
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

int mmm_genrev;
int mmm_spooldrv;
int mmm_numdisks;
int mmm_maxdirs;
int alm_do_abort = 0;
int alm_do_locate = 0;

struct user_port_data_t userinfo[MAXUSER];

int main(int argc, char **argv) {

	int retval, i, j, lastport, reqport=-1;
	unsigned char reqbuf[BUFFER_SIZE];
	struct timeval curtime;
	unsigned long request_serial = 0;

	struct INI *ini;

	memset(reqbuf, 0, BUFFER_SIZE);

	for (i=0; i<MAXUSER; i++) {
		for (j=0; j<MAXDISK; j++) {
			userinfo[i].drive_dir[j] = -1;
		}
			userinfo[i].defdrive = -1;
	}

	if (argc != 2) {
		printf("Usage: %s <config.ini>\n", argv[0]);
		return 1;
	}

	/* Initialize variables in modules */
	alm_dev_init();
	alm_img_init();
	alm_osl_init();
	alm_file_init();
	alm_special_init();

	/* Process config file */
	ini = ini_open(argv[1]);
	parse_args(ini);
	ini_close(ini);

	/* Modify OS images to have appropriate drive info */
	alm_osl_tailor_images();

	/* Load block allocation maps for public drives */
	for (i=0; i<mmm_numdisks; i++) {
		if (drvparam[i].public_private == PUBLDIR)
			alm_file_loadbam(i);
	}

	/* Print generated values for debugging fun and excitement */
	//alm_osl_print_imginfo();
	//alm_osl_savemodifiedos(4,"/tmp/USERCPM4.out");
	
	/* Add ^C handler */
	struct sigaction sa_int;
	sa_int.sa_handler = alm_cmd_sigint;
	sa_int.sa_flags = 0;
	sigemptyset(&sa_int.sa_mask);
	sigaction(SIGINT, &sa_int, NULL);


	do { /* Main loop */

		lastport = reqport;
		reqport = -1;

		// Do a small delay to fix ghost CTS
		usleep(45);
		//gettimeofday(&curtime, NULL);
		//printf("%s.%03ld (%lu): waiting for CTS...", ctime(&curtime.tv_sec), curtime.tv_usec/1000, request_serial);
		do {
			for (i=0; i<alm_dev_ports; i++) {
				// Start with port # after the last one serviced
				int j=(i+lastport+1) % alm_dev_ports;
				if (alm_dev_check_cts(j) > 0) {
					reqport = j;
					break;
				}
				if (alm_do_locate || alm_do_abort) {
					printf("locate/abort: Main CTS loop\n");
					alm_do_locate = 0;
					alm_do_abort = 0;
				}
			}
		} while (reqport < 0);


		errno = 0;
		retval = alm_dev_read(reqbuf, TVSP_REQ_SZ, reqport);

		//printf("CTS, port %d\n", reqport);


		if (retval < TVSP_REQ_SZ) {
			printf("Request too small, port %d: %d (%d) ", reqport, retval, errno);
			if (retval > 0) 
				print_hex(reqbuf, retval);
			else
				putchar('\n');
			// Reset the port if there's an error
			alm_dev_reset(reqport);
			continue;
		} 	
		if (reqbuf[0] == TVSP_SOR1) {
			// SOR1 = OS request
			switch (reqbuf[1]) {
				case TVSP_BOOT:
					alm_file_clearfiles(reqport);
					if (reqbuf[6] == 4 
							&& reqbuf[7] == 5
							&& reqbuf[8] == 6
							&& reqbuf[9] == 7)
						alm_osl_send_bootloader(reqport, reqbuf);
					else
						alm_osl_send_os(reqport, reqbuf);
					break;

				case TVSP_BRKSPOOL:
					alm_break_spool(reqport, reqbuf);
					break;

				case TVSP_CHECK:
					alm_do_check(reqport, reqbuf);
					break;

				case TVSP_READSECT:
					alm_do_read(reqport, reqbuf);
					break;

				case TVSP_WRITESECT:
					alm_do_write(reqport, reqbuf);
					break;

				case TVSP_FILEOP:
					alm_do_fileop(reqport, reqbuf);
					break;

				default:
					printf("Unknown request: ");
					print_hex(reqbuf, TVSP_REQ_SZ);
					alm_dev_reset(reqport);
					break;
			}
		} else if (reqbuf[0] == TVSP_SOR0) {
			// SOR0 = program request
			if (reqbuf[1] == 'C' && reqbuf[3] == 'L') {
				alm_do_logon(reqport, reqbuf);
			} else {
				printf("Unknown request: ");
				print_hex(reqbuf, TVSP_REQ_SZ);
			}
		}
		request_serial++;

	} while (1);


	alm_file_exit();
	alm_img_exit();
	alm_osl_exit();
	alm_dev_exit();

	return 0;
}

/* Print a string of bytes in hex */
void print_hex(unsigned char *buffer, int count) {

	int i;

	for (i=0; i<count; i++) {
		printf("%02x ", buffer[i]);
	}
	putchar('\n');
}

/* Print a string, escaping special characters */
void print_safe_ascii(unsigned char *buffer, int count) {

	int i;
	for (i=0; i<count; i++) {
		if (buffer[i] >= 32 && buffer[i] <127)
			putchar(buffer[i]);
		else
			printf("\\x%02x", buffer[i]);
	}
	putchar('\n');
}

/* Print a filename from an FCB, directory entry, etc in 8.3 format */
void print_cpm_filename(uint8_t *fname, uint8_t *ext) {

	int i=0;

	for (i=0;i<8;i++) {
		if (fname[i] == 0x20)
			break;
		putchar(fname[i] & 0x7F);
	}

	putchar('.');
	for (i=0;i<3;i++) {
		if (ext[i] == 0x20)
			break;
		putchar(ext[i] & 0x7F);
	}

}

/* Main function to handle sections in INI file */
void parse_args(struct INI *ini) {

	const char *buf;
	size_t sectlen;
	int retval;

	if (!ini) {
		printf("Error opening configuration file.\n");
		exit(1);
	}

	do {
		retval = ini_next_section(ini, &buf, &sectlen);
		if (!retval)
			return;
		if (retval < 0) {
			printf("Reading INI error: %d\n", retval);
			exit(1);
		}

		if (!strncasecmp(buf, "General", 7)) {
			alm_gen_ini(ini, buf, sectlen); /* Parse general parameters */
		} else if (!strncasecmp(buf, "Device", 6)) {
			alm_dev_ini(ini, buf, sectlen); /* Parse device info */
		} else if (!strncasecmp(buf, "Client", 6)) {
			alm_osl_ini(ini, buf, sectlen); /* Parse client section */
		} else if (!strncasecmp(buf, "Disk", 4)) {
			alm_img_ini(ini, buf, sectlen); /* Parse image info */
		} else if (!strncasecmp(buf, "Port", 4)) {
			alm_port_ini(ini, buf, sectlen); /* Parse user port config info */
		}
	} while (1);

}

/* Parse General INI section */
void alm_gen_ini(struct INI *ini, const char *buf, size_t sectlen) {

	char *section;
	int retval;

	section=alloca(sectlen + 1);
	string_copy(section, buf, sectlen);

	//printf("Evaluating section %s:\n", section);

	do {
		const char *kbuf, *vbuf;
		size_t keylen, vallen;

		retval = ini_read_pair(ini, &kbuf, &keylen, &vbuf, &vallen);
		if (!retval) {
			return; /* End of section */
		} else if (retval<0) {
			printf("Error reading from INI: %d\n", retval);
			return;
		}

		if (!strncasecmp(kbuf, "Genrev", 6)) {
			mmm_genrev = strtol(vbuf,NULL,0);
			//printf("Genrev = %d\n", mmm_genrev);
		} else if (!strncasecmp(kbuf, "Spool Drive", 11)) {
			mmm_spooldrv = strtol(vbuf, NULL,0);
			//printf("Spool Drive = %d\n", mmm_spooldrv);
		}
	} while (1);
}

/* Parse Port INI section */
void alm_port_ini(struct INI *ini, const char *buf, size_t sectlen) {

	char *section;
	int retval;
	int portnum;

	section=alloca(sectlen + 1);
	string_copy(section, buf, sectlen);

	//printf("Evaluating section %s:\n", section);
	portnum = strtol(section + 5, NULL, 0);
	if (portnum > MAXUSER) {
		printf("Port # greater than max user: %d", MAXUSER);
		return;
	}

	do {
		const char *kbuf, *vbuf;
		size_t keylen, vallen;

		retval = ini_read_pair(ini, &kbuf, &keylen, &vbuf, &vallen);
		if (!retval) {
			return; /* End of section */
		} else if (retval<0) {
			printf("Error reading from INI: %d\n", retval);
			return;
		}

		if (!strncasecmp(kbuf, "Autologon", 9)) {
			userinfo[portnum].autologon = !strncasecmp(vbuf,"y",1);
			//printf("Autologon = %d\n", userinfo[portnum].autologon);
		} else if (!strncasecmp(kbuf, "Private Dir", 11)) {
			int i;
			int pdir = strtol(vbuf, NULL,0);
			//printf("Private Dir = %d\n", pdir);
			for (i=0;i<MAXDISK;i++)
				userinfo[portnum].drive_dir[i] = pdir;
		}
	} while (1);
}

/* Copy a string for a max len, and ensure it's null terminated */
void string_copy(char *dest, const char *src, size_t len) {
	
	memcpy(dest, src, len);
	dest[len] = 0;

}

/* Convert endianness between Z80 and server as necessary */
/* Can be called from signal handler */
void set_zint16(uint8_t *dest, uint16_t value) {

	*dest = value & 0xFF;
	*(dest+1) = value >> 8;

}

/* Convert endianness between Z80 and server as necessary */
/* Can be called from signal handler */
uint16_t get_zint16(uint8_t *src) {

	return ( (*(src+1) << 8) + *src );

}


int get_direntry_fname(char *dest, void *direntry) {

	struct cpm_direntry_t *de = direntry;

	if (!dest || !de)
		return -1;

	return get_pretty_filename(dest, de->fname, de->fext);

}

/* Print out a directory entry */
int print_direntry(void *direntry, int disk) {

	char fname[13];
	struct cpm_direntry_t *de = direntry;
	int i;

	get_direntry_fname(fname, direntry);
	
	fprintf(stderr, "User flag: %02xh Filename: '%s' %s %s Extent %02xh Rec count %d\n",
			de->user, fname, (de->fext[0] & 0x80 ? "R/O" : ""),
			(de->fext[1] & 0x80 ? "SYS" : ""), 
			(32*de->ext_h + de->ext_l)/(1+drvparam[disk].EXM), de->reccnt);

	fprintf(stderr, "Blocks:");

	if (drvparam[disk].DBM < 256) {
		for (i=0; i<16; i++)
			fprintf(stderr, " %02x", de->blknums[i]);
	} else {
		for (i=0; i<16; i+=2)
			fprintf(stderr, " %04x", get_zint16(&de->blknums[i]));
		
	}
	fprintf(stderr, "\n");

	return 0;
}

#define STDOUT (1)
#define STDIN (0)

/* Can be called from signal handler */
int safe_print(char *buffer) {

	int len = strlen(buffer);
	write(STDOUT, buffer, len);
	return 0;
}

/* Can be called from signal handler */
int safe_print_num(int i) {

	char digit = (i % 10) + '0';

	if ((i/10) != 0)
		safe_print_num(i/10);
	write(STDOUT, &digit, 1);
	return 0;
}

/* Can be called from signal handler */
int safe_print_hex(int i) {

	char digit = (i & 0xF) + '0';

	if (digit > '9')
		digit += 7; // Convert to letters

	if ((i>>4) != 0)
		safe_print_num(i>>4);
	write(STDOUT, &digit, 1);
	return 0;
}

/* Can be called from signal handler */
int safe_get_buf(char *buffer, int len) {

	int i = 0;

	if (!len || !buffer)
		return 0;
	do {
		if (!read(STDOUT, buffer + i, 1)) {
			break;
		}
		i++;
	} while ((buffer[i-1] != 10) && (buffer[i-1] != 13) && (i < (len-1)));

	buffer[i] = 0;
	return strlen(buffer);
}

/* Convert file name from FCB/directory entry to 8.3 string */
/* Can be called from signal handler */
int get_pretty_filename(char *dest, uint8_t *fname, uint8_t *fext) {

	if (!dest || !fname || !fext)
		return -1;

	int i=0, j=0, k;
	for (j=7;j>=0;j--) {
		if ((fname[j] & 0x7F) != ' ')
			break;
	}
	for (i=0; i<=j; i++) {
		dest[i] = fname[i] & 0x7F;
	}
	dest[i] = '.';
	for (j=2;j>=0;j--) {
		if ((fext[j] & 0x7F) != ' ')
			break;
	}
	i++;
	for (k=0;k<=j; k++) {
		dest[i++] = fext[k] & 0x7F;
	}
	dest[i] = 0;
	return 0;

}
