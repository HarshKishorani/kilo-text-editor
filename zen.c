/*** includes ***/

// Feature test macro : https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
// We add them above our includes, because the header files we’re including use the macros to decide what features to expose.
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>

/*** defines ***/

#define ZEN_VERSION "0.0.1"

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
    char *chars;
} erow;

struct editorConfig
{
    int cx, cy; // Cursor co-ordinates
    int numrows;
    erow *row;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

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
    E.numrows++;
}

/*** file i/o ***/

/// @brief Opening and Reading a file from disk
void editorOpen(char *filename)
{
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

void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        if (y >= E.numrows)
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
            int len = E.row[y].size;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, E.row[y].chars, len);
        }

        /*
            The K command (Erase In Line) erases part of the current line.
            Its argument is analogous to the J command’s argument: 2 erases the whole line, 1 erases the part of the line to the left of the cursor, and 0 erases the part of the line to the right of the cursor.
            0 is the default argument, and that’s what we want, so we leave out the argument and just use <esc>[K
        */
        abAppend(ab, "\x1b[K", 3);

        if (y < E.screenrows - 1)
        {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen()
{
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

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1); // H- Command - Reposition the cursor to the desired location.
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[H", 3);    // Reposition it at the top-left corner so that we’re ready to draw the editor interface from top to bottom.
    abAppend(&ab, "\x1b[?25h", 6); // Reset the cursor (Display it back).

    write(STDOUT_FILENO, ab.b, ab.len); // Write the whole buffer onto the terminal instead of using multiple write statements.
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
        {
            E.cx--;
        }
        break;
    case ARROW_RIGHT:
        if (E.cx != E.screencols - 1)
        {
            E.cx++;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy != E.screenrows - 1)
        {
            E.cy++;
        }
        break;
    }
}

/// @brief Wait for a keypress, and then handle it.
void editorProcessKeypress()
{
    int c = editorReadKey();
    switch (c)
    {
    case CTRL_KEY('q'):
        // Clear the screen on exit
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);

        exit(0);
        break;

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        E.cx = E.screencols - 1;
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
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
    }
}

/*** init ***/

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}