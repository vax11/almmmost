/* pbm2bin.c: Convert a binary pbm file to data formatted properly for 
 * display by IMAGEGET.ZASM on a TeleVideo TS-803 or TPC-I via Almmmost.
 * This converts the data to a 640x240 b&w bit stream, with 4 interlaced
 * fields.
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

#define RECSPERFIELD (38)
#define RECSIZE (128)
#define BYTESPERLINE (640/8)

#define LINE_OFF(row,field) ((row/4)*(BYTESPERLINE) + (field)*RECSPERFIELD*RECSIZE)

void binary_pbm() {

	/* Assume line 2 has width/height */
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

	int row;
	char *rowbuf;
	char *imgbuf;
	int cwidth = (width+7)/8;

	imgbuf = alloca(RECSPERFIELD*RECSIZE*4);
	rowbuf = alloca(cwidth);

	if (!imgbuf || !rowbuf) {
		fprintf(stderr, "Could not allocate memory.\n");
		return;
	}

	memset(imgbuf, 0, RECSPERFIELD*RECSIZE*4);

	fprintf(stderr, "Width = %d, chars = %d, height = %d\n", width, cwidth, height);

	if (height > 240)
		height = 240;

	for (row = 0; row < height; row+=4) {
		/* Debugging */
		/* fprintf(stderr, "row %d: offset F1: %04x, F2: %04x, F3: %04x, F4: %04x\n", row, LINE_OFF(row,0), LINE_OFF(row,1), LINE_OFF(row,2), LINE_OFF(row,3)); */
		fread(rowbuf, cwidth, 1, stdin);
		memcpy(imgbuf + LINE_OFF(row,0), rowbuf, BYTESPERLINE);
		if (fread(rowbuf, cwidth, 1, stdin))
			memcpy(imgbuf + LINE_OFF(row,1), rowbuf, BYTESPERLINE);
		else
			break;
		if (fread(rowbuf, cwidth, 1, stdin))
			memcpy(imgbuf + LINE_OFF(row,2), rowbuf, BYTESPERLINE);
		else
			break;
		if (fread(rowbuf, cwidth, 1, stdin))
			memcpy(imgbuf + LINE_OFF(row,3), rowbuf, BYTESPERLINE);
		else
			break;
	}

	/* Write the whole buffer at once */
	fwrite(imgbuf, RECSPERFIELD*RECSIZE*4, 1, stdout);

	return;

}


int main (int argc, char **argv) {


	if (!fgets(buffer, BUFLEN, stdin) || strncmp("P4", buffer, 2)) {
		fprintf(stderr, "Not PBM file\n");
		return 1;
	}

	binary_pbm();
	return 0;
}
