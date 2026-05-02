CC      = gcc
CFLAGS  = -Wall -Wextra -g -fsanitize=address,undefined

TESTS   = sieve_test lru_test

all: $(TESTS)

sieve_test: sieve_test.c
	$(CC) $(CFLAGS) -o $@ $<

lru_test: lru_test.c
	$(CC) $(CFLAGS) -o $@ $<

lfu_test: lfu_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TESTS)

.PHONY: all clean
