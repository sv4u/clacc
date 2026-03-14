CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wshadow -std=c99 -pedantic -g -fwrapv

SOURCES=clacc.c parse.c lib/hdict.c lib/xalloc.c

.PHONY: default clean

default: clacc

clacc: $(SOURCES) clacc.h parse.h lib/c0vm.h lib/hdict.h lib/xalloc.h
	$(CC) $(CFLAGS) -o clacc $(SOURCES)

clean:
	rm -f clacc *.bc0
