#define _POSIX_C_SOURCE 1
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

static FILE* TTY = NULL;
static struct termios* TERM = NULL;
static uintmax_t LINE = 0, PROGRESS = 0, SIZE = 0;

typedef enum {OTHER, ESC, UP, DOWN} EscSeq;
typedef enum {DEFAULT, ESCAPE, NF, CSI, FINAL} EscState; // for ansi esc codes

static uintmax_t movecursor(int ch, uintmax_t column) {
    if (ch == '\r')
        return 0;
    if (ch == '\t')
        return column + (8 - column % 8);
    if (ch == '\b')
        return column > 0 ? column - 1 : 0;
    if ((ch < 0x80 && !isprint(ch)) || (ch >= 0x80 && ch <= 0xBF))
        return column;  // non-printing ascii or non-initial utf8 bytes
    return column + 1;
}

// handle ANSI escape codes - we need this to track when to wrap lines, which
// is necessary to make sure that we don't print too much and cause scroll off
// on the first page
// http://www.inwap.com/pdp10/ansicode.txt
// https://en.wikipedia.org/wiki/ANSI_escape_code
static EscState transition(EscState state, int ch) {
    if (ch < 0x20 || ch > 0x7F)
        return ch == 0x1B ? ESCAPE : DEFAULT;
    switch (state) {
        case ESCAPE:
            if (ch >= 0x20 && ch <= 0x2F)
                return NF;  // nF 3-byte code
            return ch == '[' ? CSI : FINAL; // Fp, Fe, Fs 2-byte codes
        case NF:
            return FINAL;
        case CSI:
            return ch >= 0x20 && ch <= 0x3F ? CSI : FINAL;
        default:
            return DEFAULT;
    }
}

static int visible(int ch) {
    return movecursor(ch, 0) > 0;
}

static int end(int ch) {
    return ch == '\n' || ch == EOF;
}

static int printrow(uintmax_t columns, FILE* input) {
    uintmax_t column = 0;
    int ch = fgetc(input);
    if (ch == EOF)
        return EOF;
    EscState state = transition(DEFAULT, ch);

    // avoid splitting unicode characters and ansi escape codes
    while ((column < columns || !visible(ch) || state != DEFAULT) && !end(ch)) {
        fputc(ch, stdout);
        PROGRESS += 1;
        if (state == DEFAULT)
            column = movecursor(ch, column);
        ch = fgetc(input);
        state = transition(state, ch);
    }

    if (ch == '\n' || ch == EOF) {
        PROGRESS += (ch == '\n');
        LINE += 1;
    } else
        ungetc(ch, input);
    fputc('\n', stdout);
    return '\n';
}

static void erase(void) {
    if (TERM)
        fputs("\r          \r", stdout);
}

static void printstatus(int ch) {
    if (TERM) {
        if (SIZE > 0)
            printf("--(%ju%%)--", PROGRESS >= UINTMAX_MAX/100 ?
                    PROGRESS/(SIZE/100) : (100*PROGRESS)/SIZE);
        else
            printf(ch == EOF ? "--(END)--" : "--(MORE)--");
    }
    fflush(stdout);
}

static int printrows(uintmax_t rows, uintmax_t cols, FILE* input, int fill) {
    int ch = 0;
    uintmax_t i = 0;
    erase();
    for (; i < rows || rows == UINTMAX_MAX; i++)
        if ((ch = printrow(cols, input)) == EOF)
            break;
    for (; fill && i < rows; i++)
        fputs("~\n", stdout);
    printstatus(ch);
    return ch;
}

static int skiplines(uintmax_t lines, FILE* input) {
    int ch = 0;
    for (uintmax_t i = 0; i < lines && ch != EOF; i++) {
        do {
            ch = fgetc(input);
            PROGRESS += (ch != EOF);
        } while (ch != '\n' && ch != EOF);
        LINE += (ch == '\n');
    }
    return ch;
}

static int gotoline(uintmax_t line, uintmax_t rows, uintmax_t columns,
        FILE* input) {
    // we can't reliably go to a line that is already displayed without
    // re-reading because it may be a very long line that has scrolled off
    if (line <= LINE) {
        if (fseek(input, 0, SEEK_SET) != 0)
            return '\n';
        LINE = 0, PROGRESS = 0;
    }
    skiplines(line - LINE - 1, input);
    return printrows(rows - 1, columns, input, 1);
}

static int scrollback(uintmax_t lines, uintmax_t rows, uintmax_t columns,
        FILE* input) {
    uintmax_t topline = LINE - (rows - 1) + 1;
    uintmax_t line = topline <= lines ? 1 : topline - lines;
    return gotoline(line, rows, columns, input);
}

static void quit(int signal) {
    erase();
    if (TTY != NULL && TERM != NULL)
        tcsetattr(fileno(TTY), TCSANOW, TERM);
    exit(signal == 0 ? 0 : 1);
}

// https://en.wikipedia.org/wiki/ANSI_escape_code#Terminal_input_sequences
static EscSeq readescseq(FILE* tty) {
    char ch = 0;
    struct termios term;
    if (tcgetattr(fileno(tty), &term) != 0)
        return perror("tcgetattr"), OTHER;
    cc_t vmin = term.c_cc[VMIN];
    cc_t vtime = term.c_cc[VTIME];
    // make reads stop blocking after 1/10 second
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 1;
    if (tcsetattr(fileno(tty), TCSANOW, &term) != 0)
        return perror("tcsetattr"), OTHER;
    size_t n = fread(&ch, 1, 1, tty), m = n;
    if (n == 1 && ch == '[')
        do n += (m = fread(&ch, 1, 1, tty));
        while (m == 1 && isdigit(ch));
    term.c_cc[VMIN] = vmin;
    term.c_cc[VTIME] = vtime;
    if (tcsetattr(fileno(tty), TCSANOW, &term) != 0)
        return perror("tcsetattr"), OTHER;
    if (n == 0 || (n == 1 && ch == 27))
        return ESC;
    if (n == 2 && ch == 'A')
        return UP;
    if (n == 2 && ch == 'B')
        return DOWN;
    return OTHER;
}

int main(int argc, char* argv[]) {
    int rows = 24, columns = 80;
    FILE* input = stdin;

    if ((argc < 2 && isatty(fileno(stdin))) || argc > 2)
        return fprintf(stderr, "usage: %s [file]\n", argv[0]), 2;

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL)
            return perror("cannot open file"), 1;
    }

    struct stat stats;
    if (fstat(fileno(input), &stats) == 0)
        SIZE = stats.st_size;

    TTY = fopen(ctermid(NULL), "r");
    if (TTY == NULL)
        return perror("cannot open tty"), 1;

    struct winsize ws;
    if (ioctl(fileno(TTY), TIOCGWINSZ, &ws) != -1) {
        rows = ws.ws_row;
        columns = ws.ws_col;
    }

    // print one screen or dump all input if output is piped
    printrows(isatty(fileno(stdout)) ? rows-1 : UINTMAX_MAX, columns, input, 0);

    // exit if the whole input file/stream fits on one screen or was dumped
    if (ungetc(fgetc(input), input) == EOF)
        exit(0);

    // ensure that terminal settings are restored before exiting
    struct sigaction action = {.sa_handler = &quit};
    sigaction(SIGINT, &action, NULL);  // Ctrl-C
    sigaction(SIGQUIT, &action, NULL); // Ctrl-'\'
    sigaction(SIGTSTP, &action, NULL); // Ctrl-Z
    sigaction(SIGTERM, &action, NULL); // kill
    sigaction(SIGHUP, &action, NULL);  // hangup

    // disable keypress buffering and echo
    struct termios term;
    if (tcgetattr(fileno(TTY), &term) != 0)
        return perror("tcgetattr"), 1;
    tcflag_t oldflags = term.c_lflag;
    term.c_lflag &= ~ICANON & ~ECHO;
    if (tcsetattr(fileno(TTY), TCSANOW, &term) != 0)
        return perror("tcsetattr"), 1;
    term.c_lflag = oldflags;
    TERM = &term;

    printstatus('\n');  // status is only printed after TERM is set

    uintmax_t N = 0;
    while (1) {
        int c = fgetc(TTY);
        switch (c) {
            case 'j':
            case '\n':
                printrows(N ? N : 1, columns, input, 0);
                break;
            case 'k':
                scrollback(N ? N : 1, rows, columns, input);
                break;
            case ' ':
                printrows((N ? N : 1) * (rows - 1), columns, input, 0);
                break;
            case 'b':
                scrollback((N ? N : 1) * (rows - 1), rows, columns, input);
                break;
            case 'd':
                printrows((N ? N : 1) * ((rows-1) / 2), columns, input, 0);
                break;
            case 'u':
                scrollback((N ? N : 1) * ((rows-1) / 2), rows, columns, input);
                break;
            case 'g':
                gotoline(N ? N : 1, rows, columns, input);
                break;
            case 'G':
                printrows(UINTMAX_MAX, columns, input, 0);
                break;
            case 'q':
            case 4:  // Ctrl-D produces EOT in non-canonical mode
                quit(0);
            case 27: // esc code, may be esc key, arrow, or other keypress
                switch (readescseq(TTY)) {
                    case ESC:
                        quit(0);
                    case UP:
                        scrollback(N ? N : 1, rows, columns, input);
                        break;
                    case DOWN:
                        printrows(N ? N : 1, columns, input, 0);
                    default: break;
                }
        }
        N = isdigit(c) ? 10 * N + (c - '0') : 0;
    }
}
