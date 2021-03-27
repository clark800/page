# Introduction

`more` is a minimalist terminal pager written in C with no dependencies. It
assumes that the terminal encoding is UTF-8. It handles the most common ANSI
control codes, including colors. When displaying a file, it shows percentage
progress. It should be portable to most POSIX systems, though it is not
technically POSIX compliant.

It does not properly handle less commonly used ANSI control codes or
backspace characters following unicode characters. In these cases the output
may line wrap at unexpected places and some trailing portions of control codes
may get displayed.

`more` is about 150 lines of code and compiles to a 30KB static binary with `musl-gcc`.

# Usage

    ls /bin | more
    more /etc/profile

# Commands

* `ENTER` - scroll one line
* `SPACE` - scroll one screen
* `d` - scroll half a screen
* `q` - quit


# Building

Run `./make` or `env CC=musl-gcc ./make`.
