#define VIDEO_MEMORY 0xB8000

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define SCREEN_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT)

#define COMMAND_SIZE 128
#define HISTORY_SIZE 8

#define KEY_NONE 0
#define KEY_UP 1
#define KEY_DOWN 2
#define KEY_LEFT 3
#define KEY_RIGHT 4
#define KEY_HOME 5
#define KEY_END 6
#define KEY_DELETE 7

int cursor = 0;

int left_shift = 0;
int right_shift = 0;
int caps_lock = 0;
int num_lock = 1;
int input_start = 0;

char history[HISTORY_SIZE][COMMAND_SIZE];
int history_count = 0;

#include "ai.h"
#include "pci.h"
#include "ac97.h"
#include "rtl8139.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "udp.h"
#include "dns.h"
#include "tcp.h"
#include "http.h"
#include "annabelle_chat.h"
#include "roger.h"

typedef void (*command_handler_t)(char *arguments);

typedef struct
{
    const char *name;
    const char *description;
    command_handler_t handler;
} shell_command_t;

extern shell_command_t command_table[];
extern int command_count;

/* =========================================================
   PORT I/O
   ========================================================= */

void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


/* =========================================================
   STRING FUNCTIONS
   ========================================================= */

int string_equal(const char *a, const char *b)
{
    int i = 0;

    while (a[i] != '\0' && b[i] != '\0')
    {
        if (a[i] != b[i])
            return 0;

        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}


int string_starts_with(const char *a, const char *b)
{
    int i = 0;

    while (b[i] != '\0')
    {
        if (a[i] != b[i])
            return 0;

        i++;
    }

    return 1;
}


int string_length(const char *s)
{
    int i = 0;

    while (s[i] != '\0')
        i++;

    return i;
}


void string_copy(char *dest, const char *src)
{
    int i = 0;

    while (src[i] != '\0')
    {
        dest[i] = src[i];
        i++;
    }

    dest[i] = '\0';
}


char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';

    return c;
}


/* =========================================================
   VGA HARDWARE CURSOR
   ========================================================= */

void update_cursor(void)
{
    unsigned short position;

    if (cursor < 0)
        cursor = 0;

    if (cursor >= SCREEN_SIZE)
        cursor = SCREEN_SIZE - 1;

    position = (unsigned short)cursor;

    outb(0x3D4, 0x0F);
    outb(0x3D5, position & 0xFF);

    outb(0x3D4, 0x0E);
    outb(0x3D5, (position >> 8) & 0xFF);
}


void cursor_shape(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 13);

    outb(0x3D4, 0x0B);
    outb(0x3D5, 15);

    update_cursor();
}


/* =========================================================
   SCREEN
   ========================================================= */

void clear_screen(void)
{
    char *video = (char *)VIDEO_MEMORY;

    int i;

    for (i = 0; i < SCREEN_SIZE; i++)
    {
        video[i * 2] = ' ';
        video[i * 2 + 1] = 0x07;
    }

    cursor = 0;

    update_cursor();
}


void scroll_screen(void)
{
    char *video = (char *)VIDEO_MEMORY;

    int row;
    int col;

    for (row = 1; row < SCREEN_HEIGHT; row++)
    {
        for (col = 0; col < SCREEN_WIDTH; col++)
        {
            video[((row - 1) * SCREEN_WIDTH + col) * 2] =
                video[(row * SCREEN_WIDTH + col) * 2];

            video[((row - 1) * SCREEN_WIDTH + col) * 2 + 1] =
                video[(row * SCREEN_WIDTH + col) * 2 + 1];
        }
    }

    for (col = 0; col < SCREEN_WIDTH; col++)
    {
        video[((SCREEN_HEIGHT - 1) * SCREEN_WIDTH + col) * 2] = ' ';
        video[((SCREEN_HEIGHT - 1) * SCREEN_WIDTH + col) * 2 + 1] = 0x07;
    }

    cursor = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
}


void print_char(char c)
{
    char *video = (char *)VIDEO_MEMORY;

    if (c == '\n')
    {
        cursor = ((cursor / SCREEN_WIDTH) + 1) * SCREEN_WIDTH;
    }
    else if (c == '\b')
    {
        if (cursor > 0)
        {
            cursor--;

            video[cursor * 2] = ' ';
            video[cursor * 2 + 1] = 0x07;
        }
    }
    else
    {
        if (cursor >= SCREEN_SIZE)
            scroll_screen();

        video[cursor * 2] = c;
        video[cursor * 2 + 1] = 0x07;

        cursor++;
    }

    while (cursor >= SCREEN_SIZE)
        scroll_screen();

    update_cursor();
}


void print(const char *text)
{
    int i = 0;

    while (text[i] != '\0')
    {
        print_char(text[i]);
        i++;
    }
}

void print_string(const char *text)
{
    print(text);
}

/* =========================================================
   NUMBER PRINTING
   ========================================================= */

void print_unsigned(unsigned int number)
{
    char buffer[16];

    int i = 0;

    if (number == 0)
    {
        print_char('0');
        return;
    }

    while (number > 0)
    {
        buffer[i] = '0' + (number % 10);
        number /= 10;
        i++;
    }

    while (i > 0)
    {
        i--;
        print_char(buffer[i]);
    }
}


void print_number(int number)
{
    if (number < 0)
    {
        unsigned int value;

        print_char('-');

        value = 0u - (unsigned int)number;

        print_unsigned(value);

        return;
    }

    print_unsigned((unsigned int)number);
}


/* =========================================================
   KEYBOARD
   ========================================================= */

char keyboard_read(void)
{
    unsigned char status;
    unsigned char scancode;
    int shift_pressed;

    status = inb(0x64);

    if (!(status & 1))
        return KEY_NONE;

    scancode = inb(0x60);

    /* Extended keys (E0 prefix). */
    if (scancode == 0xE0)
    {
        while (!(inb(0x64) & 1))
        {
        }

        scancode = inb(0x60);

        /* Numpad Enter. */
        if (scancode == 0x1C)
            return '\n';

        /* Navigation keys from the main keyboard. */
        switch (scancode)
        {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x53: return KEY_DELETE;
            default: return KEY_NONE;
        }
    }

    /* Ignore Pause/Break sequence. */
    if (scancode == 0xE1)
        return KEY_NONE;

    /* Key release. */
    if (scancode & 0x80)
    {
        scancode &= 0x7F;

        if (scancode == 0x2A)
            left_shift = 0;

        if (scancode == 0x36)
            right_shift = 0;

        return KEY_NONE;
    }

    /* Shift. */
    if (scancode == 0x2A)
    {
        left_shift = 1;
        return KEY_NONE;
    }

    if (scancode == 0x36)
    {
        right_shift = 1;
        return KEY_NONE;
    }

    shift_pressed = left_shift || right_shift;

    /* Caps Lock. */
    if (scancode == 0x3A)
    {
        caps_lock = !caps_lock;
        return KEY_NONE;
    }

    /* Num Lock. */
    if (scancode == 0x45)
    {
        num_lock = !num_lock;
        return KEY_NONE;
    }

    /* Backspace. */
    if (scancode == 0x0E)
        return '\b';

    /* Enter. */
    if (scancode == 0x1C)
        return '\n';

    /* Space. */
    if (scancode == 0x39)
        return ' ';

    /* Letters. */
    switch (scancode)
    {
        case 0x1E: return (shift_pressed ^ caps_lock) ? 'A' : 'a';
        case 0x30: return (shift_pressed ^ caps_lock) ? 'B' : 'b';
        case 0x2E: return (shift_pressed ^ caps_lock) ? 'C' : 'c';
        case 0x20: return (shift_pressed ^ caps_lock) ? 'D' : 'd';
        case 0x12: return (shift_pressed ^ caps_lock) ? 'E' : 'e';
        case 0x21: return (shift_pressed ^ caps_lock) ? 'F' : 'f';
        case 0x22: return (shift_pressed ^ caps_lock) ? 'G' : 'g';
        case 0x23: return (shift_pressed ^ caps_lock) ? 'H' : 'h';
        case 0x17: return (shift_pressed ^ caps_lock) ? 'I' : 'i';
        case 0x24: return (shift_pressed ^ caps_lock) ? 'J' : 'j';
        case 0x25: return (shift_pressed ^ caps_lock) ? 'K' : 'k';
        case 0x26: return (shift_pressed ^ caps_lock) ? 'L' : 'l';
        case 0x32: return (shift_pressed ^ caps_lock) ? 'M' : 'm';
        case 0x31: return (shift_pressed ^ caps_lock) ? 'N' : 'n';
        case 0x18: return (shift_pressed ^ caps_lock) ? 'O' : 'o';
        case 0x19: return (shift_pressed ^ caps_lock) ? 'P' : 'p';
        case 0x10: return (shift_pressed ^ caps_lock) ? 'Q' : 'q';
        case 0x13: return (shift_pressed ^ caps_lock) ? 'R' : 'r';
        case 0x1F: return (shift_pressed ^ caps_lock) ? 'S' : 's';
        case 0x14: return (shift_pressed ^ caps_lock) ? 'T' : 't';
        case 0x16: return (shift_pressed ^ caps_lock) ? 'U' : 'u';
        case 0x2F: return (shift_pressed ^ caps_lock) ? 'V' : 'v';
        case 0x11: return (shift_pressed ^ caps_lock) ? 'W' : 'w';
        case 0x2D: return (shift_pressed ^ caps_lock) ? 'X' : 'x';
        case 0x15: return (shift_pressed ^ caps_lock) ? 'Y' : 'y';
        case 0x2C: return (shift_pressed ^ caps_lock) ? 'Z' : 'z';
    }

    /* Number row and punctuation. */
    switch (scancode)
    {
        case 0x02: return shift_pressed ? '!' : '1';
        case 0x03: return shift_pressed ? '@' : '2';
        case 0x04: return shift_pressed ? '#' : '3';
        case 0x05: return shift_pressed ? '$' : '4';
        case 0x06: return shift_pressed ? '%' : '5';
        case 0x07: return shift_pressed ? '^' : '6';
        case 0x08: return shift_pressed ? '&' : '7';
        case 0x09: return shift_pressed ? '*' : '8';
        case 0x0A: return shift_pressed ? '(' : '9';
        case 0x0B: return shift_pressed ? ')' : '0';
        case 0x0C: return shift_pressed ? '_' : '-';
        case 0x0D: return shift_pressed ? '+' : '=';
        case 0x1A: return shift_pressed ? '{' : '[';
        case 0x1B: return shift_pressed ? '}' : ']';
        case 0x27: return shift_pressed ? ':' : ';';
        case 0x28: return shift_pressed ? '"' : '\'';
        case 0x29: return shift_pressed ? '~' : '`';
        case 0x2B: return shift_pressed ? '|' : '\\';
        case 0x33: return shift_pressed ? '<' : ',';
        case 0x34: return shift_pressed ? '>' : '.';
        case 0x35: return shift_pressed ? '?' : '/';
    }

    /*
       Numeric keypad.
       PS/2 Set-1 scancodes:
       7 8 9 = 47 48 49
       4 5 6 = 4B 4C 4D
       1 2 3 = 4F 50 51
       0     = 52
       .     = 53
    */
    if (num_lock)
    {
        switch (scancode)
        {
            case 0x47: return '7';
            case 0x48: return '8';
            case 0x49: return '9';
            case 0x4B: return '4';
            case 0x4C: return '5';
            case 0x4D: return '6';
            case 0x4F: return '1';
            case 0x50: return '2';
            case 0x51: return '3';
            case 0x52: return '0';
            case 0x53: return '.';
        }
    }
    else
    {
        switch (scancode)
        {
            case 0x47: return KEY_HOME;
            case 0x48: return KEY_UP;
            case 0x49: return KEY_END;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x4F: return KEY_END;
            case 0x50: return KEY_DOWN;
            case 0x51: return KEY_END;
            case 0x53: return KEY_DELETE;
        }
    }

    /* Numpad operators. */
    switch (scancode)
    {
        case 0x4A: return '-';
        case 0x4E: return '+';
        case 0x37: return '*';
        case 0x35: return '/';
    }

    return KEY_NONE;
}


/* =========================================================
   DIRECT VGA STRING DRAW
   ========================================================= */

void write_at(int position, char c)
{
    char *video = (char *)VIDEO_MEMORY;

    if (position < 0 || position >= SCREEN_SIZE)
        return;

    video[position * 2] = c;
    video[position * 2 + 1] = 0x07;
}


void erase_at(int position)
{
    write_at(position, ' ');
}


/* =========================================================
   COMMAND HISTORY
   ========================================================= */

void add_history(const char *command)
{
    int i;

    if (command[0] == '\0')
        return;

    if (history_count > 0)
    {
        if (string_equal(history[history_count - 1], command))
            return;
    }

    if (history_count < HISTORY_SIZE)
    {
        string_copy(history[history_count], command);
        history_count++;
        return;
    }

    for (i = 1; i < HISTORY_SIZE; i++)
        string_copy(history[i - 1], history[i]);

    string_copy(history[HISTORY_SIZE - 1], command);
}


/* =========================================================
   REDRAW COMMAND LINE
   ========================================================= */

void redraw_command_line(
    int start,
    char *command,
    int length,
    int edit_position,
    int old_length
)
{
    int i;
    int max_length;
    int command_cells;
    int needed_rows;
    int start_row;
    int start_col;
    int visible_start;
    int new_cursor;

    (void)old_length;

    /*
       The command is allowed to wrap across VGA rows.  The old
       implementation treated the command as one flat row and could
       scroll the display without keeping the cursor aligned with it.
    */
    start_row = start / SCREEN_WIDTH;
    start_col = start % SCREEN_WIDTH;

    command_cells = start_col + length;
    needed_rows = (command_cells + SCREEN_WIDTH - 1) / SCREEN_WIDTH;
    if (needed_rows < 1)
        needed_rows = 1;

    /* Keep the whole editable command inside the terminal viewport. */
    while (start_row + needed_rows > SCREEN_HEIGHT)
    {
        scroll_screen();
        start_row--;

        if (start_row < 0)
            start_row = 0;
    }

    start = start_row * SCREEN_WIDTH + start_col;
    input_start = start;

    max_length = length;
    if (old_length > max_length)
        max_length = old_length;

    /* Redraw the complete command, including wrapped rows. */
    for (i = 0; i < max_length; i++)
    {
        int cell = start + i;

        if (cell >= SCREEN_SIZE)
            break;

        if (i < length)
            write_at(cell, command[i]);
        else
            erase_at(cell);
    }

    new_cursor = start + edit_position;

    /* If the cursor itself is at the bottom edge, scroll it into view. */
    while (new_cursor >= SCREEN_SIZE)
    {
        scroll_screen();
        start -= SCREEN_WIDTH;
        new_cursor -= SCREEN_WIDTH;

        if (start < 0)
            start = 0;
    }

    input_start = start;
    cursor = new_cursor;

    /* Make sure the cursor remains visible on screen. */
    visible_start = cursor / SCREEN_WIDTH;
    if (visible_start < 0)
        cursor = 0;

    update_cursor();
}

int load_history(
    int index,
    char *command,
    int *length
)
{
    if (index < 0 || index >= history_count)
        return 0;

    string_copy(command, history[index]);

    *length = string_length(command);

    return 1;
}


/* =========================================================
   SHELL INPUT
   ========================================================= */

void read_command(char *command)
{
    int length = 0;
    int position = 0;

    int history_position = history_count;

    int old_length;

    char c;

    if (roger_is_active())
        print("Matey> ");
    else
        print("MeetOS> ");

    input_start = cursor;

    command[0] = '\0';


    while (1)
    {
        c = keyboard_read();

        if (c == KEY_NONE)
            continue;


        /* =================================================
           ENTER
           ================================================= */

        if (c == '\n')
        {
            command[length] = '\0';

            cursor = input_start + length;

            update_cursor();

            print("\n");

            return;
        }


        /* =================================================
           BACKSPACE
           ================================================= */

        if (c == '\b')
        {
            if (position > 0)
            {
                int i;

                old_length = length;

                for (i = position - 1; i < length - 1; i++)
                    command[i] = command[i + 1];

                length--;
                position--;

                command[length] = '\0';

                redraw_command_line(
                    input_start,
                    command,
                    length,
                    position,
                    old_length
                );
            }

            continue;
        }


        /* =================================================
           DELETE
           ================================================= */

        if (c == KEY_DELETE)
        {
            if (position < length)
            {
                int i;

                old_length = length;

                for (i = position; i < length - 1; i++)
                    command[i] = command[i + 1];

                length--;

                command[length] = '\0';

                redraw_command_line(
                    input_start,
                    command,
                    length,
                    position,
                    old_length
                );
            }

            continue;
        }


        /* =================================================
           LEFT
           ================================================= */

        if (c == KEY_LEFT)
        {
            if (position > 0)
            {
                position--;

                cursor = input_start + position;

                update_cursor();
            }

            continue;
        }


        /* =================================================
           RIGHT
           ================================================= */

        if (c == KEY_RIGHT)
        {
            if (position < length)
            {
                position++;

                cursor = input_start + position;

                update_cursor();
            }

            continue;
        }


        /* =================================================
           HOME
           ================================================= */

        if (c == KEY_HOME)
        {
            position = 0;

            cursor = input_start;

            update_cursor();

            continue;
        }


        /* =================================================
           END
           ================================================= */

        if (c == KEY_END)
        {
            position = length;

            cursor = input_start + position;

            update_cursor();

            continue;
        }


        /* =================================================
           UP
           ================================================= */

        if (c == KEY_UP)
        {
            if (history_count > 0 && history_position > 0)
            {
                history_position--;

                old_length = length;

                load_history(
                    history_position,
                    command,
                    &length
                );

                position = length;

                redraw_command_line(
                    input_start,
                    command,
                    length,
                    position,
                    old_length
                );
            }

            continue;
        }


        /* =================================================
           DOWN
           ================================================= */

        if (c == KEY_DOWN)
        {
            if (history_position < history_count - 1)
            {
                history_position++;

                old_length = length;

                load_history(
                    history_position,
                    command,
                    &length
                );

                position = length;

                redraw_command_line(
                    input_start,
                    command,
                    length,
                    position,
                    old_length
                );
            }
            else if (history_position == history_count - 1)
            {
                history_position = history_count;

                old_length = length;

                length = 0;
                position = 0;

                command[0] = '\0';

                redraw_command_line(
                    input_start,
                    command,
                    length,
                    position,
                    old_length
                );
            }

            continue;
        }


        /* =================================================
           NORMAL CHARACTER
           ================================================= */

        if (c >= 32 && c <= 126)
        {
            if (length < COMMAND_SIZE - 1)
            {
                int i;

                old_length = length;

                for (i = length; i > position; i--)
                    command[i] = command[i - 1];

                command[position] = c;

                length++;
                position++;

                command[length] = '\0';

                redraw_command_line(
                    input_start,
                    command,
                    length,
                    position,
                    old_length
                );
            }
        }
    }
}


/* =========================================================
   CALCULATOR
   ========================================================= */

void calc_skip_spaces(char **text)
{
    while (**text == ' ')
        (*text)++;
}


int calc_parse_number(
    char **text,
    int *success
)
{
    int number = 0;
    int found = 0;

    calc_skip_spaces(text);

    while (**text >= '0' && **text <= '9')
    {
        number = number * 10 +
                 (**text - '0');

        (*text)++;

        found = 1;
    }

    if (!found)
    {
        *success = 0;
        return 0;
    }

    return number;
}


int calc_power(int base, int exponent)
{
    int result = 1;
    int i;

    if (exponent < 0)
        return 0;

    for (i = 0; i < exponent; i++)
        result *= base;

    return result;
}


int calc_parse_expression(
    char **text,
    int *success
);


int calc_parse_factor(
    char **text,
    int *success
)
{
    int value;
    int sign = 1;

    calc_skip_spaces(text);

    if (**text == '-')
    {
        sign = -1;
        (*text)++;
    }
    else if (**text == '+')
    {
        (*text)++;
    }

    calc_skip_spaces(text);

    if (**text == '(')
    {
        (*text)++;

        value = calc_parse_expression(
            text,
            success
        );

        calc_skip_spaces(text);

        if (**text != ')')
        {
            *success = 0;
            return 0;
        }

        (*text)++;

        return value * sign;
    }

    value = calc_parse_number(
        text,
        success
    );

    return value * sign;
}


int calc_parse_power(
    char **text,
    int *success
)
{
    int left;
    int right;

    left = calc_parse_factor(
        text,
        success
    );

    if (!*success)
        return 0;

    calc_skip_spaces(text);

    if (**text == '^')
    {
        (*text)++;

        right = calc_parse_power(
            text,
            success
        );

        if (!*success)
            return 0;

        if (right < 0)
        {
            *success = 0;
            return 0;
        }

        left = calc_power(
            left,
            right
        );
    }

    return left;
}


int calc_parse_term(
    char **text,
    int *success
)
{
    int left;
    int right;

    char operation;

    left = calc_parse_power(
        text,
        success
    );

    if (!*success)
        return 0;

    while (1)
    {
        calc_skip_spaces(text);

        operation = **text;

        if (operation != '*' &&
            operation != '/' &&
            operation != '%')
        {
            break;
        }

        (*text)++;

        right = calc_parse_power(
            text,
            success
        );

        if (!*success)
            return 0;

        if (operation == '*')
        {
            left *= right;
        }
        else if (operation == '/')
        {
            if (right == 0)
            {
                print("Error: division by zero.\n");
                *success = 0;
                return 0;
            }

            left /= right;
        }
        else
        {
            if (right == 0)
            {
                print("Error: division by zero.\n");
                *success = 0;
                return 0;
            }

            left %= right;
        }
    }

    return left;
}


int calc_parse_expression(
    char **text,
    int *success
)
{
    int left;
    int right;

    char operation;

    left = calc_parse_term(
        text,
        success
    );

    if (!*success)
        return 0;

    while (1)
    {
        calc_skip_spaces(text);

        operation = **text;

        if (operation != '+' &&
            operation != '-')
        {
            break;
        }

        (*text)++;

        right = calc_parse_term(
            text,
            success
        );

        if (!*success)
            return 0;

        if (operation == '+')
            left += right;
        else
            left -= right;
    }

    return left;
}


/* =========================================================
   CALCULATOR COMMAND
   ========================================================= */

void calculate(char *expression)
{
    char *text;

    int success;
    int result;

    text = expression;

    calc_skip_spaces(&text);

    if (*text == '\0')
    {
        print("\nCalculator\n");
        print("--------------------\n");
        print("Operators:\n");
        print("+  Addition\n");
        print("-  Subtraction\n");
        print("*  Multiplication\n");
        print("/  Division\n");
        print("%  Remainder\n");
        print("^  Power\n");
        print("( ) Parentheses\n\n");

        print("Examples:\n");
        print("calc 10 + 5\n");
        print("calc 10 + 5 * 2\n");
        print("calc (10 + 5) * 2\n");
        print("2 ^ 8\n\n");

        return;
    }

    success = 1;

    result = calc_parse_expression(
        &text,
        &success
    );

    calc_skip_spaces(&text);

    if (!success || *text != '\0')
    {
        print("Calculator error: invalid expression.\n\n");
        return;
    }

    print("Result: ");
    print_number(result);
    print("\n\n");
}


/* =========================================================
   COMMAND HELP
   ========================================================= */

void command_help(void)
{
    int i;

    print("\n");
    print("MeetOS commands:\n\n");

    for (i = 0; i < command_count; i++)
    {
        print("  ");
        print(command_table[i].name);
        print("       ");
        print(command_table[i].description);
        print("\n");
    }

    print("\nCalculator operators:\n");
    print("  +  -  *  /  %  ^  ( )\n\n");

    print("Examples:\n");
    print("  2 + 3\n");
    print("  2 ^ 2\n");
    print("  10 + 5 * 2\n");
    print("  (10 + 5) * 2\n");
    print("  -10 + 20\n");
    print("  calc 17 % 5\n\n");
}

/* =========================================================
   ABOUT
   ========================================================= */

void command_about(void)
{
    print("\n");
    print("MeetOS\n");
    print("------------------------------\n");
    print("Educational operating system\n");
    print("Architecture : x86 32-bit\n");
    print("Kernel       : C\n");
    print("Bootloader   : Assembly\n");
    print("Shell        : MeetShell\n");
    print("Input        : PS/2 keyboard\n");
    print("Display      : VGA text mode\n");
    print("------------------------------\n\n");
}


/* =========================================================
   VERSION
   ========================================================= */

void command_version(void)
{
    print("\n");
    print("MeetOS version 0.4\n");
    print("Architecture: x86 32-bit\n");
    print("Kernel: C\n");
    print("Shell: MeetShell\n");
    print("Calculator: Expression parser\n");
    print("Annabelle.AI: Subsystem interface\n\n");
}


/* =========================================================
   UNAME
   ========================================================= */

void command_uname(void)
{
    print("MeetOS x86_32\n\n");
}


/* =========================================================
   WHOAMI
   ========================================================= */

void command_whoami(void)
{
    print("meet\n\n");
}


/* =========================================================
   ECHO
   ========================================================= */

void command_echo(char *arguments)
{
    print(arguments);
    print("\n\n");
}


/* =========================================================
   ANNABELLE.AI INTERFACE
   ========================================================= */

void command_prompt(char *input)
{
    ai_prompt(input);
}


void command_ai(char *arguments)
{
    (void)arguments;
    ai_print_status();
}


void command_historyai(char *arguments)
{
    (void)arguments;
    ai_print_history();
}


/* =========================================================
   MEETSHELL COMMAND REGISTRY
   ========================================================= */

void command_help_wrapper(char *arguments)
{
    (void)arguments;
    command_help();
}


void command_about_wrapper(char *arguments)
{
    (void)arguments;
    command_about();
}


void command_version_wrapper(char *arguments)
{
    (void)arguments;
    command_version();
}


void command_clear(char *arguments)
{
    (void)arguments;
    clear_screen();
}


void command_calc_wrapper(char *arguments)
{
    calculate(arguments);
}


void command_uname_wrapper(char *arguments)
{
    (void)arguments;
    command_uname();
}


void command_whoami_wrapper(char *arguments)
{
    (void)arguments;
    command_whoami();
}



void command_netrx(char *arguments)
{
    unsigned char frame[1600];
    int length;

    (void)arguments;

    print("\nEthernet RX\n");
    print("------------------------------\n");

    length = rtl8139_receive(frame, sizeof(frame));

    if (length == 0)
    {
        print("No Ethernet packet available.\n");
    }
    else
    {
        print("Packet received!\n");
        print("Frame length : ");

        {
            char digits[8];
            unsigned int value;
            unsigned int pos;

            value = (unsigned int)length;
            pos = 0;

            while (value > 0)
            {
                digits[pos++] =
                    (char)('0' + (value % 10));

                value /= 10;
            }

            if (pos == 0)
                print("0");

            while (pos > 0)
            {
                char text[2];

                text[0] = digits[--pos];
                text[1] = '\0';

                print(text);
            }
        }

        print(" bytes\n");
    }

    print("------------------------------\n\n");
}

void command_reboot(char *arguments)
{
    (void)arguments;

    print("Restarting MeetOS...\n");

    while (inb(0x64) & 0x02)
    {
    }

    outb(0x64, 0xFE);

    while (1)
    {
    }
}


/*
   Adding a new shell command only requires another entry here.
   Keyboard editing remains completely separate from command
   dispatch.
*/
shell_command_t command_table[] =
{
    {"help",      "Show this help",                 command_help_wrapper},
    {"about",     "About MeetOS",                   command_about_wrapper},
    {"version",   "Show version",                   command_version_wrapper},
    {"clear",     "Clear screen",                   command_clear},
    {"cls",       "Clear screen",                   command_clear},
    {"echo",      "Print text",                     command_echo},
    {"calc",      "Calculator",                     command_calc_wrapper},
    {"calculator","Calculator",                     command_calc_wrapper},
    {"uname",     "System information",             command_uname_wrapper},
    {"whoami",    "Current user",                   command_whoami_wrapper},
    { "net", "Show network status", rtl8139_status },
    { "netsend", "Send Ethernet test frame", ethernet_test },
    {"netrx",     "Receive Ethernet frame",          command_netrx},
    {"arp",       "Resolve IP using ARP",             arp_command},
    {"ipv4",      "Test IPv4 communication",          ipv4_command},
    {"udp",       "Test UDP communication",            udp_command},
    {"dns",       "Resolve a domain using DNS",         dns_command},
    {"tcp",       "Test TCP communication",            tcp_command},
    {"http",      "Test HTTP communication",           http_command},    
    {"prompt",    "Talk to Annabelle.AI",           command_prompt},
    {"&annabelle", "Enter Annabelle.AI chat",        annabelle_chat_command},
    {"!roger",    "Enter Matey acoustic chat",      roger_command},
    {"!ac97test", "Test AC97 microphone capture",  ac97_test_command},
    {"ai",        "Annabelle.AI status",            command_ai},
    {"historyai", "Annabelle.AI dialogue history",   command_historyai},
    {"reboot",    "Restart MeetOS",                 command_reboot}
};

int command_count = sizeof(command_table) / sizeof(command_table[0]);


int looks_like_expression(char *input)
{
    char c;
    int i = 0;

    while (input[i] == ' ')
        i++;

    c = input[i];

    if ((c >= '0' && c <= '9') ||
        c == '(' ||
        c == '-' ||
        c == '+')
    {
        return 1;
    }

    return 0;
}


void execute_command(char *input)
{
    char command[32];
    char *arguments;
    int i = 0;
    int j;

    while (input[i] == ' ')
        i++;

    if (input[i] == '\0')
        return;

    if (looks_like_expression(input + i))
    {
        calculate(input + i);
        return;
    }

    while (input[i] != '\0' &&
           input[i] != ' ' &&
           i < 31)
    {
        command[i] = lower_char(input[i]);
        i++;
    }

    command[i] = '\0';

    arguments = input + i;

    while (*arguments == ' ')
        arguments++;

    for (j = 0; j < command_count; j++)
    {
        if (string_equal(command, command_table[j].name))
        {
            command_table[j].handler(arguments);
            return;
        }
    }

    print("Unknown command: ");
    print(command);
    print("\nType 'help' for available commands.\n\n");
}

/* =========================================================
   SHELL
   ========================================================= */

void shell(void)
{
    char command[COMMAND_SIZE];

    while (1)
    {
        if (roger_is_active())
        {
            roger_read_line(command);
            roger_handle_line(command);
        }
        else
        {
            read_command(command);

            add_history(command);
            execute_command(command);
        }

        roger_poll();
    }
}


/* =========================================================
   KERNEL ENTRY
   ========================================================= */

__attribute__((section(".text.entry")))
void kernel_main(void)
{
    clear_screen();

    ai_init();
    pci_init();
ac97_init();    
    rtl8139_init();
    ethernet_init();
    cursor_shape();

    print("Welcome to MeetOS!\n");
    print("Our kernel is running in 32-bit protected mode.\n");
    print("MeetOS console initialized.\n");
    print("Annabelle.AI subsystem initialized.\n\n");

    print("Type 'help' to see available commands.\n\n");

    shell();

    while (1)
    {
    }
}
