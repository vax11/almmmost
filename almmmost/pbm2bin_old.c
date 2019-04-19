/* pbm2bin_old.c: Convert a binary pbm file to data formatted properly for 
 * display by IMAGEGET.ZASM on a TeleVideo TS-803 or TPC-I via Almmmost.
 * This converts the data to a 640x240 b&w bit stream using an old
 * format, and is here as an example
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
#include <stdlib.h>
#include <string.h>

#define BUFLEN (1024)
char buffer[BUFLEN];
int width=0;
int height=0;

void binary_pbm() {


	// Assume line 2 has width/height
	do {
		char *nextptr=0;
		if (!fgets(buffer, BUFLEN, stdin)) {
			fprintf(stderr, "Failed to read width/height");
			return;
		}
		int tmp = strtoul(buffer, &nextptr, 10);
		if (tmp > 0) {
			if (width<1) {
				width = tmp;
				tmp = strtoul(nextptr, NULL, 10);
				if (tmp > 0) 
					height = tmp;
			} else {
				height = tmp;
			}
		}


	} while (height == 0);

	int row, i;
	char *rowbuf[1024/8];
	int cwidth = (width+7)/8;
	memset(rowbuf, 0, (1024)/8);

	fprintf(stderr, "Width = %d, chars = %d, height = %d\n", width, cwidth, height);

	if (width > 1024) {
		fprintf(stderr, "Image too wide: %d\n", width);
		return;
	}

	for (row = 0; row < height; row++) {
		fread(rowbuf, cwidth, 1, stdin);
		fwrite(rowbuf, (1024/8), 1, stdout);
	}
	if (row % 4) {
		memset(rowbuf, 0, (1024)/8);
		for (i=0; i<(4-(row%4)); i++)
			fwrite(rowbuf, (1024/8), 1, stdout);
	}

	return;

}


int main (int argc, char **argv) {

	int tmp;
	int i=0, j=0, bits=0;

	

	if (!fgets(buffer, BUFLEN, stdin) || strncmp("P1", buffer, 2)) {
		if (!strncmp("P4", buffer,2)) {
			binary_pbm();
			return 0;
		}
		fprintf(stderr, "Not PBM file\n");
		return 1;
	}

	do {
		
		if (!fgets(buffer, BUFLEN, stdin)) {
			fprintf(stderr, "EOF before data.\n");
			return 2;
		}
		
		tmp = strtoul(buffer, NULL, 0);
		if (tmp > 0) {
			if (width == 0)
				width = tmp;
			else
				height = tmp;
		}

	} while (width == 0 && height == 0);
	

	while (fgets(buffer, BUFLEN, stdin)) {

		
		if (!buffer[0])
			break;						// EOF
		if (buffer[0] != '0' && buffer[0] != '1')
			continue;					// Random junk

		for (i=0; i<BUFLEN; i++) {
			if (buffer[i] == '0') {
				bits |= 1 << (7-j);			// 0 = white
				j++;
			} else if (buffer[i] == '1') {
				j++;					// 1 = black
			} else if (!buffer[i]) {
				break;					// end of string
			}
			if (j > 7) {
				putc(bits, stdout);
				j=0;
				bits=0;
			}
		
		}
	}
	
	return 0;
}
