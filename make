#!/bin/sh

"${CC:-cc}" -O2 -fno-asynchronous-unwind-tables $CPPFLAGS $CFLAGS $LDFLAGS \
    -std=c99 -Wpedantic -Wall -Wextra -Wfatal-errors \
    -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wshadow \
    -Wnull-dereference -Wcast-qual -Winit-self -Wwrite-strings \
    -o page ./*.c
