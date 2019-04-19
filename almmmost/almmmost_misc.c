/* almmmost_misc.c: Module to handle misc. client requests for Almmmost.
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

#include <ini.h>

#include "almmmost.h"
#include "almmmost_misc.h"
#include "almmmost_device.h"
#include "almmmost_image.h"
#include "almmmost_file.h"


int alm_break_spool(int portnum, void *reqbuf) {

	// FIXME maybe do something?
	return 0;

}

int alm_do_check(int portnum, void *reqbuf) {

	struct tvsp_ipc_response ipc_resp;
	struct tvsp_check_request *chkbuf = reqbuf;
	uint8_t databuf[TVSP_DATA_SZ];

	int retval;

	ipc_resp.retcode = 0;
	ipc_resp.x = 0;
	ipc_resp.errcode = 0;
	ipc_resp.err = 0;

	if (chkbuf->subreq != TVSP_CHECK_HIJACK) {
		//printf("(CHECK) waiting for CTS...\n");
		do {
			retval = alm_dev_check_cts(portnum);
			if (alm_do_locate || alm_do_abort) {
				printf("locate/abort: alm_do_check(%d,buf):\n", portnum);
				printf("  request %c, drive %c\n", 
						chkbuf->subreq, chkbuf->drv + 'A');
				alm_do_locate = 0;
			}
			if (alm_do_abort) {
				ipc_resp.err = 1;
				ipc_resp.errcode = ERR_BIOS_WRITE;
				alm_do_abort = 0;
				goto docheck_exit;
			}

		} while (!retval);
		retval = alm_dev_read(databuf, TVSP_DATA_SZ, portnum);
		if (retval < 1) {
			printf("Error reading data buffer: %d\n", errno);
		}
	}

	switch (chkbuf->subreq) {
		case TVSP_CHECK_SPOOLDRV:
			ipc_resp.retcode = mmm_spooldrv;
			//printf("Check spool drive: %c\n", ipc_resp.retcode+'A');
			break;

		case TVSP_CHECK_AUTOLDPROC:
			ipc_resp.retcode = (userinfo[portnum].autologon << 6 ) | (portnum & 0xF);
			//printf("Check proc id/autolog: %02x\n", ipc_resp.retcode);
			break;

		case TVSP_CHECK_GENREV:
			ipc_resp.retcode = mmm_genrev;
			//printf("Check MmmOST GENREV: %d\n", ipc_resp.retcode);
			break;

		case TVSP_CHECK_HIJACK:
			//printf("Hijack request drive %c\n", chkbuf->drv + 'A');
			//userinfo[portnum].defdrive = chkbuf->drv;

			// Re-write extents
			alm_file_sync();
			//alm_file_loadbam(chkbuf->drv);
			//print_hex((uint8_t *)chkbuf, TVSP_REQ_SZ);
			//ipc_resp.retcode = 1;
			break;

		default:
			ipc_resp.err = 0xff;
			printf("Unknown request ");
			print_hex(reqbuf, TVSP_REQ_SZ);
			break;
	}

docheck_exit:
	usleep(WRITEDELAY);
	retval = alm_dev_write(&ipc_resp, TVSP_RESP_SZ, portnum);
	if (retval < 1) {
		printf("Error sending response: %d\n", errno);
	}
	
	return 0;
}

int alm_do_logon(int portnum, void *reqbuf){ 

	uint8_t *rb = reqbuf;
	int drive = rb[2];
	int i;
	int retval;

	uint8_t databuf[RECSIZE];
	struct tvsp_ipc_response ipc_resp;

	ipc_resp.err = 0;
	ipc_resp.errcode = 0;
	ipc_resp.retcode = 1;

	while (!alm_dev_check_cts(portnum)) {
		if (alm_do_locate || alm_do_abort) {
			printf("locate/abort: alm_do_logon(%d,buf):\n", portnum);
			alm_do_locate = 0;
		}
		if (alm_do_abort) {
			ipc_resp.err = 1;
			ipc_resp.errcode = ERR_BIOS_WRITE;
			alm_do_abort = 0;
			goto adl_exit;
		}
	
	}
	retval = alm_dev_read(databuf, RECSIZE, portnum);
	//printf("Part 2, %d bytes: ", retval);
	if (retval > 0) {
		char passwdtxt[9];
		// Check drive #
		if (drive > mmm_numdisks || drvparam[drive].public_private != PRIVDIR) {
			goto adl_exit;
		}

		memcpy(passwdtxt, databuf, 8);
		for (i=0; i<8; i++)
			if (databuf[i] == ' ')
				break;
		passwdtxt[i] = 0;
	
		if (strncasecmp(passwdtxt,"DIR",3)) {
			goto adl_exit;
		}
		int dest = strtol(passwdtxt+3,NULL,0);
		if (dest > mmm_maxdirs) {
			goto adl_exit;
		}
		if (drvparam[drive].image_fd[dest] < 0) {
			goto adl_exit;
		}
		for (i=0; i<alm_dev_ports; i++) {
			if (i != portnum && userinfo[i].drive_dir[drive] == dest)
				goto adl_exit;
		}
		// If we get here, we should be good to go, I think.
		userinfo[portnum].drive_dir[drive] = dest;
		ipc_resp.retcode = 0;
			
	}
adl_exit:
	alm_dev_write(&ipc_resp, TVSP_RESP_SZ, portnum);

	return 0;	
}

