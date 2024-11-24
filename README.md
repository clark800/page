# Introduction

`page` is a minimalist terminal pager written in C with no dependencies.

* Supports UTF-8 (and only UTF-8).
* Handles all ANSI escape codes (e.g. colors) except codes that move the cursor.
* Shows percentage progress when displaying a file.
* Should be portable to most POSIX systems, though it is not strictly POSIX compliant since there wasn't a POSIX compliant way to get the terminal dimensions in C until 2024 which isn't supported everywhere yet.
* `page` is about 200 lines of code and compiles to a 30KB static binary with `musl-gcc`.

# Usage

    <command> | page
    page <file>

# Commands

* `ENTER`/`DOWN` - scroll one line
* `SPACE` - scroll one screen
* `d` - scroll half a screen
* `g` - go to top of file (when paging a file)
* `Ng` - go to line number N (can only go backwards if paging a file)
* `q`/`ESC` - quit

Scrolling commands can be prefixed by a number N to make them repeat N times.

# Building

Run `./make` or `env CC=musl-gcc ./make`.
