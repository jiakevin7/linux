CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O0
LDFLAGS := -lnuma

TARGETS := test_first_touch

.PHONY: all clean

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
