# Compiler and flags
CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O0
LDFLAGS := -lnuma

# Binaries to build (each must have a matching .c file)
TARGETS := test test_first_touch

.PHONY: all clean

# Default target
all: $(TARGETS)

# Generic rule: build 'prog' from 'prog.c'
%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
