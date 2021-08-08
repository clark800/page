#!/bin/sh

"${CC:-cc}" $CPPFLAGS \
    ${CFLAGS--O2 -fno-asynchronous-unwind-tables} \
    ${LDFLAGS--s -static -Wl,--gc-sections} \
    -std=c99 -Wpedantic -Wall -Wextra -Wfatal-errors \
    -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wshadow \
    -Wnull-dereference -Wcast-qual -Winit-self -Wwrite-strings \
    -o page ./*.c
