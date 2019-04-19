# Makefile to build Almmmost. 
# This requires libcurl (https://curl.haxx.se/libcurl/)
# and libini (https://github.com/pcercuei/libini)
#  -- Note that on debian, libini requires CMakeLists.txt to be changed on 
#  	line 1 to require VERSION 3.0.0.
# This also requires the tvi_sdlc kernel driver

INCLUDEDIR=../tvi_sdlc
CFLAGS=-I$(INCLUDEDIR) -Wall -g
CC=gcc
LDFLAGS=-lini -lcurl-gnutls
ALSOURCES=$(wildcard almmmost*.c)
ALOBJECTS=$(ALSOURCES:.c=.o)


all:	almmmost pbm2bin

almmmost:	$(ALOBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

pbm2bin:	pbm2bin.c

make_e5:	make_e5.c

clean:
	rm -f *.o almmmost pbm2bin
