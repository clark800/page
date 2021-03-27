#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>

static int PROGRESS = 0, SIZE = 0;

typedef enum {DEFAULT, ESCAPE, CODE, END} State;  // for ansi escape codes

static int movecursor(int ch, int column) {
    if (ch == '\r')
        return 0;
    if (ch == '\t')
        return column + (8 - column % 8);
    if (ch == '\b')     // backspace (won't work properly for utf8)
        return column > 0 ? column - 1 : 0;
    if ((ch < 0x80 && !isprint(ch)) || (ch >= 0x80 && ch < 0xC0))
        return column;  // non-printing ascii or non-initial utf8 bytes
    return column + 1;
}

static int visible(int ch) {
    return movecursor(ch, 0) != 0 || ch == '\n';
}

static int end(int ch) {
    return ch == '\n' || ch == EOF;
}

static State transition(State state, int ch) {
    const int ESC = 033;
    switch (state) {
        case ESCAPE:
            return ch == '[' ? CODE : (ch == ESC ? ESCAPE : DEFAULT);
        case CODE:
            return isdigit(ch) || ch == ';' ? CODE : END;
        default:
            return ch == ESC ? ESCAPE : DEFAULT;
    }
}

static int printline(int columns, FILE* stream) {
    int column = 0, ch = fgetc(stream);
    int state = transition(DEFAULT, ch);

    // avoid line splits in the middle of unicode and ansi escape sequences
    while ((column < columns || !visible(ch) || state != DEFAULT) && !end(ch)) {
        fputc(ch, stdout);
        PROGRESS += 1;
        if (state == DEFAULT)
            column = movecursor(ch, column);
        ch = fgetc(stream);
        state = transition(state, ch);
    }

    if (ch == EOF)
        return ch;
    if (ch == '\n')
        PROGRESS += 1;
    else
        ungetc(ch, stream);
    fputc('\n', stdout);
    return ch;
}

static void erase() {
    if (SIZE > 0)
        fputs("\r          \r", stdout);
}

static int printlines(int rows, int columns, FILE* stream) {
    int ch = 0;
    erase();
    for (int i = 0; i < rows && ch != EOF; i++)
        ch = printline(columns, stream);
    if (SIZE > 0)
        fprintf(stdout, "--(%d%%)--", (100*PROGRESS)/SIZE);
    fflush(stdout);
    return ch;
}

static int cleanup(FILE* tty, FILE* stream) {
    erase();
    fclose(tty);
    fclose(stream);
    return 0;
}

int main(int argc, char* argv[]) {
    FILE* stream = stdin;
    int rows = 24, columns = 80;
    int istty = isatty(fileno(stdout));

    if (argc > 2)
        return fputs("usage: more [file]\n", stderr), 2;

    if (argc == 2) {
        stream = fopen(argv[1], "r");
        if (stream == NULL)
            return perror("cannot open file"), 1;
        struct stat stats;
        if (istty && fstat(fileno(stream), &stats) == 0)
            SIZE = stats.st_size;
    }

    FILE* tty = fopen(ctermid(NULL), "r");
    if (tty == NULL)
        return perror("cannot open tty"), 1;

    struct winsize ws;
    if (ioctl(fileno(tty), TIOCGWINSZ, &ws) != -1) {
        rows = ws.ws_row;
        columns = ws.ws_col;
    }

    if (!istty) {
        while (printlines(rows - 1, columns, stream) != EOF);
        return cleanup(tty, stream);
    }

    struct termios term;
    if (tcgetattr(fileno(tty), &term) != 0)
        return perror("tcgetattr"), 1;
    term.c_lflag &= ~ICANON & ~ECHO; // disable keypress buffering and echo
    if (tcsetattr(fileno(tty), TCSANOW, &term) != 0)
        return perror("tcsetattr"), 1;

    int ch = printlines(rows - 1, columns, stream);

    while (ch != EOF) {
        switch (fgetc(tty)) {
            case '\n':
                ch = printlines(1, columns, stream);
                break;
            case ' ':
                ch = printlines(rows - 1, columns, stream);
                break;
            case 'q':
                return cleanup(tty, stream);
        }
    }

    return cleanup(tty, stream);
}
