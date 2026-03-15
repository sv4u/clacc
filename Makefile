CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wshadow -std=c99 -pedantic -g -fwrapv

SOURCES=clacc.c parse.c lib/hdict.c lib/xalloc.c

.PHONY: default clean test c0vm-lite

default: clacc

clacc: $(SOURCES) clacc.h parse.h lib/c0vm.h lib/hdict.h lib/xalloc.h
	$(CC) $(CFLAGS) -o clacc $(SOURCES)

c0vm-lite:
	$(MAKE) -C tools/c0vm-lite

test: clacc c0vm-lite
	@./tests/run_tests.sh

test-full: clacc
	@./tests/run_tests.sh --runner "$${C0VM:-./vendor/c0vm/c0vm}"

clean:
	rm -f clacc *.bc0
	$(MAKE) -C tools/c0vm-lite clean
