/* tvi_sdlc.c: The kernel module to interface with a Zilog Z85C30
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/preempt.h>
#include <linux/irqflags.h>

#include "tvi_sdlc.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Finnegan");
MODULE_DESCRIPTION("Linux device driver for Zilog 85C30 interface on NTC C.H.I.P.");
MODULE_VERSION("0.1");

#define DEVICE_NAME "tvisdlc"
#define CLASS_NAME "tvi_sdlc"

#define BUFF_SZ (4096)

static int tvi_sdlc_dev_major;
static char tvi_sdlc_data_buffer[BUFF_SZ] = {0};
static struct class *tvi_sdlc_class = NULL;
static struct device *tvi_sdlc_device = NULL;
static uint32_t __iomem *tvi_sdlc_gpio_pg = NULL;
static int tvi_sdlc_open_count = 0;

#define ABNUM(port)	(port & 1)
#define CSNUM(port)	(port >> 1)
#define REGMASK 	(0xF)

#define CTS_TIMEOUT 	 (1000000)
#define CHAR_TIMEOUT 	   (10000)
#define FCHAR_TIMEOUT 	   (50000)

/* Cache recieve register values for speed */
static int tvi_sdlc_rr[REGMASK+1][TVI_SDLC_NUM_PORTS];
/* Cache write register values so we know what it is */
static int tvi_sdlc_wr[REGMASK+1][TVI_SDLC_NUM_PORTS];

/* constants for initialization */

#define TXTAB_NUM (6)
static int tvi_sdlc_txtab[TXTAB_NUM][2] = 
	{{ 6, 0xFF}, 		// Set global address
	{7, 0x7e}, 		// Set flag char
	{3, 0xd9}, 		// 8b receive, enable hunt, rx crc en, rx en
	{5, 0x6b}, 		// 8b tx, tx en, SDLC CRC, tx crc en, RTS on
	{1, 0x80}, 		// Enable -wait on tx buffer empty, disable interrupts
	{0, 0x80}};		// Reset CRC
#define RXTAB_NUM (4)
static int tvi_sdlc_rxtab[RXTAB_NUM][2] = 
	{{ 7, 0x7E}, 		// Set flag char
	{1, 0xa0}, 		// Enable -wait, on recieve buffer
	{0x75, 0x6b}, 		// Reset error & RX CRC, set 8b tx, sdlc crc, tx crc enable, RTS on
	{3, 0xd9}};		// 8b recv, enable hunt, RX crc en, rx en
#define INITTAB_NUM (5)
static int tvi_sdlc_inittab[INITTAB_NUM][2] = 
	{{ 4, 0x20}, 		// 1x clock, SDLC mode
	{10, 0x80},		// Preset CRC generator to 1s
	{5, 0xE0},		// 8bit, set DTR on
	{11, 0x08},		// RX clock /RTxC pin, TX clock in from /TRxC pin
	{14, 0x0},		// Turn off BRG
	};



struct file_operations tvi_sdlc_fops = {
	.read = tvi_sdlc_read,
	.write = tvi_sdlc_write,
	.open = tvi_sdlc_open,
	.release = tvi_sdlc_release,
	.unlocked_ioctl = tvi_sdlc_ioctl

};

static int __init tvi_sdlc_init(void) { // UPDATED

	int reg,port;

	printk(KERN_INFO "tvi_sdlc: Z85C30 driver for NTC C.H.I.P. to TeleVideo SDLC interface\n");

	// Register major number
	tvi_sdlc_dev_major = register_chrdev(0, DEVICE_NAME, &tvi_sdlc_fops);
	if (tvi_sdlc_dev_major < 0) {
		printk(KERN_ALERT "tvi_sdlc: failed to register number %d\n", tvi_sdlc_dev_major);
		return tvi_sdlc_dev_major;
	}

	// Register device class
	tvi_sdlc_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(tvi_sdlc_class)) {
		unregister_chrdev(tvi_sdlc_dev_major, DEVICE_NAME);
		printk(KERN_ALERT "tvi_sdlc: failed to register class\n");
		return PTR_ERR(tvi_sdlc_class);
	}

	// Register device driver
	tvi_sdlc_device = device_create(tvi_sdlc_class, NULL, MKDEV(tvi_sdlc_dev_major, 0), NULL, DEVICE_NAME);
	if (IS_ERR(tvi_sdlc_device)) {
		class_destroy(tvi_sdlc_class);
		unregister_chrdev(tvi_sdlc_dev_major, DEVICE_NAME);
		printk(KERN_ALERT "tvi_sdlc: failed to create device\n");
		return PTR_ERR(tvi_sdlc_device);
	}

	//Map in the GPIO addresses
	tvi_sdlc_gpio_pg = ioremap(GPIO_PAGE_ADDR, 0x1000); // Map in 4K on the right page
	if (tvi_sdlc_gpio_pg == NULL) {
		class_destroy(tvi_sdlc_class);
		unregister_chrdev(tvi_sdlc_dev_major, DEVICE_NAME);
		printk(KERN_ALERT "tvi_sdlc: failed to ioremap\n");
		return 1;
	}

	// Initialize register caches with -1
	for (reg=0;reg<=15;reg++)
		for (port=0;port<TVI_SDLC_NUM_PORTS;port++)
			tvi_sdlc_wr[reg][port] = tvi_sdlc_rr[reg][port] = -1;

	tvi_sdlc_init_gpio();
	for (port=0; port<TVI_SDLC_NUM_PORTS; port++) {
		tvi_sdlc_reset(port);
	}
	for (port=0; port<TVI_SDLC_NUM_PORTS; port++) {
		tvi_sdlc_init_z8530(port);
	}
	tvi_sdlc_enable422();

	printk(KERN_INFO "tvi_sdlc: Registered as major number %d\n", tvi_sdlc_dev_major);
	return 0;
}

static void __exit tvi_sdlc_exit(void) { // UPDATED

	int port;

	for (port=0; port<TVI_SDLC_NUM_PORTS; port++)
		tvi_sdlc_clear_rts(port);

	iounmap(tvi_sdlc_gpio_pg);
	device_destroy(tvi_sdlc_class, MKDEV(tvi_sdlc_dev_major, 0));
	class_unregister(tvi_sdlc_class);
	class_destroy(tvi_sdlc_class);
	unregister_chrdev(tvi_sdlc_dev_major, DEVICE_NAME);
	printk(KERN_INFO "tvi_sdlc: exiting\n");

}


module_init(tvi_sdlc_init);
module_exit(tvi_sdlc_exit);

/* Kernel char device funtions */
static ssize_t tvi_sdlc_read(struct file *fp, char __user *usr_buffer, size_t buffer_s, loff_t *f_offset) {

	size_t xfr_bytes = 0;
	int bufptr = 0;
	int timeout;
	int port = -1;
	int rx_crcerror=0, rx_overrun=0, rx_abort=0, rx_eom=0;
	unsigned long flags;

	memset(tvi_sdlc_data_buffer, 0, BUFF_SZ);
	bufptr=0;

	if (fp->private_data) {
		if (((long int)fp->private_data & 0xFFFF0000) == 0x85300000)
			port = ((long int)fp->private_data & 0xF);
	}
	if (port == -1)	{
		printk(KERN_INFO "tvi_sdlc: bad port numer\n");
		return TVI_SDLC_ERR_BADFP;
	}

	// Clear any waiting characters
	while (RXREADY(tvi_sdlc_read_reg(port,0)))
		tvi_sdlc_read_data(port);
	
	// Wait for CTS
	timeout = CTS_TIMEOUT;
	while (!tvi_sdlc_get_cts(port) && timeout > 0)
		timeout--;

	if (!timeout)
		return TVI_SDLC_ERR_NOCTS; // No CTS received

	udelay(5);
	// Init for RX
	tvi_sdlc_init_rx(port);

	local_irq_save(flags);
	preempt_disable();


	// Transfer data from 8530
	do {									/* 7 read / 9 write gpio / byte */
		if (bufptr)
			timeout = CHAR_TIMEOUT;
		else
			timeout = FCHAR_TIMEOUT;
		while (!RXREADY(tvi_sdlc_read_reg(port, 0)) && (timeout > 0))	/* 2/2 r/w gpio */
			timeout--;
		if (timeout > 0) {
			tvi_sdlc_data_buffer[bufptr] = tvi_sdlc_read_data(port);	/* 2/2 r/w gpio */
			bufptr++;
		}
		if (RXEOM(tvi_sdlc_read_reg(port, 1))) {				/* 3/5 gpio */
			rx_eom = 1;
			break;
		}
		if (TXABRT(tvi_sdlc_rr[0][port])) {
			rx_abort = 1;
			break;
		}
		if (RXOVERRUN(tvi_sdlc_rr[1][port])) {
			rx_overrun = 1;
			break;
		}

	} while ((bufptr < BUFF_SZ) && (timeout > 0));
	
	if (CRCERROR(tvi_sdlc_rr[1][port])) {
		rx_crcerror = 1;
	}

	// Un-init port
	tvi_sdlc_write_reg(port, 0, 0x70);
	tvi_sdlc_clear_rts(port);

	preempt_enable();
	local_irq_restore(flags);

	/* Error return */
	if (rx_abort) {
		printk(KERN_INFO "tvi_sdlc: abort received, char %d\n", bufptr);
		return TVI_SDLC_ERR_ABORT;
	} else if (rx_overrun) {
		printk(KERN_INFO "tvi_sdlc: rx overrun, char %d\n", bufptr);
		return TVI_SDLC_ERR_OVERRUN;
	} else if (!timeout) {
		printk(KERN_INFO "tvi_sdlc: rx timeout, char %d\n", bufptr);
		return TVI_SDLC_ERR_TIMEOUT - bufptr;
	} else if (rx_crcerror) {
		printk(KERN_INFO "tvi_sdlc: bad crc received, char %d\n", bufptr);
		return TVI_SDLC_ERR_BADCRC;
	} 

	// Only transfer up to buffer_s bytes.
	xfr_bytes = (bufptr < buffer_s) ? bufptr : buffer_s;

	// Reverse the bytes, since the client sends/receives the bytes in the reverse order
	for (bufptr = 0; bufptr < xfr_bytes; bufptr++) {
		if (copy_to_user(usr_buffer + bufptr, tvi_sdlc_data_buffer + xfr_bytes - bufptr - 1, 1)) {
			printk(KERN_INFO "tvi_sdlc: error copying to user space, char %d\n", bufptr);
			return TVI_SDLC_ERR_BADBUFFER;
		}
	}

	return xfr_bytes;
}

static ssize_t tvi_sdlc_write(struct file *fp, const char __user *usr_buffer, size_t buffer_s, loff_t *f_offset) {

	int xfr_bytes = 0, timeout, tx_underrun = 0, tx_ctslost = 0, txptr, port = -1, bufptr;
	unsigned long flags;

	if (buffer_s < 1)
		return 0;
	if (buffer_s > BUFF_SZ)
		return TVI_SDLC_ERR_BUFTOOSMALL;

	
	if (fp->private_data) {
		if (((long int)fp->private_data & 0xFFFF0000) == 0x85300000)
			port = ((long int)fp->private_data & 0xF);
	}
	if (port == -1)	{
		printk(KERN_INFO "tvi_sdlc: bad port numer\n");
		return TVI_SDLC_ERR_BADFP; // Something's wrong with the port #
	}


	// Reverse the bytes
	for (bufptr = 0; bufptr < buffer_s; bufptr++) {
		if (copy_from_user(tvi_sdlc_data_buffer + buffer_s - bufptr - 1, usr_buffer + bufptr, 1))
			return TVI_SDLC_ERR_BADBUFFER;
	}

	// Init port for tx
	tvi_sdlc_init_tx(port);

	// Wait for CTS
	timeout = CTS_TIMEOUT;
	while (!tvi_sdlc_get_cts(port) && timeout > 0)
		timeout--;

	if (!timeout) {
		tvi_sdlc_clear_rts(port);	// Clear RTS if we time out
		return TVI_SDLC_ERR_NOCTS; // No CTS received
	}

	local_irq_save(flags);
	preempt_disable();

	// Delay a bit
	udelay(150);

	// TX first byte, then reset EOM latch
	txptr = 0;
	tvi_sdlc_write_data(port, tvi_sdlc_data_buffer[txptr++]);			// 1/3 r/w gpio access
	tvi_sdlc_write_reg(port, 0, 0xC0);						// 1/3 r/w gpio access

	// Output the rest
	
	timeout = CHAR_TIMEOUT;
	while ((timeout > 0) && (txptr < buffer_s)) {				// 4 read 8 write
		if (!tvi_sdlc_get_cts(port)) {					// 3/5 gpio access
			tx_ctslost = 1;
		}
		if (TXEMPTY(tvi_sdlc_rr[0][port])) {
			tvi_sdlc_write_data(port, tvi_sdlc_data_buffer[txptr++]);	// 1/3 r/w gpio access
			timeout = CHAR_TIMEOUT;
		}
		timeout--;
		if (TXEOM(tvi_sdlc_rr[0][port])) {
			tx_underrun = 1;
			break;
		}

	}

	// Clear RTS when we're done
	tvi_sdlc_clear_rts(port);

	// Wait for the last couple bytes + CRC to send
	timeout=CHAR_TIMEOUT * 10;
	while (!TXEOM(tvi_sdlc_read_reg(port, 0)) && (timeout > 0))
		timeout--;

	preempt_enable();
	local_irq_restore(flags);
	xfr_bytes = txptr;

	if (txptr != buffer_s) {
		// Only error if we didn't send everything 
		if (tx_underrun) {
			tvi_sdlc_write_reg(port, 0, TVI_SDLC_SEND_ABORT);
			printk(KERN_INFO "tvi_sdlc: tx underrun, char %d\n", txptr);
			return TVI_SDLC_ERR_UNDERRUN;
		} else if (tx_ctslost) {
			tvi_sdlc_write_reg(port, 0, TVI_SDLC_SEND_ABORT);
			printk(KERN_INFO "tvi_sdlc: tx cts lost, char %d\n", txptr);
			return TVI_SDLC_ERR_CTSLOST;
		} else if (!timeout) {
			tvi_sdlc_write_reg(port, 0, TVI_SDLC_SEND_ABORT);
			printk(KERN_INFO "tvi_sdlc: tx timeout, char %d\n", txptr);
			return TVI_SDLC_ERR_TIMEOUT;
		}
	}

	return xfr_bytes;
}

static int tvi_sdlc_open(struct inode *ip, struct file *fp) {

	tvi_sdlc_open_count++;
	fp->private_data = (void *)0x85300000;

	return 0;

}

static int tvi_sdlc_release(struct inode *ip, struct file *fp) {

	if (tvi_sdlc_open_count)
		tvi_sdlc_open_count--;

	return 0;

}

static long tvi_sdlc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg) {

	long retval = 0;
	int port;
	int gpioport;
	int val;

	port = TVI_SDLC_IOCTL_DATA_PORT(arg);
	gpioport = arg & 0xF;
	val = arg >> 8;

	switch (cmd) {

		case TVI_SDLC_IOCTL_ENABLE422:
			retval = tvi_sdlc_enable422();
			break;

		case TVI_SDLC_IOCTL_DISABLE422:
			retval = tvi_sdlc_disable422();
			break;

		case TVI_SDLC_IOCTL_SET_RTS:
			if (TVI_SDLC_IOCTL_DATA_VAL(arg))
				retval = tvi_sdlc_set_rts(port);
			else
				retval = tvi_sdlc_clear_rts(port);
			break;

		case TVI_SDLC_IOCTL_GET_CTS:
			retval = tvi_sdlc_get_cts(port);
			break;

		case TVI_SDLC_IOCTL_RESET:
			retval = tvi_sdlc_reset(port);
			break;

		case TVI_SDLC_IOCTL_INIT:
			retval = tvi_sdlc_init_z8530(port);
			break;

		case TVI_SDLC_IOCTL_INIT_TX:
			retval = tvi_sdlc_init_tx(port);
			break;

		case TVI_SDLC_IOCTL_INIT_RX:
			retval = tvi_sdlc_init_rx(port);
			break;

		case TVI_SDLC_IOCTL_GET_RR:
			retval = tvi_sdlc_read_reg(port, (TVI_SDLC_IOCTL_DATA_VAL(arg) & REGMASK));
			break;

		case TVI_SDLC_IOCTL_GET_WR:
			retval = tvi_sdlc_wr[(TVI_SDLC_IOCTL_DATA_VAL(arg) & REGMASK)][port];
			break;

		case TVI_SDLC_IOCTL_SET_PORT:
			fp->private_data = (void *)(0x85300000 | (port & 0xF));
			retval = 0;
			break;

		case TVI_SDLC_IOCTL_GET_INT:
			retval = tvi_sdlc_check_int();
			break;

		case TVI_SDLC_IOCTL_SET_RR0:
			retval = tvi_sdlc_write_reg(port,0,TVI_SDLC_IOCTL_DATA_VAL(arg) & 0xFF);
			break;
			
		case TVI_SDLC_IOCTL_GET_PD:
			retval = tvi_sdlc_gpio_getpd(gpioport, val);
			break;

		case TVI_SDLC_IOCTL_SET_PD:
			retval = tvi_sdlc_gpio_setpd(gpioport, val);
			break;

		case TVI_SDLC_IOCTL_SET_IODIR:
			retval = tvi_sdlc_gpio_setiodir(val);
			break;

		case TVI_SDLC_IOCTL_GET_CFG:
			retval = tvi_sdlc_gpio_getcfg(gpioport);
			break;

		default:
			retval = -1;

	}

	return retval;

}

/* Internal functions to communicate with the z8530 chip */

void tvi_sdlc_busy_wait(void) {

	// Delay a hair by doing an ioread.
	ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	
}

/* Functions to control talking to the z8530 */
static int tvi_sdlc_init_gpio(void) {

	uint32_t tmp;

	// PD2-7 as output, 10 input, 11-15 out, 18-19 out, 20-27 in (Data)
	iowrite32(0x11111100, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,0));
	iowrite32(0x11111000, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,1));
	iowrite32(0x00001100, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2));
	iowrite32(0, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,3));

	// set PG1 = input for INT (not interrupt for now)
	tmp = ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PG,0));
	iowrite32((tmp & 0xFFFFFFF0) | 0x00000001, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PG,0));

	// Set high (off) -422_EN, -RD, -WR, -INTACT, CS:G1,-G2, low (input) DATA_TO_Z8530
	iowrite32(Z8530_DEFAULT, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	return 0;
}

static int tvi_sdlc_reset(int port) {
	
	uint32_t tmp;

	tmp = ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	// Clear CS & WR & RD
	tmp = (tmp & ~(Z8530_WR | Z8530_RD) & Z8530_CSMASK) | Z8530_CSVAL(port);
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	
	// Wait for a bit
	tvi_sdlc_busy_wait();

	// Clear everything
	iowrite32(tmp | Z8530_CTRLLINES, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	return 0;
}

static int tvi_sdlc_init_z8530(int port) {

	int i;

	for (i=0; i<INITTAB_NUM; i++)
		tvi_sdlc_write_reg(port, tvi_sdlc_inittab[i][0], tvi_sdlc_inittab[i][1]);

	return 0;
}

/* static int tvi_sdlc_init_pwm();  -- We'll leave this up to the user to configure */

static int tvi_sdlc_enable422(void) {

	iowrite32( ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD)) 
			& ~(Z8530_422EN), tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	
	return 0;

}

static int tvi_sdlc_disable422(void) {

	iowrite32( ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD)) 
			| Z8530_422EN, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	return 0;

}

static int tvi_sdlc_check_int(void) {

	return (ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PG)) & 0x2) && 1;

}

static int tvi_sdlc_check_wait(void) {

	return (ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD)) 
			& Z8530_WAIT) && 1;

}

static int tvi_sdlc_read_byte(int port, int cd) {
	
	uint32_t tmp;
	int val;
	int timeout = 1000;

	// Set everything off
	tmp = ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	tmp = (tmp & ~(Z8530_DATA_TO_Z8530)) | Z8530_CTRLLINES;
	// Already Set I/O direction from z8530
	// Set GPIO pins to inputs
	iowrite32(ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2)) & ~Z8530_DAT_IO_MASK2, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2));
	iowrite32(ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,3)) & ~Z8530_DAT_IO_MASK3, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,3));
	// Set RD
	tmp = tmp & ~Z8530_RD;
	// Set CD
	if (cd == Z8530_DATA)
		tmp = tmp | Z8530_DCSEL;
	else
		tmp = tmp & ~(Z8530_DCSEL);
	// Set AB 
	tmp &= Z8530_ABMASK;
	tmp |= Z8530_ABSEL(port);
	// Set CS lines
	tmp &= Z8530_CSMASK;
	tmp |= Z8530_CSVAL(port);
	// A:set control lines
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	// B: Wait on /WAIT
	do {
		timeout--;
	} while (!tvi_sdlc_check_wait() && (timeout > 0));
	
	// C:read values
	if (timeout > 0) {
	
		val = ((ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD))) & Z8530_DAT_MASK) >> Z8530_DAT_SHIFT;
	} else {
		val = -1;
	}
	// D:Clear RD & CS
	tmp = tmp | Z8530_RD | ~Z8530_CSMASK;
	// E:Clear everything
	tmp = tmp | Z8530_CTRLLINES;
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	//printk(KERN_ALERT "tvi_sdlc: Read byte - (0x%08x) (0x%08x) (0x%08x)\n", val1, val2, tmp);

	return val;
	
}

static int tvi_sdlc_write_byte(int port, int cd, int outbyte) {
	
	uint32_t tmp;

	// Set everything off
	tmp = ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	tmp |= Z8530_CTRLLINES;

	// A:Set I/O direction to z8530
	tmp |= Z8530_DATA_TO_Z8530;
	
	// Set GPIO pins to outputs
	iowrite32((ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2)) & ~Z8530_DAT_IO_MASK2) | Z8530_DAT_OUT_VAL2, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2));
	iowrite32((ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,3)) & ~Z8530_DAT_IO_MASK3) | Z8530_DAT_OUT_VAL3, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,3));

	// B:Write byte to pins
	tmp = (tmp & ~(Z8530_DAT_MASK)) | ((outbyte & 0xFF) << Z8530_DAT_SHIFT);

	// C:Set CS & WR & AB & CD
	tmp = tmp & ~Z8530_WR;
	if (cd == Z8530_DATA)
		tmp = tmp | Z8530_DCSEL;
	else
		tmp = tmp & ~(Z8530_DCSEL);
	// Set AB 
	tmp &= Z8530_ABMASK;
	tmp |= Z8530_ABSEL(port);
	// Set CS lines
	tmp &= Z8530_CSMASK;
	tmp |= Z8530_CSVAL(port);
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	// D:Clear WR & CS
	tmp = tmp | Z8530_WR | ~Z8530_CSMASK;
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	// Set GPIO pins to inputs
	iowrite32(ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2)) & ~Z8530_DAT_IO_MASK2, tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,2));

	// E:Clear everything
	tmp = (tmp & ~(Z8530_DATA_TO_Z8530)) | Z8530_CTRLLINES;
	iowrite32(tmp, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	//printk(KERN_ALERT "tvi_sdlc: Write byte - (0x%08x) (0x%08x)\n", val2, tmp);
	
	return 0;

}


/* Functions using the above to read/write the chip registers */
static int tvi_sdlc_write_reg(int port, uint8_t regnum, uint8_t value) {

	if (regnum != 0)
		tvi_sdlc_write_byte(port, Z8530_CTRL, regnum);
	tvi_sdlc_write_byte(port, Z8530_CTRL, value);
	tvi_sdlc_wr[(regnum & 0xF)][port] = value;

	return 0;
}

static int tvi_sdlc_read_reg(int port, uint8_t regnum) {

	if (regnum != 0)
		tvi_sdlc_write_byte(port, Z8530_CTRL, regnum);
	tvi_sdlc_rr[(regnum & 0xF)][port] = tvi_sdlc_read_byte(port, Z8530_CTRL);

	return tvi_sdlc_rr[(regnum & 0xF)][port];
}

static int tvi_sdlc_write_data(int port, uint8_t value) {

	tvi_sdlc_write_byte(port, Z8530_DATA, value);

	return 0;

}

static int tvi_sdlc_read_data(int port) {

	return tvi_sdlc_read_byte(port, Z8530_DATA);

}

/* Functions using the above to talk SDLC to a client */
static int tvi_sdlc_init_tx(int port) {

	int i;

	for (i=0; i<TXTAB_NUM; i++)
		tvi_sdlc_write_reg(port, tvi_sdlc_txtab[i][0], tvi_sdlc_txtab[i][1]);

	return 0;
}

static int tvi_sdlc_init_rx(int port) {

	int i;

	for (i=0; i<RXTAB_NUM; i++)
		tvi_sdlc_write_reg(port, tvi_sdlc_rxtab[i][0], tvi_sdlc_rxtab[i][1]);

	return 0;
}

static int tvi_sdlc_set_rts(int port) {

	if (tvi_sdlc_wr[5][port] < 0)
		tvi_sdlc_wr[5][port] = 0x6B;
	else
		tvi_sdlc_wr[5][port] |= 2;
	return tvi_sdlc_write_reg(port, 5, tvi_sdlc_wr[5][port]);
}

static int tvi_sdlc_clear_rts(int port) {

	if (tvi_sdlc_wr[5][port] < 0)
		tvi_sdlc_wr[5][port] = 0x69;
	else
		tvi_sdlc_wr[5][port] &= ~2;
	return tvi_sdlc_write_reg(port, 5, tvi_sdlc_wr[5][port]);
}

static int tvi_sdlc_get_cts(int port) {

	// Need to unlatch status	
	return CTSSET(tvi_sdlc_read_reg(port, 0x10));
}


static int tvi_sdlc_gpio_getpd(int pdport, int value) {

	return (ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD)) >> (pdport*8)) & 0xFF;
}

static int tvi_sdlc_gpio_setpd(int pdport, int value) {

	uint32_t portval = ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	uint32_t mask = 0xFF << (pdport * 8);

	portval &= ~mask;
	portval |= ((value & 0xFF) << (pdport * 8));
	iowrite32(portval, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	return (ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD)) >> (pdport*8)) & 0xFF;
}

static int tvi_sdlc_gpio_setiodir(int value) {

	uint32_t portval = ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));

	if (value)
		portval |= (Z8530_DATA_TO_Z8530);
	else
		portval &= ~(Z8530_DATA_TO_Z8530);

	iowrite32(portval, tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD));
	
	return (ioread32(tvi_sdlc_gpio_pg + GPIO_DAT(GPIO_PD)) >> (Z8530_DAT_SHIFT)) & 0xFF;

}

static int tvi_sdlc_gpio_getcfg(int port) {

	return (ioread32(tvi_sdlc_gpio_pg + GPIO_CFG(GPIO_PD,(port & 0x3))));
}
