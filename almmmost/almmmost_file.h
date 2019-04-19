/* almmmost_file.h: The shared file I/O interface module for Almmmost.
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

#ifndef _ALMMMOST_FILE_H
#define _ALMMMOST_FILE_H

/* Functions we intercept: */

#define TVSP_FILE_OPEN (15)
#define TVSP_FILE_CLOSE (16)
#define TVSP_FILE_SEARCH1ST (17)
#define TVSP_FILE_DELETE (19)
#define TVSP_FILE_READSEQ (20)
#define TVSP_FILE_WRITESEQ (21)
#define TVSP_FILE_MAKE (22)
#define TVSP_FILE_RENAME (23)
#define TVSP_FILE_SETATTR (30)
#define TVSP_FILE_READRAND (33)
#define TVSP_FILE_WRITERAND (34)
#define TVSP_FILE_GETSIZE (35)
#define TVSP_FILE_SETRANDREC (36)
#define TVSP_FILE_WRITERANDZ (40)

#define LOG_EXT(ext,exm) (ext & exm)
#define PHY_EXT(s2,ext,exm) (((s2 * 32) + (ext & 0x1F)) / (exm + 1))
#define FULL_EXT(s2,ext) ((s2 * 32) + (ext & 0x1F))
#define EXT_EXTL(recs) ((recs>>7) & 0x1F)
#define EXT_S2(recs) (recs>>12)

/* MMMOST 2.0 error messages:
 *
 * RETBUF: FILENO (2)
 * 	   RTNCODE (1) -- Return code from bdos
 * 	   PRNTRTN (1) -- Print control retn byte (Error to print)
 *
 * PRNTRTN - 7: 1 if print 2nd line, 6-4: M11TAB, 3-0: M3TAB
 *
 * M11TAB:
 * 1,2: Bad Sector
 * 3: File/Drive R/O (Causes system warm boot)
 * 4: Select
 *
 * M3TAB:
 * 1: Command fault error
 * 2: Write Protect
 * 3: Illegal Function call
 * 4: File status wrong for function
 * 5: Drive type error
 * 6: Xfr out err
 * 7: Xfr in err
 * 8: Genrev number bad
 * 9: Out of space
 */

#define MMMERR_OK (0)
#define MMMERR_BADSECT (0x90)
#define MMMERR_RO (0x98)
#define MMMERR_SELECT (0xC0)
#define MMMERR_CMDFAULT (1)
#define MMMERR_WRTPROT (2)
#define MMMERR_ILLCALL (3)
#define MMMERR_BADFILE (4)
#define MMMERR_DRVTYPE (5)
#define MMMERR_XFROUT (6)
#define MMMERR_XFRIN (7)
#define MMMERR_GENREV (8)
#define MMMERR_NOSPACE (9)

#define RETCODE_OK (0)
#define RETCODE_UNWRITTEN_DATA (1)
#define RETCODE_UNWRITTEN_EXTENT (4)
#define RETCODE_DIRFULL (5)
#define RETCODE_PAST_ENDOFDISK (6)
#define RETCODE_MISCERR (0xFF)

struct ext_ll_t {
	int denum;
	struct ext_ll_t *next;
	uint16_t blocks[16];
	uint16_t extsize;
};

struct special_file_t;
struct special_data_t;

struct file_status_t {
	uint8_t used;
	uint8_t drivenum;
	uint8_t usrcode;
	uint8_t port;
	uint8_t fname[8];
	uint8_t fext[3];
	uint8_t is_ro;
	int size; // in records
	struct ext_ll_t *extent;
	// Values used for special files
	struct special_data_t *trap;
	uint8_t *special_buf;
};

extern struct file_status_t *fileinfo;
	

int alm_file_init();
int alm_file_exit();
int alm_file_ini(struct INI *ini, const char *buf, size_t buflen);
int alm_do_fileop(int portnum, void *reqbuf);

/* Functions corresponding to BDOS commands */
/* Open / Make, BDOS 15, 22 */
int alm_file_doopen(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);

/* Set Random Record, BDOS 36 */
int alm_file_dosetrandpos(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);

/* Close, BDOS 16 */
int alm_file_doclose(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);

/* Read seq / Read rand, BDOS 20, 33 */
int alm_file_doread(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp, uint8_t *readbuf);

/* Write seq / Write rand / Write rand zero block, BDOS 21, 34, 40 */
int alm_file_dowrite(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp, uint8_t *writebuf);

/* Search for First, BDOS 17 -- note drive = ? means search for any user #, including e5's */
int alm_file_dosearch(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);

/* Delete file BDOS 19 / Rename file BDOS 23 / Set attributes BDOS 30 */
int alm_file_domoddir(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);

/* Get file size (set rrec to EOF), BDOS 35 */
int alm_file_dogetsize(int portnum, struct tvsp_file_request *freq, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);

/* Close all files opened by portnum */
int alm_file_clearfiles(int portnum);

/* Save all file state */
int alm_file_sync();

/* Print all open files */
int alm_file_printopen();

/* Close all open files on a particular disk */
int alm_file_closeallondisk(int disk);

/* Internal functions */
int alm_file_loadbam(int disk);
int alm_file_allocblk(int disk, int dentry);
int alm_file_deallocblk(int disk, uint16_t block);
int alm_file_finddentry(int port, int disk, int usrcode, struct cpm_fcb_t *fcb);
int alm_file_closeentry(int disk, int fnum);
int alm_alloc_dentry(int disk, int usrcode, int fnum, struct cpm_fcb_t *fcb);
int alm_file_rewrite_extents(int fnum);
int alm_same_file(const struct cpm_fcb_t *fcb, const uint8_t *fname, const uint8_t *fext);
int alm_modify_dir(int fileop, int disk, int uc, struct cpm_fcb_t *fcb, struct tvsp_file_response *resp);
int alm_file_modify_thisde(int fileop, int disk, struct cpm_direntry_t *de, struct cpm_fcb_rename_t *fcbren);
int alm_file_blks2fcb(int disk, struct ext_ll_t *ext, struct cpm_fcb_t *fcb);
int alm_file_getfnum(int freq_fnum, const struct cpm_fcb_t *fcb, int portnum, int disk, int uc);

#endif /* _ALMMMOST_FILE_H */
