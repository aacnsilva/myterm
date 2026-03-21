# Standalone test runner — compiles and runs pure-logic tests
# without needing libghostty or Raylib.
#
# Usage:
#   make          — build and run all tests
#   make test     — same as above
#   make clean    — remove test binaries

CC      ?= gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -I../src -I.
SRCDIR  := ../src
TESTDIR := .

# Modules that can be tested without libghostty/raylib
PURE_SRCS := $(SRCDIR)/tabs.c $(SRCDIR)/config.c $(SRCDIR)/search.c $(SRCDIR)/splits.c

# We need stub implementations for functions these modules call
STUBS := stubs.c

TESTS := test_tabs test_config test_search test_splits

.PHONY: all test clean

all: test

test: $(TESTS)
	@echo ""
	@echo "Running all tests..."
	@echo "===================="
	@failed=0; \
	for t in $(TESTS); do \
		./$$t || failed=$$((failed + 1)); \
	done; \
	./test_release.sh || failed=$$((failed + 1)); \
	echo ""; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test suite(s) FAILED"; \
		exit 1; \
	else \
		echo "All test suites PASSED"; \
	fi

test_tabs: test_tabs.c $(SRCDIR)/tabs.c stubs.c
	$(CC) $(CFLAGS) -o $@ $^

test_config: test_config.c $(SRCDIR)/config.c
	$(CC) $(CFLAGS) -o $@ $^

test_search: test_search.c $(SRCDIR)/search.c stubs.c
	$(CC) $(CFLAGS) -o $@ $^

test_splits: test_splits.c $(SRCDIR)/splits.c stubs.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)
