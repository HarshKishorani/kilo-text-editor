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

enum editorHighlight
{
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)

/*** data ***/

/// @brief struct that will contain all the syntax highlighting information for a particular filetype.
struct editorSyntax
{
    char *filetype;   // The filetype field is the name of the filetype that will be displayed to the user in the status bar.
    char **filematch; // filematch is an array of strings, where each string contains a pattern to match a filename against. If the filename matches, then the file will be recognized as having that filetype.
    int flags;        // flags is a bit field that will contain flags for whether to highlight numbers and whether to highlight strings for that filetype. eg HL_HIGHLIGHT_NUMBERS
};

/// @brief Data type for storing a row of text in our editor.
typedef struct erow
{
    int size;
    int rsize; // Contains the size of the contents of 'render'.
    char *chars;
    char *render;      // Contains the actual characters to draw on the screen for that row of text.
    unsigned char *hl; // store the highlighting of each line in an array
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
    struct editorSyntax *syntax; // When E.syntax is NULL, that means there is no filetype for the current file, and no syntax highlighting should be done
    struct termios orig_termios;
};
struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};

/// @brief HLDB stands for “highlight database”
struct editorSyntax HLDB[] = {
    {"c",
     C_HL_extensions,
     HL_HIGHLIGHT_NUMBERS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

/*** syntax highlighting ***/

/// @brief Takes a character and returns true if it’s considered a separator character.
int is_separator(int c)
{
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row)
{
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL)
        return;

    // 'prev_sep' keeps track of whether the previous character was a separator.
    int prev_sep = 1;

    int i = 0;
    while (i < row->rsize)
    {
        char c = row->render[i];

        // 'prev_hl' is set to the highlight type of the previous character.
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        // To highlight a digit with HL_NUMBER, we now require the previous character to either be a separator, or to also be highlighted with HL_NUMBER.
        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS)
        {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER))
            {
                // Increment i to “consume” that character, set prev_sep to 0 to indicate we are in the middle of highlighting something,
                // and then continue the loop.
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }
}

int editorSyntaxToColor(int hl)
{
    switch (hl)
    {
    case HL_NUMBER:
        return 31;
    case HL_MATCH:
        return 34;
    default:
        return 37;
    }
}

/// @brief Function that tries to match the current filename to one of the filematch fields in the HLDB. If one matches, it’ll set E.syntax to that filetype.
void editorSelectSyntaxHighlight()
{
    E.syntax = NULL;

    if (E.filename == NULL)
        return;

    // strrchr() returns a pointer to the last occurrence of a character in a string
    char *ext = strrchr(E.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
    {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i])
        {
            int is_ext = (s->filematch[i][0] == '.');

            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i])))
            {
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++)
                {
                    editorUpdateSyntax(&E.row[filerow]);
                }

                return;
            }
            i++;
        }
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

int editorRowRxToCx(erow *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
            cur_rx += (ZEN_TAB_STOP - 1) - (cur_rx % ZEN_TAB_STOP);
        cur_rx++;
        if (cur_rx > rx)
            return cx;
    }
    return cx;
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

    editorUpdateSyntax(row);
}

/// @brief Allocate space for a new erow, and then copy the given string to a new erow at the end of the E.row array. Insert a row at the index specified by the new at argument.
void editorInsertRow(int at, char *s, size_t len)
{
    if (at < 0 || at > E.numrows)
        return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

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
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c); // Insert the character at the cursor position.
    E.cx++;
}

void editorInsertNewline()
{
    // If we’re at the beginning of a line, all we have to do is insert a new blank row before the line we’re on.
    if (E.cx == 0)
    {
        editorInsertRow(E.cy, "", 0);
    }
    // Otherwise, we have to split the line we’re on into two rows.
    else
    {
        // First we call editorInsertRow() and pass it the characters on the current row that are to the right of the cursor.
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);

        row = &E.row[E.cy];

        // Then we truncate the current row’s contents by setting its size to the position of the cursor, and we call editorUpdateRow() on the truncated row.
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
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

    editorSelectSyntaxHighlight();

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
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);

    E.dirty = 0;
}

void editorSave()
{
    if (E.filename == NULL)
    {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL)
        {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

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

/*** find ***/

void editorFindCallback(char *query, int key)
{
    /*
        'last_match' will contain the index of the row that the last match was on, or -1 if there was no last match.
        'direction' will store the direction of the search: 1 for searching forward, and -1 for searching backward.
    */
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;
    if (saved_hl)
    {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b')
    {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
    {
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP)
    {
        direction = -1;
    }
    else
    {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1)
        direction = 1;

    // 'current' is the index of the current row we are searching. We start on the last_match row.
    int current = last_match;

    int i;
    for (i = 0; i < E.numrows; i++)
    {
        // If there was a last match, it starts on the line after (or before, if we’re searching backwards).
        // If there wasn’t a last match, it starts at the top of the file and searches in the forward direction to find the first match.
        current += direction;

        // Causes current to go from the end of the file back to the beginning of the file, or vice versa, to allow a search to “wrap around” the end of a file and continue from the top (or bottom).
        if (current == -1)
            current = E.numrows - 1;
        else if (current == E.numrows)
            current = 0;

        // The row to search.
        erow *row = &E.row[current];

        // check if query is a substring of the current row. It returns NULL if there is no match, otherwise it returns a pointer to the matching substring.
        char *match = strstr(row->render, query);

        if (match)
        {
            // When we find a match, we set last_match to current, so that if the user presses the arrow keys, we’ll start the next search from that point.
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);

            // match - row->render is the index into render of the match, so we use that as our index into hl.
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind()
{
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;
    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
    if (query)
    {
        free(query);
    }
    // When the user presses Escape to cancel a search, we want the cursor to go back to where it was when they started the search.
    else
    {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
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

            char *c = &E.row[filerow].render[E.coloff];
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1;

            int j;
            for (j = 0; j < len; j++)
            {
                if (hl[j] == HL_NORMAL)
                {
                    if (current_color != -1)
                    {
                        abAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    abAppend(ab, &c[j], 1);
                }
                else
                {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color)
                    {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
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
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);

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

/// @brief Displays a prompt in the status bar, and lets the user input a line of text after the prompt.
char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();
        int c = editorReadKey();

        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        {
            if (buflen != 0)
                buf[--buflen] = '\0';
        }
        // Allow the user to press Escape to cancel the input prompt.
        else if (c == '\x1b')
        {
            editorSetStatusMessage("");
            if (callback)
                callback(buf, c);
            free(buf);
            return NULL;
        }
        // When the user presses Enter, and their input is not empty, the status message is cleared and their input is returned.
        else if (c == '\r')
        {
            if (buflen != 0)
            {
                editorSetStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        }
        // Otherwise, when they input a printable character, we append it to buf.
        else if (!iscntrl(c) && c < 128)
        {
            // If buflen has reached the maximum capacity we allocated (stored in bufsize), then we double bufsize and allocate that amount of memory before appending to buf.
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback)
            callback(buf, c);
    }
}

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
        editorInsertNewline();
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

    case CTRL_KEY('f'):
        editorFind();
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
    E.syntax = NULL;

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

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find || 🤖 Made by Harsh Kishorani. 🤖");

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}