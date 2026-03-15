CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wshadow -std=c99 -pedantic -g -fwrapv
EXTRA_CFLAGS ?=

SOURCES=clacc.c parse.c lib/hdict.c lib/xalloc.c

.PHONY: default clean test c0vm-lite \
        coverage coverage-full coverage-clean \
        docs docs-clean

default: clacc

clacc: $(SOURCES) clacc.h parse.h lib/c0vm.h lib/hdict.h lib/xalloc.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o clacc $(SOURCES)

c0vm-lite:
	$(MAKE) -C tools/c0vm-lite

test: clacc c0vm-lite
	@./tests/run_tests.sh

test-full: clacc
	@./tests/run_tests.sh --runner "$${C0VM:-./vendor/c0vm/c0vm}" --strip-return-line

# --- Code Coverage (requires lcov) ---

coverage-clean:
	rm -f clacc *.gcno *.gcda lib/*.gcno lib/*.gcda
	rm -f coverage.info coverage-filtered.info
	rm -rf coverage-report
	$(MAKE) -C tools/c0vm-lite coverage-clean

LCOV_FLAGS = --rc branch_coverage=1 --ignore-errors unused,unused \
             --ignore-errors deprecated,deprecated

coverage: coverage-clean
	$(MAKE) clacc EXTRA_CFLAGS="--coverage"
	$(MAKE) -C tools/c0vm-lite EXTRA_CFLAGS="--coverage"
	@./tests/run_tests.sh
	lcov --capture --directory . --directory tools/c0vm-lite \
	     --output-file coverage.info $(LCOV_FLAGS)
	lcov --remove coverage.info '/usr/*' \
	     --output-file coverage-filtered.info $(LCOV_FLAGS)
	genhtml coverage-filtered.info --output-directory coverage-report \
	        $(LCOV_FLAGS)
	@echo ""
	@lcov --summary coverage-filtered.info $(LCOV_FLAGS)
	@echo ""
	@echo "Coverage report: coverage-report/index.html"

coverage-full: coverage-clean
	$(MAKE) clacc EXTRA_CFLAGS="--coverage"
	$(MAKE) -C $${C0VM_DIR:-../c0vm} c0vm EXTRA_CFLAGS="--coverage"
	@./tests/run_tests.sh --runner "$${C0VM_DIR:-../c0vm}/c0vm" --strip-return-line
	lcov --capture --directory . --directory $${C0VM_DIR:-../c0vm} \
	     --output-file coverage.info $(LCOV_FLAGS)
	lcov --remove coverage.info '/usr/*' \
	     --output-file coverage-filtered.info $(LCOV_FLAGS)
	genhtml coverage-filtered.info --output-directory coverage-report \
	        $(LCOV_FLAGS)
	@echo ""
	@lcov --summary coverage-filtered.info $(LCOV_FLAGS)
	@echo ""
	@echo "Coverage report: coverage-report/index.html"

# --- Documentation (requires Doxygen) ---

docs:
	doxygen Doxyfile

docs-clean:
	rm -rf docs/html

clean:
	rm -f clacc *.bc0
	$(MAKE) -C tools/c0vm-lite clean
