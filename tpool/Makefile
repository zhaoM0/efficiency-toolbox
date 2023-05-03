NAME := tpool

.PHONY: all test clean

.DEFAULT_GOAL := all
all: test

tpool.o: tpool.c tpool.h
	gcc -c $^

test: tpool.o test.c
	gcc -o $@ $^ -lpthread

clean:
	rm -f test tpool.o