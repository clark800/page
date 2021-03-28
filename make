#!/bin/sh

"${CC:-cc}" \
    -std=c99 -pedantic -D_POSIX_C_SOURCE -Wall -Wextra -Wshadow \
    -O2 -fpie -s -static -o page page.c
