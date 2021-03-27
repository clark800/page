#include <sys/ioctl.h>
#include <stdio.h>
#include <ctype.h>
#include <termios.h>

typedef enum {DEFAULT, ESCAPE, CODE, END} State;

static int move(int ch, int column) {
    if (ch == '\r')
        return 0;
    if (ch == '\t')
        return column + (8 - column % 8);
    if (ch == '\b')
        return column > 0 ? column - 1 : 0;
    if ((ch < 0x80 && !isprint(ch)) || (ch >= 0x80 && ch < 0xC0))
        return column;  // non-printing ascii or non-initial utf8 bytes
    return column + 1;
}

static int visible(int ch) {
    return move(ch, 0) != 0 || ch == '\n';
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

static int printline(int columns) {
    int column = 0, ch = getchar();
    int state = transition(DEFAULT, ch);

    // avoid line splits in the middle of unicode and ansi escape sequences
    while ((column < columns || !visible(ch) || state != DEFAULT) && !end(ch)) {
        putchar(ch);
        if (state == DEFAULT)
            column = move(ch, column);
        ch = getchar();
        state = transition(state, ch);
    }

    if (ch == EOF)
        return ch;
    if (ch != '\n')
        ungetc(ch, stdin);
    putchar('\n');
    fflush(stdout);
    return ch;
}

static int printlines(int lines, int columns) {
    int c = 0;
    for (int i = 0; i < lines && c != EOF; i++)
        c = printline(columns);
    return c;
}

int main(void) {
    int lines = 24, columns = 80;

    FILE* tty = fopen("/dev/tty", "r");
    if (tty == NULL)
        return perror("failed to open /dev/tty"), 1;

    struct winsize ws;
    if (ioctl(fileno(tty), TIOCGWINSZ, &ws) != -1) {
        lines = ws.ws_row;
        columns = ws.ws_col;
    }

    struct termios term;
    if (tcgetattr(fileno(tty), &term) != 0)
        return perror("tcgetattr"), 1;
    term.c_lflag &= ~ICANON & ~ECHO; // disable keypress buffering and echo
    if (tcsetattr(fileno(tty), TCSANOW, &term) != 0)
        return perror("tcsetattr"), 1;

    int c = printlines(lines - 1, columns);

    while (c != EOF) {
        switch (fgetc(tty)) {
            case '\n':
                c = printline(columns);
                break;
            case ' ':
                c = printlines(lines - 1, columns);
                break;
            case 'q':
                fclose(tty);
                return 0;
        }
    }

    fclose(tty);
    return 0;
}
