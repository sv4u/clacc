CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wshadow -std=c99 -pedantic -g -fwrapv

.PHONY: default clean

default: c0vm-lite

c0vm-lite: c0vm_lite.c
	$(CC) $(CFLAGS) -o c0vm-lite c0vm_lite.c

clean:
	rm -f c0vm-lite
