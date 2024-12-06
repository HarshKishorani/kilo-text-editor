/*** includes ***/

// Feature test macro : https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
// We add them above our includes, because the header files we’re including use the macros to decide what features to expose.
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define ZEN_VERSION "0.0.1"
#define ZEN_TAB_STOP 4
#define ZEN_QUIT_TIMES 3

/*
    The 'CTRL_KEY' macro bitwise-ANDs a character with the value 00011111, in binary.
    (In C, you generally specify bitmasks using hexadecimal, since C doesn’t have binary literals, and hexadecimal is more concise and
    readable once you get used to it.)

    In other words, it sets the upper 3 bits of the character to 0.
    This mirrors what the Ctrl key does in the terminal: it strips bits 5 and 6 from whatever key you press in combination with Ctrl,
    and sends that.
    (By convention, bit numbering starts from 0.) The ASCII character set seems to be designed this way on purpose.
    (It is also similarly designed so that you can set and clear bit 5 to switch between lowercase and uppercase.)
*/
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

/// @brief Data type for storing a row of text in our editor.
typedef struct erow
{
    int size;
    int rsize; // Contains the size of the contents of 'render'.
    char *chars;
    char *render; // Ccntains the actual characters to draw on the screen for that row of text.
} erow;

struct editorConfig
{
    int cx, cy; // Cursor co-ordinates
    int rx;     // x variable index into the render field.
    int rowoff; // Keep track of what row of the file the user is currently scrolled to
    int coloff; // Keep track of what col of the file the user is currently scrolled to
    int numrows;
    erow *row;
    int dirty;
    int screenrows;
    int screencols;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};
struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

/*** terminal ***/

/// @brief Function that prints an error message and exits the program.
void die(const char *s)
{
    // Clear the screen on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    /*
        Most C library functions that fail will set the global errno variable to indicate what the error was.
        perror() looks at the global errno variable and prints a descriptive error message for it.
        It also prints the string given to it before it prints the error message, which is meant to provide context about what part of your code caused the error.
    */
    perror(s);
    exit(1);
}

/// @brief Restore original terminal attributes when progeam exits.
void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/// @brief Enable Raw mode for our terminal
void enableRawMode()
{
    /*
        We can set a terminal’s attributes by :
        (1) using tcgetattr() to read the current attributes into a struct.
        (2) modifying the struct by hand
        (3) passing the modified struct to tcsetattr() to write the new terminal attributes back out.
    */
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) // Get current Terminal attributes
        die("tcgetattr");

    // register our disableRawMode() function to be called automatically when the program exits, whether it exits by returning from main(), or by calling the exit() function
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;

    /*
        By default your terminal starts in canonical mode, also called cooked mode.
        In this mode, keyboard input is only sent to your program when the user presses Enter.
    */

    /*
        Turn off flags:
        - Local Flag : "ECHO" command
        - Local Flag : "ICANON" (Canonical mode for the terminal)
        - Local Flag : "ISIG" (Turn off 'Ctrl-C' => sends a SIGINT signal to the current process which causes it to terminate,
                       and 'Ctrl-Z' => sends a SIGTSTP signal to the current process which causes it to suspend.)
                       Now Ctrl-C can be read as a 3 byte and Ctrl-Z can be read as a 26 byte.
        - Input flag : "IXON" (Turn off => "Ctrl-S" and "Ctrl-Q" are used for software flow control. Ctrl-S stops data from being transmitted to the terminal until you press "Ctrl-Q".)
                       Now Ctrl-S can be read as a 19 byte and Ctrl-Q can be read as a 17 byte.
        - Local flag : "IEXTEN" (Disable "Ctrl-V", the terminal waits for you to type another character and then sends that character literally.)
                       Ctrl-V can now be read as a 22 byte, and Ctrl-O as a 15 byte.
        - Input flag : "ICRNL" (Diable Ctrl-M)
                       Now Ctrl-M is read as a 13 (carriage return), and the Enter key is also read as a 13.
        - Output flag : We will turn off all output processing features by turning off the "OPOST" flag.

        Miscellaneous flags
        - When "BRKINT" is turned on, a break condition will cause a SIGINT signal to be sent to the program, like pressing Ctrl-C.
        - "INPCK" enables parity checking, which doesn’t seem to apply to modern terminal emulators.
        - "ISTRIP" causes the 8th bit of each input byte to be stripped, meaning it will set it to 0. This is probably already turned off.

        - "CS8" is not a flag, it is a bit mask with multiple bits, which we set using the bitwise-OR (|) operator unlike all the flags we are turning off.
          It sets the character size (CS) to 8 bits per byte. On my system, it’s already set that way.
    */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*
        VMIN and VTIME are indexes into the c_cc field, which stands for “control characters”, an array of bytes that control various terminal settings.
        The VMIN value sets the minimum number of bytes of input needed before read() can return.
        We set it to 0 so that read() returns as soon as there is any input to be read.
        The VTIME value sets the maximum amount of time to wait before read() returns.
        It is in tenths of a second, so we set it to 1/10 of a second, or 100 milliseconds.
        If read() times out, it will return 0, which makes sense because its usual return value is the number of bytes read.
    */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) // set modified attributes for current terminal.
        die("tcsetattr");
}

/// @brief Wait for one keypress, and return it.
int editorReadKey()
{
    int nread;
    char c;

    // Read in char c
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    // If we read the escape character => special key press
    if (c == '\x1b')
    {
        // Read next 2 chars (bytes)
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            // Handle Page-Up and Page-down keys
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;
    /*
        The n command (Device Status Report) can be used to query the terminal for status information.
        We want to give it an argument of 6 to ask for the cursor position.
    */
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    while (i < sizeof(buf) - 1)
    {
        // Read response of n command in a buffer to parse it. The respnse will be something like \x1b[48;53R. Representing the cursor position
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    // Read these values in the rows and cols
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    // Get the size of the terminal by simply calling ioctl() with the TIOCGWINSZ request.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        /*
            ioctl() isn’t guaranteed to be able to request the window size on all systems,
            so we are going to provide a fallback method of getting the window size.

            The strategy is to position the cursor at the bottom-right of the screen, then use escape sequences that let us query the position of the cursor.
            That tells us how many rows and columns there must be on the screen.

            We are sending two escape sequences one after the other.
            The C command (Cursor Forward) moves the cursor to the right, and the B command (Cursor Down) moves the cursor down.
            The argument says how much to move it right or down by.
            We use a very large value, 999, which should ensure that the cursor reaches the right and bottom edges of the screen.
        */
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

/// @brief Converts a chars index into a render index.
int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;

    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
        {
            /*
                For each character, if it’s a tab we use rx % ZEN_TAB_STOP to find out how many columns we are to the right of the last tab stop,
                and then subtract that from ZEN_TAB_STOP - 1 to find out how many columns we are to the left of the next tab stop.
                We add that amount to rx to get just to the left of the next tab stop, and then the unconditional rx++ statement gets us right on the next tab stop.
            */
            rx += (ZEN_TAB_STOP - 1) - (rx % ZEN_TAB_STOP);
        }
        rx++;
    }

    return rx;
}

/// @brief Uses the chars string of an erow to fill in the contents of the render string.
/// @param row
void editorUpdateRow(erow *row)
{
    // Count number of tabs used in line as we will render spaces instead of tabs.
    // Because tabs just shift the cursor.
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
            tabs++;
    }

    // Allocate memory to render.
    free(row->render);
    row->render = malloc(row->size + tabs * (ZEN_TAB_STOP - 1) + 1);
    int idx = 0;

    for (j = 0; j < row->size; j++)
    {
        // If we encounter tab
        if (row->chars[j] == '\t')
        {
            // Maximum number of characters needed for each tab is ZEN_TAB_STOP.
            row->render[idx++] = ' ';
            while (idx % ZEN_TAB_STOP != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

/// @brief Allocate space for a new erow, and then copy the given string to a new erow at the end of the E.row array.
/// @param s String to append to the row.
/// @param len Length of the string to append.
void editorAppendRow(char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

/// @brief Freeing the memory owned by the erow.
void editorFreeRow(erow *row)
{
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at)
{
    if (at < 0 || at >= E.numrows)
        return;

    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    
    E.numrows--;
    E.dirty++;
}

/// @brief Append a string to an editor row.
/// @param row Row to which the string needs to be appended.
/// @param s String to append.
/// @param len Size of the string to append.
void editorRowAppendString(erow *row, char *s, size_t len)
{
    // The row’s new size is row->size + len + 1 (including the null byte).
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);

    row->size += len;

    row->chars[row->size] = '\0';

    editorUpdateRow(row);
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

    row->size++;

    row->chars[at] = c;
    editorUpdateRow(row);

    E.dirty++;
}

void editorRowDelChar(erow *row, int at)
{
    if (at < 0 || at >= row->size)
        return;

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);

    row->size--;

    editorUpdateRow(row);

    E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
    {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c); // Insert the character at the cursor position.
    E.cx++;
}

void editorDelChar()
{
    if (E.cy == E.numrows)
        return;

    if (E.cx == 0 && E.cy == 0)
        return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0)
    {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    }
    // If cursor at start of the row, append the rest of the string in row in the previos row.
    else
    {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy); // delete the row that E.cy
        E.cy--;
    }
}

/*** file i/o ***/

/// @brief Converts our array of erow structs into a single string that is ready to be written out to a file.
char *editorRowsToString(int *buflen)
{
    // Add up the lengths of each row of text, adding 1 to each one for the newline character that will be added to the end of each line.
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
    {
        totlen += E.row[j].size + 1;
    }
    *buflen = totlen;

    // Create and copy the contents to the buffer.
    char *buf = malloc(totlen);
    char *p = buf; // p pointer for adding the newline character.
    for (j = 0; j < E.numrows; j++)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

/// @brief Opening and Reading a file from disk
void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    /*
        getline() is useful for reading lines from a file when we don’t know how much memory to allocate for each line.
        It takes care of memory management for you. First, we pass it a null line pointer and a linecap (line capacity) of 0.
        That makes it allocate new memory for the next line it reads, and set line to point to the memory, and set linecap to let you know how much memory it allocated.
        Its return value is the length of the line it read, or -1 if it’s at the end of the file and there are no more lines to read.
    */
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        {
            linelen--;
        }
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);

    E.dirty = 0;
}

void editorSave()
{
    if (E.filename == NULL)
        return;

    // Get string of every row in the file.
    int len;
    char *buf = editorRowsToString(&len);

    // Open and write to the file.
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

    if (fd != -1)
    {
        if (ftruncate(fd, len) != -1) // Sets the file’s size to the specified length.
        {
            if (write(fd, buf, len) == len)
            {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/

// Create our own dynamic string type that supports one operation: appending.
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/// @brief To append a string s to an abuf
/// @param ab Buffer to append the string onto.
/// @param s The String to append.
/// @param len Length of the new string to append in buffer.
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len); // Create a new buffer of size (afub.len + len) and contents of abuf
    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len); // Copy contents of s to to new[ab.len].
    ab->b = new;                   // Make abuf point the new buffer created.
    ab->len += len;
}

/// @brief Deallocates the dynamic memory used by an abuf.
void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** output ***/

void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows)
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    // Vertical Scroll
    //  if the cursor is above the visible window, and if so, scroll up to where the cursor is.
    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    // if the cursor is past the bottom of the visible window, and contains slightly more complicated arithmetic because E.rowoff refers to what’s at the top of the screen,
    // and we have to get E.screenrows involved to talk about what’s at the bottom of the screen.
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    // Horizontal Scroll
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "🤖 Zen text editor -- version %s -- Made by Harsh Kishorani! 🤖", ZEN_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;

                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        /*
            The K command (Erase In Line) erases part of the current line.
            Its argument is analogous to the J command’s argument: 2 erases the whole line, 1 erases the part of the line to the left of the cursor, and 0 erases the part of the line to the right of the cursor.
            0 is the default argument, and that’s what we want, so we leave out the argument and just use <esc>[K
        */
        abAppend(ab, "\x1b[K", 3);

        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    /*
        To make the status bar stand out, we’re going to display it with inverted colors: black text on a white background.
        The escape sequence '<esc>[7m' switches to inverted colors, and '<esc>[m' switches back to normal formatting.

        The m command (Select Graphic Rendition) : http://vt100.net/docs/vt100-ug/chapter3.html#SGR
    */
    abAppend(ab, "\x1b[7m", 4);

    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");

    // Current row number.
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

    if (len > E.screencols)
        len = E.screencols;

    abAppend(ab, status, len);

    while (len < E.screencols)
    {
        // Display current row number at the end of status bar.
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    editorScroll();
    /*
        Intro to Escape Sequences. Consider an example : ("\x1b[2J", 4)

        Write 4 bytes to the terminal.
        1st byte is '\x1b' which is the escape character, or 27 in decimal.
        The other three bytes are '[2J'.

        Escape sequences always start with an escape character (27) followed by a [ character.
        Escape sequences instruct the terminal to do various text formatting tasks, such as coloring text, moving the cursor around, and clearing parts of the screen.

        We are using the J command (Erase In Display) to clear the screen.

        Escape sequence commands take arguments, which come before the command.
        In this case the argument is 2, which says to clear the entire screen.
        <esc>[1J would clear the screen up to where the cursor is, and <esc>[0J would clear the screen from the cursor up to the end of the screen.
        Also, 0 is the default argument for J, so just <esc>[J by itself would also clear the screen from the cursor to the end.

        Here we will be using VT100 Escape sequences : http://vt100.net/docs/vt100-ug/chapter3.html
        We can also use ncurses : https://en.wikipedia.org/wiki/Ncurses
    */
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // Hide the cursor.

    abAppend(&ab, "\x1b[H", 3); // H Command - Reposition it at the top-left corner so that we’re ready to draw the editor interface from top to bottom.

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); // H- Command - Reposition the cursor to the desired location.
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // Reset the cursor (Display it back).

    write(STDOUT_FILENO, ab.b, ab.len); // Write the whole buffer onto the terminal instead of using multiple write statements.
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...)
{
    /*
        The ... argument makes editorSetStatusMessage() a variadic function, meaning it can take any number of arguments.
        C’s way of dealing with these arguments is by having you call va_start() and va_end() on a value of type va_list.
        The last argument before the ... (in this case, fmt) must be passed to va_start(), so that the address of the next arguments is known.
        Then, between the va_start() and va_end() calls, you would call va_arg() and pass it the type of the next argument (which you usually get from the given format string) and it would return the value of that argument.
        In this case, we pass fmt and ap to vsnprintf() and it takes care of reading the format string and calling va_arg() to get each argument.
    */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap); // vsnprintf() helps us make our own printf()-style function
    va_end(ap);
    E.statusmsg_time = time(NULL); // set E.statusmsg_time to the current time
}

/*** input ***/

void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
        {
            E.cx--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size; // Go to end char of prev line if going left out of bound.
        }
        break;
    case ARROW_RIGHT:
        // Only increment cursor if it's not end of the line
        if (row && E.cx < row->size)
        {
            E.cx++;
        }
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
        {
            E.cy++;
        }
        break;
    }

    // Snap cursor to end of line
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

/// @brief Wait for a keypress, and then handle it.
void editorProcessKeypress()
{
    static int quit_times = ZEN_QUIT_TIMES;

    int c = editorReadKey();
    switch (c)
    {
    // 'Enter' Key
    case '\r':
        /* TODO */
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0)
        {
            editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                                   "Press Ctrl-Q %d more times to quit.",
                                   quit_times);
            quit_times--;
            return;
        }
        // Clear the screen on exit
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);

        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        // Move to the end of the line with End
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY)
            editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP)
        {
            E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN)
        {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        editorInsertChar(c);
        break;
    }
}

/*** init ***/

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

    E.screenrows -= 2; // Make room for Status Bar and Status message.
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit || 🤖 Made by Harsh Kishorani. 🤖");

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}