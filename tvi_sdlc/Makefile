obj-m := tvi_sdlc.o


all:	mod-all tvi_sdlc_ioctl

clean:	mod-clean

mod-all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mod-clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

tvi_sdlc_ioctl: tvi_sdlc_ioctl.c
