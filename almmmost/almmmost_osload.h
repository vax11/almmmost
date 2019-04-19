/* almmmost_osload.h: Module to handle bootloader/OS load requests for Almmmost.
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

#ifndef _ALMMMOST_OSLOAD_H
#define _ALMMMOST_OSLOAD_H

#define MAXHOSTID (7)

#define HOST_PARAMS_BYTES (8)

/* From USERCPMx on TS-806 example: (All in hex)
 *
 * 		Univ.	802h	803	803h	800R	tpci
 * 		U	1	4	5	6	7
 * OS load off	CB80	C780	C380	C380	CB80	C380
 * Off in img	2AE2	3011	340D	36BF	2CC3	33F6
 * Off in mem	F662	F791	F78D	FA3F	F843	F776
 * conbuf	F640	F76F	F6A0	F948	F59F	F661
 * processor_	52	1e	3e	c3	00	af
 * spool_	01	01	01	01	01	01
 * genrev	01	01	01	01	01	01
 * pubdrive	4000	4000	4000	4000	4000	4000
 * num_disks	03	03	03	03	03	03
 */

struct host_boot_data_t {
	uint8_t *bootloader;
	uint8_t *os_image;
	unsigned int os_base;
	unsigned int conbuf;
	unsigned int hpam_addr;
};

struct tvsp_boot_request {
	uint8_t sor;
	uint8_t req;
	uint8_t usr;
	uint8_t cboot;
	uint8_t sects;
	uint8_t recnum;
	uint8_t x[4];
};
	
#define BOOTLOADER_SIZE (128)
#define OSIMAGE_SIZE (64*1024)

extern int mmm_genrev;
extern int mmm_spooldev;
extern int max_ostype;

extern struct host_boot_data_t bootinfo[];

/* Initialize variables */
int alm_osl_init();

/* Free allocated memory and clear variables */
int alm_osl_exit();

/* Parse the config file and read in os/bootloader images */
int alm_osl_ini(struct INI *ini, const char *buf, size_t sectlen);

/* Add OS and drive parameters to the images */
int alm_osl_tailor_images();

/* Print generated values */
int alm_osl_print_imginfo();

/* Save a modified copy of the OS for debugging purposes */
int alm_osl_savemodifiedos(int ostype, char *filename);

/* Send the bootloader out port portnum */
int alm_osl_send_bootloader(int portnum, void *reqbuf);

/* Send the os image out port portnum, and clear locks (locking not yet implemented) */
int alm_osl_send_os(int portnum, void *reqbuf);

#endif /* _ALMMMOST_OSLOAD_H */
