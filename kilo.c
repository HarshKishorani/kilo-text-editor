/*** includes ***/

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

/// @brief Function that prints an error message and exits the program.
void die(const char *s)
{
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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
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
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) // Get current Terminal attributes
        die("tcgetattr");

    // register our disableRawMode() function to be called automatically when the program exits, whether it exits by returning from main(), or by calling the exit() function
    atexit(disableRawMode);
    struct termios raw = orig_termios;

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

/*** init ***/

int main()
{
    enableRawMode();

    while (1)
    {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
            die("read");

        // iscntrl() tests whether a character is a control character.
        // Control characters are nonprintable characters that we don’t want to print to the screen.
        if (iscntrl(c))
        {
            printf("%d\r\n", c);
        }
        else
        {
            printf("%d ('%c')\r\n", c, c); // We are using "\r\n" because we turn off "output processing features" for terminal using "OPOST".
        }

        if (c == 'q')
            break;
    }

    return 0;
}