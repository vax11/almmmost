# Makefile to assmeble z80 sources
Z80ASM=z80asm

SOURCES=$(wildcard *.ZASM)
TARGETS=$(SOURCES:.ZASM=.CPM)

all: $(TARGETS)

clean: 
	rm -f $(TARGETS)

%.CPM: %.ZASM
	$(Z80ASM) -o $@ --list=$*.LST $<
