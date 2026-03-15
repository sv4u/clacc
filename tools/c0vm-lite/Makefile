CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wshadow -std=c99 -pedantic -g -fwrapv
EXTRA_CFLAGS ?=

.PHONY: default clean coverage-clean

default: c0vm-lite

c0vm-lite: c0vm_lite.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o c0vm-lite c0vm_lite.c

coverage-clean:
	rm -f c0vm-lite *.gcno *.gcda

clean:
	rm -f c0vm-lite
