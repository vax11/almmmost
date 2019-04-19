/* tvi_sdlc.h: The kernel module to interface with a Zilog Z85C30
 * chip using a Next Thing Co CHIP SBC's GPIO pins, and custom
 * hardware. This sends data to a client using the TeleVideo
 * RS-422 SDLC protocol.
 *
 * Almmmost is a modern replacement for the TeleVideo MmmOST network 
 * operating system used on the TeleVideo TS-8xx Zilog Z80-based computers 
 * from the early 1980s.
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

#ifndef _TVI_SDLC_H
#define _TVI_SDLC_H

#	ifdef __KERNEL__

/* Kernel module load/unload */
static int __init tvi_sdlc_init(void);

static void __exit tvi_sdlc_exit(void);


/* Kernel char device funtions */
static ssize_t tvi_sdlc_read(struct file *fp, char __user *usr_buffer, size_t buffer_s, loff_t *f_offset);

static ssize_t tvi_sdlc_write(struct file *fp, const char __user *usr_buffer, size_t buffer_s, loff_t *f_offset);

static int tvi_sdlc_open(struct inode *ip, struct file *fp);

static int tvi_sdlc_release(struct inode *ip, struct file *fp);

static long tvi_sdlc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);

/* Internal functions to communicate with the z8530 chip */

void tvi_sdlc_busy_wait(void);

/* Functions to control talking to the z8530 */

static int tvi_sdlc_init_gpio(void);

static int tvi_sdlc_reset(int port);

static int tvi_sdlc_init_z8530(int port);

/* static int tvi_sdlc_init_pwm();  -- We'll leave this up to the user to configure */

static int tvi_sdlc_enable422(void);

static int tvi_sdlc_disable422(void);

static int tvi_sdlc_check_int(void);

static int tvi_sdlc_check_wait(void);

static int tvi_sdlc_read_byte(int port, int cd);

static int tvi_sdlc_write_byte(int port, int cd, int outbyte);

/* Functions using the above to read/write the chip registers */
static int tvi_sdlc_write_reg(int port, uint8_t regnum, uint8_t value);

static int tvi_sdlc_read_reg(int port, uint8_t regnum);

static int tvi_sdlc_write_data(int port, uint8_t value);

static int tvi_sdlc_read_data(int port);

/* Functions using the above to talk SDLC to a client */
static int tvi_sdlc_init_tx(int port);

static int tvi_sdlc_init_rx(int port);

static int tvi_sdlc_set_rts(int port);

static int tvi_sdlc_clear_rts(int port);

static int tvi_sdlc_get_cts(int port);

/* Debugging functions */
static int tvi_sdlc_gpio_getpd(int pdport, int value);

static int tvi_sdlc_gpio_setpd(int pdport, int value);

static int tvi_sdlc_gpio_setiodir(int value);

static int tvi_sdlc_gpio_getcfg(int port);

/* Address for things */

//#define GPIO_ADDR (0x01C20800)
#define GPIO_PAGE_ADDR (0x01C20000)
#define GPIO_PAGE_OFFSET (0x800)
#define GPIO_PB (1)
#define GPIO_PC (2)
#define GPIO_PD (3)
#define GPIO_PE (4)
#define GPIO_PF (5)
#define GPIO_PG (6)
#define GPIO_CFG(x,y) ((0x24*(x))/4+GPIO_PAGE_OFFSET/4+(y))
#define GPIO_DAT(x) ((0x24*(x)+0x10)/4+GPIO_PAGE_OFFSET/4)
#define GPIO_PUPD(x,y)  ((0x24*(x)+0x1C)/4+GPIO_PAGE_OFFSET/4+(y))
#define Z8530_WAIT (1<<10)
/* CS - bits 6,5 = 0,1 bits 4-2 = 000 - 111 = chip # */
#define Z8530_CSVAL(port) (0x00000020 | (((port)<<1)& 0x3C) ) // ... 001C BA00
#define Z8530_CSMASK (0xFFFFFF83) // ... 1000 0011
#define Z8530_422EN (1<<11)
#define Z8530_DATA_TO_Z8530 (1<<12)
#define Z8530_INTACT (1<<13)
#define Z8530_RD (1<<14)
#define Z8530_WR (1<<15)
/* AB - port LSB  */
#define Z8530_ABMASK (0xFFFFFFFF ^ (1<<18))
#define Z8530_ABSEL(port) (((~port) & 1)<<18)
#define Z8530_DCSEL (1<<19)
#define Z8530_DAT_SHIFT (20)
#define Z8530_DAT_MASK (0x0FF00000)
#define Z8530_DEFAULT (Z8530_422EN | Z8530_RD | Z8530_WR | Z8530_INTACT | ~(Z8530_CSMASK))
#define Z8530_CTRLLINES (Z8530_RD | Z8530_WR | ~(Z8530_CSMASK) | Z8530_INTACT)
#define Z8530_DAT_IO_MASK2 (0xFFFF0000)
#define Z8530_DAT_IO_MASK3 (0x0000FFFF)
#define Z8530_DAT_OUT_VAL2 (0x11110000)
#define Z8530_DAT_OUT_VAL3 (0x00001111)
#define Z8530_PORTA (1)
#define Z8530_PORTB (0)
#define Z8530_CTRL (0)
#define Z8530_DATA (1)


#	endif /* __KERNEL__ */
/* macros to evaluate registers for certain fields */

#define RXREADY(rr0) (rr0 & 1)
#define TXEMPTY(rr0) (rr0 & 0x4)
#define SYNCHUNT(rr0) (rr0 & 0x10)
#define CTSSET(rr0) (rr0 & 0x20)
#define TXEOM(rr0) (rr0 & 0x40)
#define TXABRT(rr0) (rr0 & 0x80)

#define RXOVERRUN(rr1) (rr1 & 0x20)
#define CRCERROR(rr1) (rr1 & 0x40)
#define RXEOM(rr1) (rr1 & 0x80)

#define TVI_SDLC_SEND_ABORT (0x18)

/* IOCTL commands */

#define TVI_SDLC_IOCTL_DATA(port,val) ((port & 0x0F) | (val << 8))
#define TVI_SDLC_IOCTL_DATA_PORT(x) (x & 0xF)
#define TVI_SDLC_IOCTL_DATA_VAL(x) (x >> 8)


#define TVI_SDLC_PORTA		(1)
#define TVI_SDLC_PORTB		(0)
#define TVI_SDLC_CTRL		(0)
#define TVI_SDLC_DATA		(1)
#define TVI_SDLC_IOCTL_BASE	(0x85300000)
#define TVI_SDLC_IOCTL_ENABLE422	(TVI_SDLC_IOCTL_BASE | 0)
#define TVI_SDLC_IOCTL_DISABLE422	(TVI_SDLC_IOCTL_BASE | 1)
#define TVI_SDLC_IOCTL_SET_RTS	(TVI_SDLC_IOCTL_BASE | 2)
#define TVI_SDLC_IOCTL_GET_CTS	(TVI_SDLC_IOCTL_BASE | 3)
#define TVI_SDLC_IOCTL_RESET	(TVI_SDLC_IOCTL_BASE | 4)
#define TVI_SDLC_IOCTL_INIT	(TVI_SDLC_IOCTL_BASE | 5)
#define TVI_SDLC_IOCTL_INIT_TX	(TVI_SDLC_IOCTL_BASE | 6)
#define TVI_SDLC_IOCTL_INIT_RX	(TVI_SDLC_IOCTL_BASE | 7)
#define TVI_SDLC_IOCTL_GET_RR	(TVI_SDLC_IOCTL_BASE | 8)
#define TVI_SDLC_IOCTL_GET_WR	(TVI_SDLC_IOCTL_BASE | 9)
#define TVI_SDLC_IOCTL_SET_PORT	(TVI_SDLC_IOCTL_BASE | 10)
#define TVI_SDLC_IOCTL_GET_INT	(TVI_SDLC_IOCTL_BASE | 11)
#define TVI_SDLC_IOCTL_SET_RR0	(TVI_SDLC_IOCTL_BASE | 12)
#define TVI_SDLC_IOCTL_SET_PD	(TVI_SDLC_IOCTL_BASE | 32)
#define TVI_SDLC_IOCTL_GET_PD	(TVI_SDLC_IOCTL_BASE | 33)
#define TVI_SDLC_IOCTL_SET_IODIR	(TVI_SDLC_IOCTL_BASE | 34)
#define TVI_SDLC_IOCTL_GET_CFG	(TVI_SDLC_IOCTL_BASE | 35)


#define TVI_SDLC_ERR_BADFP		(-1)
#define TVI_SDLC_ERR_NOCTS		(-2)
#define TVI_SDLC_ERR_ABORT		(-3)
#define TVI_SDLC_ERR_OVERRUN	(-4)
#define TVI_SDLC_ERR_TIMEOUT	(-128)
#define TVI_SDLC_ERR_BADCRC	(-6)
#define TVI_SDLC_ERR_BUFTOOSMALL	(-7)
#define TVI_SDLC_ERR_UNDERRUN	(-8)
#define TVI_SDLC_ERR_CTSLOST	(-9)
#define TVI_SDLC_ERR_BADBUFFER	(-10)
#define TVI_SDLC_NUM_PORTS		(16)

#endif /* _TVI_SDLC_H */
