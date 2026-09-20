#include "roger.h"
#include "tcp_transport.h"

extern void print(const char *text);
extern void clear_screen(void);
extern char keyboard_read(void);

#define VIDEO_MEMORY 0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

#define KEY_NONE   0
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_LEFT   3
#define KEY_RIGHT  4
#define KEY_HOME   5
#define KEY_END    6
#define KEY_DELETE 7

#define ROGER_SERVER_IP_0 192
#define ROGER_SERVER_IP_1 168
#define ROGER_SERVER_IP_2 1
#define ROGER_SERVER_IP_3 4

#define ROGER_SERVER_PORT 9100
#define ROGER_LOCAL_PORT 42000

#define ROGER_MAX_HISTORY 64
#define ROGER_MESSAGE_SIZE 96
#define ROGER_STREAM_SIZE 2048

static volatile unsigned char *video =
    (volatile unsigned char *)VIDEO_MEMORY;

static int roger_active = 0;
static int roger_connected = 0;
static int roger_row = 3;

static tcp_connection_t roger_connection;

typedef struct
{
    char sender[16];
    char message[ROGER_MESSAGE_SIZE];
    int from_ahoy;
} roger_message_t;

static roger_message_t roger_history[ROGER_MAX_HISTORY];
static int roger_history_count = 0;


/* =========================================================
   BASIC STRING HELPERS
   ========================================================= */

static int roger_strlen(const char *text)
{
    int length = 0;

    if (text == 0)
        return 0;

    while (text[length] != '\0')
        length++;

    return length;
}


static void roger_copy(
    char *destination,
    const char *source,
    int maximum
)
{
    int i = 0;

    if (destination == 0 || maximum <= 0)
        return;

    if (source == 0)
    {
        destination[0] = '\0';
        return;
    }

    while (source[i] != '\0' &&
           i < maximum - 1)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}


static int roger_equals(
    const char *a,
    const char *b
)
{
    int i = 0;

    if (a == 0 || b == 0)
        return 0;

    while (a[i] != '\0' &&
           b[i] != '\0')
    {
        if (a[i] != b[i])
            return 0;

        i++;
    }

    return a[i] == '\0' &&
           b[i] == '\0';
}


/* =========================================================
   VGA HELPERS
   ========================================================= */

static void roger_put_at(
    int row,
    int col,
    char c,
    unsigned char attribute
)
{
    int offset;

    if (row < 0 || row >= SCREEN_HEIGHT)
        return;

    if (col < 0 || col >= SCREEN_WIDTH)
        return;

    offset =
        (row * SCREEN_WIDTH + col) * 2;

    video[offset] =
        (unsigned char)c;

    video[offset + 1] =
        attribute;
}


static void roger_write_at(
    int row,
    int col,
    const char *text,
    unsigned char attribute
)
{
    int i = 0;

    if (text == 0)
        return;

    while (text[i] != '\0' &&
           col + i < SCREEN_WIDTH)
    {
        roger_put_at(
            row,
            col + i,
            text[i],
            attribute
        );

        i++;
    }
}


static void roger_fill_row(
    int row,
    unsigned char attribute
)
{
    int col;

    for (col = 0;
         col < SCREEN_WIDTH;
         col++)
    {
        roger_put_at(
            row,
            col,
            ' ',
            attribute
        );
    }
}


/* =========================================================
   CHAT HISTORY
   ========================================================= */

static void roger_store_message(
    const char *sender,
    const char *message,
    int from_ahoy
)
{
    int index;

    if (sender == 0 ||
        message == 0)
    {
        return;
    }

    if (roger_history_count >=
        ROGER_MAX_HISTORY)
    {
        for (index = 1;
             index < ROGER_MAX_HISTORY;
             index++)
        {
            roger_history[index - 1] =
                roger_history[index];
        }

        roger_history_count =
            ROGER_MAX_HISTORY - 1;
    }

    roger_copy(
        roger_history[
            roger_history_count
        ].sender,
        sender,
        sizeof(
            roger_history[
                roger_history_count
            ].sender
        )
    );

    roger_copy(
        roger_history[
            roger_history_count
        ].message,
        message,
        ROGER_MESSAGE_SIZE
    );

    roger_history[
        roger_history_count
    ].from_ahoy = from_ahoy;

    roger_history_count++;
}


static void roger_clear_history(void)
{
    roger_history_count = 0;
}


/* =========================================================
   CHAT UI
   ========================================================= */

static void roger_draw_screen(void)
{
    int col;

    clear_screen();

    /* Header. */
    roger_fill_row(0, 0x70);

    roger_write_at(
        0,
        31,
        "ROGER CHAT",
        0x70
    );

    /* Top border. */
    roger_fill_row(1, 0x07);

    for (col = 0;
         col < SCREEN_WIDTH;
         col++)
    {
        roger_put_at(
            1,
            col,
            '-',
            0x07
        );
    }

    /* Message area. */
    for (col = 2;
         col < 21;
         col++)
    {
        roger_fill_row(
            col,
            0x07
        );
    }

    /* Input separator. */
    roger_fill_row(21, 0x07);

    for (col = 0;
         col < SCREEN_WIDTH;
         col++)
    {
        roger_put_at(
            21,
            col,
            '-',
            0x07
        );
    }

    /* Input line. */
    roger_fill_row(22, 0x07);

    /* Bottom separator. */
    for (col = 0;
         col < SCREEN_WIDTH;
         col++)
    {
        roger_put_at(
            23,
            col,
            '-',
            0x07
        );
    }

    /* Help/status line. */
    roger_fill_row(24, 0x07);

    roger_write_at(
        24,
        2,
        "&roger = leave chat",
        0x07
    );

    roger_row = 2;
}


static void roger_redraw_messages(void)
{
    int row;

    for (row = 2;
         row < 21;
         row++)
    {
        roger_fill_row(
            row,
            0x07
        );
    }

    roger_row = 2;
}


/* =========================================================
   MESSAGE DISPLAY
   ========================================================= */

static void roger_show_message(
    const char *name,
    const char *message,
    int right_side
)
{
    int name_length;
    int message_length;
    int col;

    if (name == 0 ||
        message == 0)
    {
        return;
    }

    name_length =
        roger_strlen(name);

    message_length =
        roger_strlen(message);

    /*
     * Each message uses two rows.
     */
    if (roger_row > 18)
    {
        roger_redraw_messages();
    }

    if (right_side)
    {
        col =
            SCREEN_WIDTH -
            message_length -
            4;

        if (col < 2)
            col = 2;

        roger_write_at(
            roger_row,
            SCREEN_WIDTH -
                name_length -
                3,
            name,
            0x07
        );

        roger_write_at(
            roger_row + 1,
            col,
            message,
            0x07
        );
    }
    else
    {
        roger_write_at(
            roger_row,
            2,
            name,
            0x07
        );

        roger_write_at(
            roger_row + 1,
            2,
            message,
            0x07
        );
    }

    roger_row += 3;
}


static void roger_redraw_history(void)
{
    int start;
    int i;

    roger_redraw_messages();

    if (roger_history_count <= 0)
        return;

    /*
     * The chat area can display approximately
     * six messages at once.
     */
    start =
        roger_history_count - 6;

    if (start < 0)
        start = 0;

    for (i = start;
         i < roger_history_count;
         i++)
    {
        roger_show_message(
            roger_history[i].sender,
            roger_history[i].message,
            roger_history[i].from_ahoy
        );
    }
}


static void roger_add_and_show(
    const char *sender,
    const char *message,
    int from_ahoy
)
{
    roger_store_message(
        sender,
        message,
        from_ahoy
    );

    roger_redraw_history();
}


/* =========================================================
   NETWORK SEND
   ========================================================= */

static int roger_send_line(
    const char *line
)
{
    unsigned char payload[128];
    int length;
    int i;

    if (!roger_connected ||
        line == 0)
    {
        return 0;
    }

    length =
        roger_strlen(line);

    if (length <= 0 ||
        length >=
            (int)sizeof(payload) - 1)
    {
        return 0;
    }

    for (i = 0;
         i < length;
         i++)
    {
        payload[i] =
            (unsigned char)line[i];
    }

    payload[length] = '\n';

    return tcp_send(
        &roger_connection,
        TCP_TRANSPORT_FLAG_PSH |
        TCP_TRANSPORT_FLAG_ACK,
        payload,
        (unsigned int)(length + 1)
    );
}


static int roger_connect(void)
{
    unsigned char server_ip[4];

    server_ip[0] =
        ROGER_SERVER_IP_0;

    server_ip[1] =
        ROGER_SERVER_IP_1;

    server_ip[2] =
        ROGER_SERVER_IP_2;

    server_ip[3] =
        ROGER_SERVER_IP_3;

    roger_connected = 0;

    if (!tcp_connect(
            &roger_connection,
            server_ip,
            ROGER_LOCAL_PORT,
            ROGER_SERVER_PORT))
    {
        return 0;
    }

    roger_connected = 1;

    return 1;
}


/* =========================================================
   HISTORY RECEIVE / SYNCHRONIZATION
   ========================================================= */

static void roger_process_network_line(
    char *line,
    int redraw
)
{
    char *separator1;
    char *separator2;
    char *message;
    int from_ahoy;

    if (line == 0 ||
        line[0] == '\0')
    {
        return;
    }

    if (roger_equals(
            line,
            "HISTORY_END"))
    {
        return;
    }

    /*
     * HISTORY|sender|direction|message
     */
    if (line[0] == 'H' &&
        line[1] == 'I' &&
        line[2] == 'S' &&
        line[3] == 'T' &&
        line[4] == 'O' &&
        line[5] == 'R' &&
        line[6] == 'Y' &&
        line[7] == '|')
    {
        separator1 =
            line + 8;

        separator2 = 0;

        {
            int i = 0;

            while (separator1[i] != '\0')
            {
                if (separator1[i] == '|')
                {
                    separator2 =
                        separator1 + i;

                    break;
                }

                i++;
            }
        }

        if (separator2 == 0)
            return;

        *separator2 = '\0';

        from_ahoy =
            separator2[1] == '1';

        message =
            separator2 + 3;

        roger_store_message(
            separator1,
            message,
            from_ahoy
        );

        if (redraw)
            roger_redraw_history();

        return;
    }

    /*
     * Ordinary message:
     *
     * MATEY|Hello
     * AHOY|Hello
     */
    separator1 = 0;

    {
        int i = 0;

        while (line[i] != '\0')
        {
            if (line[i] == '|')
            {
                separator1 =
                    line + i;

                break;
            }

            i++;
        }
    }

    if (separator1 == 0)
        return;

    *separator1 = '\0';

    message =
        separator1 + 1;

    if (message[0] == '\0')
        return;

    if (roger_equals(
            line,
            "MATEY"))
    {
        roger_add_and_show(
            "MATEY",
            message,
            0
        );
    }
}


static int roger_receive_history(void)
{
    unsigned char payload[1024];
    unsigned int payload_length;
    unsigned char flags;

    char stream[ROGER_STREAM_SIZE];
    int stream_length = 0;

    int finished = 0;
    int received_any = 0;

    stream[0] = '\0';

    /*
     * Ask Matey for its saved conversation.
     */
    if (!roger_send_line("SYNC"))
        return 0;

    while (!finished)
    {
        int result;
        unsigned int i;

        result =
            tcp_receive(
                &roger_connection,
                payload,
                sizeof(payload) - 1,
                &payload_length,
                &flags
            );

        if (!result)
            break;

        if (payload_length == 0)
        {
            if (flags &
                TCP_TRANSPORT_FLAG_FIN)
            {
                break;
            }

            continue;
        }

        received_any = 1;

        for (i = 0;
             i < payload_length;
             i++)
        {
            unsigned char c;

            c =
                (char)payload[i];

            if (stream_length <
                ROGER_STREAM_SIZE - 1)
            {
                stream[stream_length++] =
                    c;

                stream[stream_length] =
                    '\0';
            }

            if (c == '\n')
            {
                int start = 0;
                int j;

                for (j = 0;
                     j < stream_length;
                     j++)
                {
                    if (stream[j] == '\n')
                    {
                        stream[j] =
                            '\0';

                        if (stream[start] !=
                            '\0')
                        {
                            if (roger_equals(
                                    stream + start,
                                    "HISTORY_END"))
                            {
                                finished = 1;
                            }
                            else
                            {
                                roger_process_network_line(
                                    stream + start,
                                    1
                                );
                            }
                        }

                        start = j + 1;
                    }
                }

                /*
                 * Keep any incomplete line.
                 */
                if (start > 0)
                {
                    int remaining =
                        stream_length -
                        start;

                    int k;

                    for (k = 0;
                         k < remaining;
                         k++)
                    {
                        stream[k] =
                            stream[start + k];
                    }

                    stream_length =
                        remaining;

                    stream[
                        stream_length
                    ] = '\0';
                }
            }
        }

        if (flags &
            TCP_TRANSPORT_FLAG_FIN)
        {
            break;
        }
    }

    if (received_any)
        roger_redraw_history();

    return finished;
}


/* =========================================================
   NORMAL NETWORK POLL
   ========================================================= */

void roger_poll(void)
{
    unsigned char payload[512];
    unsigned int payload_length;
    unsigned char flags;

    char stream[1024];

    static int stream_length = 0;
    static char persistent_stream[1024];

    unsigned int i;

    if (!roger_active ||
        !roger_connected)
    {
        return;
    }

    if (stream_length == 0)
    {
        persistent_stream[0] =
            '\0';
    }

    if (!tcp_receive(
            &roger_connection,
            payload,
            sizeof(payload) - 1,
            &payload_length,
            &flags))
    {
        return;
    }

    if (payload_length == 0)
        return;

    /*
     * Copy the persistent stream into a
     * local working buffer.
     */
    for (i = 0;
         i < (unsigned int)stream_length &&
         i < sizeof(stream) - 1;
         i++)
    {
        stream[i] =
            persistent_stream[i];
    }

    stream_length =
        (int)i;

    for (i = 0;
         i < payload_length &&
         stream_length <
             (int)sizeof(stream) - 1;
         i++)
    {
        stream[stream_length++] =
            (char)payload[i];
    }

    stream[stream_length] =
        '\0';

    {
        int start = 0;
        int j;

        for (j = 0;
             j < stream_length;
             j++)
        {
            if (stream[j] == '\n')
            {
                stream[j] =
                    '\0';

                if (stream[start] !=
                    '\0')
                {
                    roger_process_network_line(
                        stream + start,
                        1
                    );
                }

                start =
                    j + 1;
            }
        }

        if (start > 0)
        {
            int remaining =
                stream_length - start;

            int k;

            for (k = 0;
                 k < remaining;
                 k++)
            {
                persistent_stream[k] =
                    stream[start + k];
            }

            stream_length =
                remaining;

            persistent_stream[
                stream_length
            ] = '\0';
        }
        else
        {
            for (j = 0;
                 j < stream_length;
                 j++)
            {
                persistent_stream[j] =
                    stream[j];
            }

            persistent_stream[
                stream_length
            ] = '\0';
        }
    }

    if (flags &
        TCP_TRANSPORT_FLAG_FIN)
    {
        roger_connected = 0;
    }
}


/* =========================================================
   INPUT
   ========================================================= */

void roger_read_line(char *line)
{
    int length = 0;
    int col = 9;
    unsigned char c;

    line[0] = '\0';

    for (col = 0;
         col < SCREEN_WIDTH;
         col++)
    {
        roger_put_at(
            22,
            col,
            ' ',
            0x07
        );
    }

    roger_write_at(
        22,
        2,
        "AHOY > ",
        0x07
    );

    col = 9;

    roger_put_at(
        22,
        col,
        '_',
        0x07
    );

    while (roger_active)
    {
        c = keyboard_read();

        if (c == KEY_NONE)
        {
            /*
             * Do not block here indefinitely.
             * Network receive is handled when
             * keyboard activity causes a poll.
             */
            continue;
        }

        if (c == '\n')
        {
            line[length] =
                '\0';

            if (col < SCREEN_WIDTH)
            {
                roger_put_at(
                    22,
                    col,
                    ' ',
                    0x07
                );
            }

            return;
        }

        if (c == '\b')
        {
            if (length > 0)
            {
                length--;

                line[length] =
                    '\0';

                col =
                    9 + length;

                roger_put_at(
                    22,
                    col,
                    ' ',
                    0x07
                );

                roger_put_at(
                    22,
                    col,
                    '_',
                    0x07
                );
            }

            continue;
        }

        if (c == KEY_UP ||
            c == KEY_DOWN ||
            c == KEY_LEFT ||
            c == KEY_RIGHT ||
            c == KEY_HOME ||
            c == KEY_END ||
            c == KEY_DELETE)
        {
            continue;
        }

        if (c >= 32 &&
            c <= 126)
        {
            if (length <
                68)
            {
                line[length] =
                    c;

                length++;

                line[length] =
                    '\0';

                roger_put_at(
                    22,
                    9 + length - 1,
                    c,
                    0x07
                );

                col =
                    9 + length;

                if (col < SCREEN_WIDTH)
                {
                    roger_put_at(
                        22,
                        col,
                        '_',
                        0x07
                    );
                }
            }
        }
    }

    line[0] = '\0';
}


/* =========================================================
   ROGER CONTROL
   ========================================================= */

void roger_init(void)
{
    roger_active = 0;
    roger_connected = 0;
    roger_clear_history();
}


void roger_command(char *arguments)
{
    char line[96];

    (void)arguments;

    if (roger_active)
        return;

    roger_active = 1;

    roger_draw_screen();

    roger_write_at(
        24,
        42,
        "Connecting...",
        0x07
    );

    /*
     * Connect to the Matey phone.
     */
    if (!roger_connect())
    {
        roger_fill_row(24, 0x07);

        roger_write_at(
            24,
            2,
            "Connection failed - &roger = leave chat",
            0x07
        );

        roger_show_message(
            "SYSTEM",
            "Matey is not reachable.",
            0
        );
    }
    else
    {
        roger_fill_row(24, 0x07);

        roger_write_at(
            24,
            2,
            "Connected to Matey - &roger = leave chat",
            0x07
        );

        /*
         * Synchronize the saved phone history.
         *
         * We keep the existing MeetOS history until
         * we receive the phone's history. This means
         * the conversation remains available during
         * this OS session.
         */
        /*
         * Enter the chat immediately.
         *
         * History synchronization must not happen here because
         * tcp_receive() is blocking. We will handle synchronization
         * separately after the interactive chat is working.
         */
        if (roger_history_count == 0)
        {
            roger_show_message(
                "MATEY",
                "Ahoy!",
                0
            );
        }
    }

    while (roger_active)
    {
        roger_read_line(line);

        if (!roger_active)
            break;

        roger_handle_line(line);
    }
}


void roger_exit(void)
{
    if (!roger_active)
        return;

    roger_active = 0;

    roger_connected = 0;

    clear_screen();

    print(
        "MeetOS console initialized.\n"
    );
}


int roger_is_active(void)
{
    return roger_active;
}


void roger_handle_line(char *line)
{
    char network_line[128];

    if (line == 0)
        return;

    if (line[0] == '&' &&
        line[1] == 'r' &&
        line[2] == 'o' &&
        line[3] == 'g' &&
        line[4] == 'e' &&
        line[5] == 'r' &&
        line[6] == '\0')
    {
        roger_exit();
        return;
    }

    if (line[0] == '\0')
        return;

    /*
     * Display and remember the AHOY message.
     */
    roger_add_and_show(
        "AHOY",
        line,
        1
    );

    /*
     * Send:
     *
     * AHOY|message
     */
    network_line[0] = '\0';

    roger_copy(
        network_line,
        "AHOY|",
        sizeof(network_line)
    );

    {
        int current =
            roger_strlen(network_line);

        int remaining =
            (int)sizeof(network_line) -
            current;

        if (remaining > 1)
        {
            roger_copy(
                network_line + current,
                line,
                remaining
            );
        }
    }

    roger_send_line(
        network_line
    );

    /*
     * Do not call roger_poll() here.
     *
     * roger_poll() uses the blocking tcp_receive()
     * routine. Calling it immediately after every
     * message freezes keyboard input while waiting
     * for network data.
     *
     * Network polling will be moved into a
     * non-blocking Roger event loop.
     */
}
