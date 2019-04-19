/* almmmost_special.c: Special file handling module for Almmmost.
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

#ifndef _ALMMMOST_SPECIAL_H
#define _ALMMMOST_SPECIAL_H

struct special_file_t {
	uint8_t fname[8];
	uint8_t fext[3];
	uint8_t is_ro;
	int (*callbk)(int fileno, int fop, int pos); // Pointer to function called when client does something to fileno
	struct special_file_t *next;
};

struct special_data_t {
	uint8_t *readbuf;
	int readbufsize;
	int readbufmax;
	uint8_t *writebuf;
	int writebufsize;
	int writebufmax;
	struct special_file_t *sfp;
	size_t tmp;
};

extern struct special_file_t *special_files;
extern char fileinsys_name[], fileoutsys_name[];
extern char imggetsys_url[];
extern char lynxgetsys_url[];

#define FOP_IS_READ(fop) ((fop == TVSP_FILE_READSEQ) || (fop == TVSP_FILE_READRAND))
#define FOP_IS_WRITE(fop) ((fop == TVSP_FILE_WRITESEQ) || (fop == TVSP_FILE_WRITERAND) || (fop == TVSP_FILE_WRITERANDZ))

/* Public functions */
int alm_special_init();
int alm_special_exit();
int alm_special_ini(struct INI *ini, const char *buf, size_t buflen);

/* Trap for file open */
int alm_special_trapopen(int fileno, struct cpm_fcb_t *fcb);

/* Trap for file close (and reboot) */
int alm_special_trapclose(int fileno);

/* Trap for read/write */
int alm_special_trapfileop(int fileno, int fileop, int pos);

/* Print list of special files */
int alm_special_printlist();

/* Private functions */
int alm_special_free_sft(struct special_file_t *sf);
int alm_special_add_sft(struct special_file_t *sf, const char *filename, int (*fp)(int, int, int));

/* chargen.sys */
int alm_special_chargen(int fileno, int fop, int pos);
/* mutli.sys */
int alm_special_multisys(int fileno, int fop, int pos);
/* filein.sys */
int alm_special_fileinsys(int fileno, int fop, int pos);
/* fileout.sys */
int alm_special_fileoutsys(int fileno, int fop, int pos);
/* urlget.sys */
int alm_special_urlget(int fileno, int fop, int pos);
/* imgget.sys / lynxget.sys */
int alm_special_cgiget(int fileno, int fop, int pos);


#endif /* _ALMMMOST_SPECIAL_H */
