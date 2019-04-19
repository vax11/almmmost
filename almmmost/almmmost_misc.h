/* almmmost_misc.h: Module to handle misc. client requests for Almmmost.
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

#ifndef _ALMMMOST_MISC_H
#define _ALMMMOST_MISC_H

/* Request to end print spool and send to printer */
int alm_break_spool(int portnum, void *reqbuf);

/* Handle a "Check" request from the client */
int alm_do_check(int portnum, void *reqbuf);

/* Logon request:
 * 8 bytes reqest: 00 43 xx 4c 00 00 00 00 00 00 (xx = drive num)
 * 128 bytes, first 8=password
 * expect 4 byte response, byte 0=successful */
int alm_do_logon(int portnum, void *reqbuf);

#endif /* _ALMMMOST_MISC_H */
