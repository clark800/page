#define _POSIX_C_SOURCE 1
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

static FILE* TTY = NULL;
static struct termios* TERM = NULL;
static int PROGRESS = 0, SIZE = 0;

typedef enum {OTHER, ESC, DOWN} EscSeq;
typedef enum {DEFAULT, ESCAPE, NF, CSI, FINAL} EscState; // for ansi esc codes

static int movecursor(int ch, int column) {
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

// handle ANSI escape codes
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

static int printline(int columns, FILE* input) {
    int column = 0, ch = fgetc(input);
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

    if (ch == EOF)
        return ch;
    if (ch == '\n')
        PROGRESS += 1;
    else
        ungetc(ch, input);
    fputc('\n', stdout);
    return ch;
}

static void erase(void) {
    if (SIZE > 0)
        fputs("\r          \r", stdout);
}

static int printlines(int rows, int columns, FILE* input) {
    int ch = 0;
    erase();
    for (int i = 0; i < rows && ch != EOF; i++)
        ch = printline(columns, input);
    if (SIZE > 0)
        fprintf(stdout, "--(%d%%)--", (100*PROGRESS)/SIZE);
    fflush(stdout);
    return ch;
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
    if (n == 2 && ch == 'B')
        return DOWN;
    return OTHER;
}

int main(int argc, char* argv[]) {
    int rows = 24, columns = 80;
    int istty = isatty(fileno(stdout));
    FILE* input = stdin;

    if ((argc < 2 && isatty(fileno(stdin))) || argc > 2)
        return fprintf(stderr, "usage: %s [file]\n", argv[0]), 2;

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL)
            return perror("cannot open file"), 1;
        struct stat stats;
        if (istty && fstat(fileno(input), &stats) == 0)
            SIZE = stats.st_size;
    }

    TTY = fopen(ctermid(NULL), "r");
    if (TTY == NULL)
        return perror("cannot open tty"), 1;

    struct winsize ws;
    if (ioctl(fileno(TTY), TIOCGWINSZ, &ws) != -1) {
        rows = ws.ws_row;
        columns = ws.ws_col;
    }

    if (!istty) {
        while (printlines(rows - 1, columns, input) != EOF);
        quit(0);
    }

    printlines(rows - 1, columns, input);

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

    while (1) {
        switch (fgetc(TTY)) {
            case '\n':
                printlines(1, columns, input);
                break;
            case 'd':
                printlines(rows / 2, columns, input);
                break;
            case 't':
                if (fseek(input, 0, SEEK_SET) != 0)
                    break;
                PROGRESS = 0;  // fallthrough
            case ' ':
                printlines(rows - 1, columns, input);
                break;
            case 'q':
                quit(0);
            case 27: // esc code, may be esc key, arrow, or other keypress
                switch (readescseq(TTY)) {
                    case ESC:
                        quit(0);
                    case DOWN:
                        printlines(1, columns, input);
                    default: break;
                }
        }
    }
}
