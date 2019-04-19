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
#include <sys/time.h>
#include <time.h>
#include <curl/curl.h>

#include <ini.h>

#include "almmmost.h"
#include "almmmost_image.h"
#include "almmmost_file.h"
#include "almmmost_special.h"
#include "almmmost_device.h"

struct special_file_t *special_files;
static size_t alm_special_fillbuf(void *buf, size_t size, size_t nmemb, void *userp);

char fileinsys_name[INPBUFSIZE];
char fileoutsys_name[INPBUFSIZE];
char imggetsys_url[INPBUFSIZE];
char lynxgetsys_url[INPBUFSIZE];

#define IMAGEFNAME "imgget.sys"
#define IMAGEFNAME_MATCH (6)
#define LYNXFNAME "lynxget.sys"
#define LYNXFNAME_MATCH (7)

int alm_special_init() {

	strcpy(fileinsys_name, "/root/filein.sys");
	strcpy(fileoutsys_name, "/root/fileout.sys");
	strcpy(imggetsys_url, "http://vax11.net/cgi-bin/tvi-image.pl");
	strcpy(lynxgetsys_url, "http://vax11.net/cgi-bin/tvi-lynx.pl");
	special_files = calloc(sizeof(struct special_file_t),1);
	alm_special_add_sft(special_files, "chargen.sys", alm_special_chargen);
	alm_special_add_sft(special_files, "multi.sys", alm_special_multisys);
	alm_special_add_sft(special_files, "filein.sys", alm_special_fileinsys);
	alm_special_add_sft(special_files, "fileout.sys", alm_special_fileoutsys);
	alm_special_add_sft(special_files, "urlget.sys", alm_special_urlget);
	alm_special_add_sft(special_files, IMAGEFNAME, alm_special_cgiget);
	alm_special_add_sft(special_files, LYNXFNAME, alm_special_cgiget);
	return 0;
}

int alm_special_exit() {

	alm_special_free_sft(special_files);
	special_files = NULL;

	return 0;
}

int alm_special_ini(struct INI *ini, const char *buf, size_t buflen) {

	return 0;
}

/* Trap for file open */
int alm_special_trapopen(int fileno, struct cpm_fcb_t *fcb) {

	struct special_file_t *sf;
	struct file_status_t *file = &(fileinfo[fileno]);

	sf = special_files;

	// Default to no trap
	file->trap = NULL;
	// Check if fcb points to a special file, and if so, fill in fileinfo struct.
	do {
		if (alm_same_file(fcb, sf->fname, sf->fext)) {
			// Allocate buffer
			file->special_buf = calloc(RECSIZE, 1);
			if (!file->special_buf)
				break;
			// Allocate space for internal data
			file->trap = calloc(sizeof(struct special_data_t), 1);
			if (!file->trap) {
				free(file->special_buf);
				file->special_buf = NULL;
				break;	// We check for null return below
			}
			// Set callback function pointer
			file->trap->sfp = sf;
			// Handle open callback
			sf->callbk(fileno, TVSP_FILE_OPEN, 0);
			// Save name into fileinfo record
			memcpy(file->fname, sf->fname, 8);
			memcpy(file->fext, sf->fext, 8);
			break;
		}
		sf = sf->next;
	} while (sf != NULL);

	return (file->trap != NULL) ;
}

/* Print list of special files */
int alm_special_printlist() {

	struct special_file_t *sfp = special_files->next;
	char filename[16];

	while (sfp) {

		safe_print("Special file: '");
		get_pretty_filename(filename, sfp->fname, sfp->fext);
		safe_print(filename);
		safe_print("'\n");

		sfp=sfp->next;
	}

	return 0;
}

/* Trap for file close (and reboot) */
/* Can be called from signal handler */
int alm_special_trapclose(int fileno) {

	struct file_status_t *file = &(fileinfo[fileno]);
	
	// Handle file close
	if (file->trap && file->trap->sfp && file->trap->sfp->callbk) {
		file->trap->sfp->callbk(fileno, TVSP_FILE_CLOSE, 0);
		free(file->trap);
		free(file->special_buf);
		file->trap = NULL;
		file->special_buf = NULL;
	}

	return 0;
}

/* Trap for read/write */
int alm_special_trapfileop(int fileno, int fileop, int pos) {
	
	struct file_status_t *file = &(fileinfo[fileno]);

	// Call the appropriate trap
	if (file->trap && file->trap->sfp && file->trap->sfp->callbk) 
		return file->trap->sfp->callbk(fileno,  fileop, pos);

	return 0;
}

/* Free the data structure on exit */
int alm_special_free_sft(struct special_file_t *sf) {

	// Walk the tree, and delete the last entry
	if (sf != NULL) {
		alm_special_free_sft(sf);
		sf->next = NULL;
		free(sf);
	}
	return 0;
}

/* Add an entry to the data structure */
int alm_special_add_sft(struct special_file_t *sf, const char *filename, int (*fp)(int, int, int)) {

	int i,j;

	// Find the last entry
	while (sf->next)
		sf = sf->next;
	// Allocate space and clear it
	sf->next = calloc(sizeof(struct special_file_t),1);
	if (!sf->next)
		return -1;
	sf = sf->next;
	memset(sf->fname, ' ', 8);
	memset(sf->fext, ' ', 3);
	// Copy the filename
	for (i=0; i<8; i++) {
		if (filename[i] == '.' || filename[i] == 0)
			break;
		sf->fname[i] = filename[i] & 0xDF; // convert to uppercase as necessary, perserve bits
	}
	// Skip over the .
	if (filename[i] == '.')
		i++;
	// Copy extension
	for (j=0; j<3; j++) {
		if (filename[i] == 0)
			break;
		sf->fext[j] = filename[i+j] & 0xDF;
	}
	// Set the callback function
	sf->callbk = fp;

	return 0;
	
}

/* chargen.sys */
#define CHARGEN_RECS (17)
int alm_special_chargen(int fileno, int fop, int pos) {

	int i;

	struct file_status_t *file = &(fileinfo[fileno]);

	if (fop == TVSP_FILE_OPEN) {
		// Don't buffer anything 
	} else if (FOP_IS_READ(fop)) {
		// Generate the record

		// Return if past EOF
		if (pos > CHARGEN_RECS)
			return -1;
		memset(file->special_buf, 0x20, RECSIZE-2);
		snprintf((char *)file->special_buf, RECSIZE-2, "Record %03x ", pos);
		for (i=32; i<127; i++)
			file->special_buf[i-32+11] = i;
		file->special_buf[126] = 0xd;
		file->special_buf[127] = 0xa;
		
	} else if (fop == TVSP_FILE_CLOSE) {
		/* Can be called from signal handler */
		// Don't do anything here
	}
		

	return 0;
}

/* mutli.sys */
#define MULTISYS_RECS (7)
int alm_special_multisys(int fileno, int fop, int pos) {

	int i;
	struct timeval curtime;
	struct tm *t;


	struct file_status_t *file = &(fileinfo[fileno]);

	if (fop == TVSP_FILE_OPEN) {
		// Don't buffer anything
	} else if (fop == TVSP_FILE_CLOSE) {
		// Don't do anything here
	} else if (FOP_IS_READ(fop)) {
		if (pos >= MULTISYS_RECS)
			return -1;
		if (!file->special_buf)
			return -1;
		memset(file->special_buf, ' ', RECSIZE-2);
		file->special_buf[126] = 0x0d;
		file->special_buf[127] = 0x0a;
		switch (pos) {
			case 0:
				i = snprintf((char *)file->special_buf, RECSIZE-2, "%-13s%02d %-19s%03d %-8s%d",
						"USERID:", file->port,
						"MESSAGE COUNT:", 0,
						"STATUS:", 0);
				// Clean up null pointer
				file->special_buf[ (i>(RECSIZE-2)) ? RECSIZE-2 : i] = ' ';
				break;

			case 6:
				gettimeofday(&curtime, NULL);
				t = localtime(&curtime.tv_sec);
				i=snprintf((char *)file->special_buf, RECSIZE-2, "%02d/%02d/%04d %02d:%02d:%02d.%02d", 
						t->tm_mon+1, t->tm_mday, t->tm_year + 1900,
						t->tm_hour, t->tm_min, t->tm_sec, (int)curtime.tv_usec / 10000);
				file->special_buf[ (i>(RECSIZE-2)) ? RECSIZE-2 : i] = ' ';
				break;

			default:
				break;
		}

		
	}
	return 0;

}

/* filein.sys */
int alm_special_fileinsys(int fileno, int fop, int pos) {
	int i, retval;
	int fd;
	int fsize;
	int nrecs;

	struct file_status_t *file = &(fileinfo[fileno]);

	if (fop == TVSP_FILE_OPEN) {
		// Open file, get size
		fd = open(fileinsys_name, O_RDONLY);
		if (fd < 0)
			return 0;
		fsize = lseek(fd, 0, SEEK_END);
		if (fsize > CPMMAXSIZE)
			fsize = CPMMAXSIZE;
		lseek(fd, 0, SEEK_SET);
		// Allocate buffer to next RECSIZE size
		nrecs = ((fsize - 1)/RECSIZE + 1);
		file->trap->readbuf = malloc(nrecs*RECSIZE);
		if (!file->trap->readbuf)
			return 0;
		memset(file->trap->readbuf, 0x1A, nrecs * RECSIZE);
		for (i=0; i<nrecs; i++) {
			retval = read(fd, file->trap->readbuf + (i*RECSIZE), RECSIZE);
			if (retval < 1) {
				// Nothing read, don't increment i
				break;
			} else if (retval < RECSIZE) {
				// Short read, increment i to count this
				i++;
				break;
			}
		}
		close(fd);
		// Set size based on records read
		file->trap->readbufsize = i;
	} else if (fop == TVSP_FILE_CLOSE) {
		if (file->trap->readbuf)
			free(file->trap->readbuf);
		file->trap->readbuf = NULL;
		file->trap->readbufsize = 0;
		if (file->trap->writebuf)
			free(file->trap->writebuf);
		file->trap->writebuf = NULL;
		file->trap->writebufsize = 0;
	} else if (FOP_IS_READ(fop)) {
		// Check for null pointers
		if (!file->trap || !file->trap->readbuf || !file->special_buf)
			return -1;
		if (pos >= file->trap->readbufsize)
			return -1;
		if (pos > file->trap->readbufmax)
			file->trap->readbufmax = pos;
		memcpy(file->special_buf, file->trap->readbuf + (pos * RECSIZE), RECSIZE);
	}
	return 0;

}

/* fileout.sys */
int alm_special_fileoutsys(int fileno, int fop, int pos) {
	int fd;

	struct file_status_t *file = &(fileinfo[fileno]);

	if (fop == TVSP_FILE_OPEN) {
		// Allocate inital buffer for writing
		file->trap->writebuf = malloc(1);
		if (!file->trap->writebuf)
			return -1;
	} else if (fop == TVSP_FILE_CLOSE) {
		/* Can be called from signal handler */
		if (file->trap->writebufsize < 1) 
			return 0; 	// Nothing to write to file
		// Open file
		fd = open(fileoutsys_name, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
			return 0;
		// Write contents & close
		write(fd, file->trap->writebuf, file->trap->writebufsize * RECSIZE);
		close(fd);
		// free buffers
		if (file->trap->readbuf)
			free(file->trap->readbuf);
		file->trap->readbuf = NULL;
		file->trap->readbufsize = 0;
		if (file->trap->writebuf)
			free(file->trap->writebuf);
		file->trap->writebuf = NULL;
		file->trap->writebufsize = 0;
	} else if (FOP_IS_WRITE(fop)) {

		// find new size, realloc if bigger than old size
		int newsize = pos+1;

		if (newsize > file->trap->writebufsize) {
			uint8_t *newptr = realloc(file->trap->writebuf, newsize * RECSIZE);
			if (!newptr)
				return -1;
			file->trap->writebuf = newptr;
			file->trap->writebufsize = newsize;
		}
		// Copy data to buffer
		memcpy(file->trap->writebuf + (RECSIZE*pos), file->special_buf, RECSIZE);

	} else if (FOP_IS_READ(fop)) {
		// Check for null pointers
		if (!file->trap || !file->trap->readbuf || !file->special_buf)
			return -1;
		if (pos >= file->trap->writebufsize)
			return -1;
		if (pos > file->trap->writebufmax)
			file->trap->readbufmax = pos;
		// Read what we wrote to the file
		memcpy(file->special_buf, file->trap->writebuf + (pos * RECSIZE), RECSIZE);
	}
	return 0;

}

// Callback for libcurl to write data recieved to buffer
static size_t alm_special_fillbuf(void *buf, size_t size, size_t nmemb, void *userp) {
	size_t bytes=size*nmemb, newsize, rounduprecs;
	struct special_data_t *sdp = (struct special_data_t *)userp;

	// Fail if we've read more than CP/M can handle
	if (sdp->readbufsize >= 65536)
		return 0;
	newsize = sdp->tmp + bytes;

	// Allocate full records so it's easier to read later
	rounduprecs = (newsize - 1)/RECSIZE + 1;
	if (rounduprecs > sdp->readbufsize) {
		uint8_t *rdbufnew = realloc(sdp->readbuf, rounduprecs * RECSIZE);
		if (!rdbufnew) {
			printf("Failed realloc in alm_special_fillbuf!\n");
			return 0;
		}
		sdp->readbuf = rdbufnew;
	}
	memcpy(sdp->readbuf + sdp->tmp, buf, bytes);
	sdp->tmp = newsize;
	sdp->readbufsize = sdp->readbufmax = (newsize-1)/RECSIZE + 1;
	
	return bytes;
}

/* urlget.sys */
int alm_special_urlget(int fileno, int fop, int pos) {

	struct file_status_t *file = &(fileinfo[fileno]);

	if (fop == TVSP_FILE_OPEN) {
		file->trap->writebuf = malloc(1);
		if (!file->trap->writebuf)
			return -1;
		file->trap->readbuf = malloc(1);
		if (!file->trap->readbuf)
			return -1;
	} else if (fop == TVSP_FILE_CLOSE) {
		/* Can be called from signal handler */
		if (file->trap->readbuf)
			free(file->trap->readbuf);
		file->trap->readbuf = NULL;
		file->trap->readbufsize = 0;
		if (file->trap->writebuf)
			free(file->trap->writebuf);
		file->trap->writebuf = NULL;
		file->trap->writebufsize = 0;
	} else if (FOP_IS_WRITE(fop)) {
		// find new size, realloc if bigger than old size
		int newsize = pos+1;
		uint8_t *eofpos;

		if (file->trap->tmp > 0)		// If we've already done got a file, don't do it again.
			return -1;

		if (newsize > file->trap->writebufsize) {
			uint8_t *newptr = realloc(file->trap->writebuf, newsize * RECSIZE);
			if (!newptr)
				return -1;
			file->trap->writebuf = newptr;
			file->trap->writebufsize = newsize;
		}
		// Copy data to buffer
		memcpy(file->trap->writebuf + (RECSIZE*pos), file->special_buf, RECSIZE);

		// find ^Z, exit if not found
		eofpos = memchr(file->trap->writebuf, 0x1a, file->trap->writebufsize * RECSIZE);
		// turn into a ASCIIZ string, and pass to libcurl
		if (eofpos) {
			CURL *curlp;

			// Init libcurl
			curlp = curl_easy_init();
			if (!curlp)
				return -1;
			// null terminate instead of ^Z
			*eofpos = 0;
			// Set up and do read
			curl_easy_setopt(curlp, CURLOPT_URL, file->trap->writebuf);
			curl_easy_setopt(curlp, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
			curl_easy_setopt(curlp, CURLOPT_WRITEFUNCTION, alm_special_fillbuf);
			curl_easy_setopt(curlp, CURLOPT_WRITEDATA, (void *)file->trap);
			curl_easy_setopt(curlp, CURLOPT_USERAGENT, "almmmost/0.1 (CP/M; 2.2)");
			curl_easy_perform(curlp);
			curl_easy_cleanup(curlp);
			// If not a full last record, fill remainder with EOF ^Z's
			if (file->trap->tmp % RECSIZE)
				memset(file->trap->readbuf + file->trap->tmp, 0x1a, RECSIZE - (file->trap->tmp % RECSIZE));

		}
		
	} else if (FOP_IS_READ(fop)) {
		// Check for null pointers
		if (!file->trap || !file->trap->readbuf || !file->special_buf || !file->trap->tmp)
			return -1;
		if (pos >= file->trap->readbufsize)
			return -1;
		if (pos > file->trap->readbufmax)
			file->trap->readbufmax = pos;
		// If we're good, read the record
		memcpy(file->special_buf, file->trap->readbuf + (pos * RECSIZE), RECSIZE);
	}
	return 0;
}

/* imgget.sys / lynxget.sys */
int alm_special_cgiget(int fileno, int fop, int pos) {

	struct file_status_t *file = &(fileinfo[fileno]);

	if (fop == TVSP_FILE_OPEN) {
		file->trap->writebuf = malloc(1);
		if (!file->trap->writebuf)
			return -1;
		file->trap->readbuf = malloc(1);
		if (!file->trap->readbuf)
			return -1;
	} else if (fop == TVSP_FILE_CLOSE) {
		/* Can be called from signal handler */
		if (file->trap->readbuf)
			free(file->trap->readbuf);
		file->trap->readbuf = NULL;
		file->trap->readbufsize = 0;
		if (file->trap->writebuf)
			free(file->trap->writebuf);
		file->trap->writebuf = NULL;
		file->trap->writebufsize = 0;
	} else if (FOP_IS_WRITE(fop)) {
		// find new size, realloc if bigger than old size
		int newsize = pos+1;
		uint8_t *eofpos;

		if (file->trap->tmp > 0)		// If we've already done got a file, don't do it again.
			return -1;

		if (newsize > file->trap->writebufsize) {
			uint8_t *newptr = realloc(file->trap->writebuf, newsize * RECSIZE);
			if (!newptr)
				return -1;
			file->trap->writebuf = newptr;
			file->trap->writebufsize = newsize;
		}
		// Copy data to buffer
		memcpy(file->trap->writebuf + (RECSIZE*pos), file->special_buf, RECSIZE);

		// find ^Z, exit if not found
		eofpos = memchr(file->trap->writebuf, 0x1a, file->trap->writebufsize * RECSIZE);
		// turn into a ASCIIZ string, and pass to libcurl
		if (eofpos) {
			CURL *curlp;
			char postoption[INPBUFSIZE];
			char *urlenc_data;
			char *url;

			// URL determined by what special file we're using
			if (!(strncasecmp((char *)file->trap->sfp->fname, IMAGEFNAME, IMAGEFNAME_MATCH)))
				url = imggetsys_url;
			else if (!(strncasecmp((char *)file->trap->sfp->fname, LYNXFNAME, LYNXFNAME_MATCH)))
				url = lynxgetsys_url;
			else {
				printf("alm_special_cgigetsys: I don't know what handler I'm for: ");
				print_cpm_filename(file->trap->sfp->fname, file->trap->sfp->fext);
				return -1;
			}
			// Init libcurl
			curlp = curl_easy_init();
			if (!curlp)
				return -1;
			// null terminate instead of ^Z, and url encode
			*eofpos = 0;
			urlenc_data = curl_easy_escape(curlp, (char *)file->trap->writebuf, 0);
			
			// Set up and do read
			strcpy(postoption, "url=");
			strncat(postoption, urlenc_data, INPBUFSIZE);
			postoption[INPBUFSIZE-1] = 0;
			curl_free(urlenc_data);
				
			curl_easy_setopt(curlp, CURLOPT_URL, url);
			curl_easy_setopt(curlp, CURLOPT_POST, 1L);
			curl_easy_setopt(curlp, CURLOPT_POSTFIELDSIZE, strlen(postoption));
			curl_easy_setopt(curlp, CURLOPT_POSTFIELDS, postoption);
			curl_easy_setopt(curlp, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
			curl_easy_setopt(curlp, CURLOPT_WRITEFUNCTION, alm_special_fillbuf);
			curl_easy_setopt(curlp, CURLOPT_WRITEDATA, (void *)file->trap);
			curl_easy_setopt(curlp, CURLOPT_USERAGENT, "almmmost/0.1 (CP/M; 2.2)");
			curl_easy_perform(curlp);
			curl_easy_cleanup(curlp);
			// If not a full last record, fill remainder with EOF ^Z's
			if (file->trap->tmp % RECSIZE)
				memset(file->trap->readbuf + file->trap->tmp, 0x1a, RECSIZE - (file->trap->tmp % RECSIZE));

		}
		
	} else if (FOP_IS_READ(fop)) {
		// Check for null pointers
		if (!file->trap || !file->trap->readbuf || !file->special_buf || !file->trap->tmp)
			return -1;
		if (pos >= file->trap->readbufsize)
			return -1;
		if (pos > file->trap->readbufmax)
			file->trap->readbufmax = pos;
		// If we're good, read the record
		memcpy(file->special_buf, file->trap->readbuf + (pos * RECSIZE), RECSIZE);
	}
	return 0;
}

