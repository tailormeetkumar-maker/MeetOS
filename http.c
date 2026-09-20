#include "http.h"
#include "tcp_transport.h"

extern void print(const char *text);

#define HTTP_REQUEST_SIZE 1024
#define HTTP_RESPONSE_SIZE 4096

/*
 * MeetOS HTTP client
 *
 * Annabelle.AI communication:
 *
 * MeetOS
 *   ↓
 * HTTP POST
 *   ↓
 * TCP
 *   ↓
 * QEMU network
 *   ↓
 * Python backend
 *
 * A different local TCP source port is used
 * for each HTTP request. This helps avoid
 * problems caused by old TCP connections that
 * are still being closed by the network stack.
 */
static unsigned short next_local_port = 49156;


/* -------------------------------------------------
 * Basic string length
 * ------------------------------------------------- */

static unsigned int string_length(const char *text)
{
    unsigned int length = 0;

    if (text == 0)
        return 0;

    while (text[length] != '\0')
        length++;

    return length;
}


/* -------------------------------------------------
 * Copy string
 * ------------------------------------------------- */

static void copy_string(
    char *destination,
    const char *source,
    unsigned int capacity)
{
    unsigned int i = 0;

    if (destination == 0 ||
        source == 0 ||
        capacity == 0)
    {
        return;
    }

    while (source[i] != '\0' &&
           i < capacity - 1)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}


/* -------------------------------------------------
 * Find HTTP header/body separator
 *
 * Looks for:
 *
 * \r\n\r\n
 * ------------------------------------------------- */

static unsigned int find_header_end(
    const unsigned char *data,
    unsigned int length)
{
    unsigned int i;

    if (data == 0)
        return 0;

    if (length < 4)
        return 0;

    for (i = 0; i <= length - 4; i++)
    {
        if (data[i] == '\r' &&
            data[i + 1] == '\n' &&
            data[i + 2] == '\r' &&
            data[i + 3] == '\n')
        {
            return i + 4;
        }
    }

    return 0;
}


/* -------------------------------------------------
 * Parse Content-Length
 * ------------------------------------------------- */

static unsigned int parse_content_length(
    const unsigned char *data,
    unsigned int header_end)
{
    unsigned int i;
    unsigned int value = 0;

    if (data == 0)
        return 0;

    if (header_end < 15)
        return 0;

    for (i = 0; i + 15 < header_end; i++)
    {
        if (data[i] == 'C' &&
            data[i + 1] == 'o' &&
            data[i + 2] == 'n' &&
            data[i + 3] == 't' &&
            data[i + 4] == 'e' &&
            data[i + 5] == 'n' &&
            data[i + 6] == 't' &&
            data[i + 7] == '-' &&
            data[i + 8] == 'L' &&
            data[i + 9] == 'e' &&
            data[i + 10] == 'n' &&
            data[i + 11] == 'g' &&
            data[i + 12] == 't' &&
            data[i + 13] == 'h' &&
            data[i + 14] == ':')
        {
            i += 15;

            while (i < header_end &&
                   (data[i] == ' ' ||
                    data[i] == '\t'))
            {
                i++;
            }

            while (i < header_end &&
                   data[i] >= '0' &&
                   data[i] <= '9')
            {
                value =
                    value * 10 +
                    (data[i] - '0');

                i++;
            }

            return value;
        }
    }

    return 0;
}


/* -------------------------------------------------
 * Extract HTTP response body
 * ------------------------------------------------- */

static void extract_response_body(
    const unsigned char *response,
    unsigned int response_length,
    char *body,
    unsigned int body_capacity)
{
    unsigned int header_end;
    unsigned int body_length;
    unsigned int content_length;
    unsigned int i;

    if (body == 0 ||
        body_capacity == 0)
    {
        return;
    }

    body[0] = '\0';

    if (response == 0 ||
        response_length == 0)
    {
        return;
    }

    header_end =
        find_header_end(
            response,
            response_length);

    /*
     * If no HTTP header separator is found,
     * treat the whole response as the body.
     */
    if (header_end == 0)
    {
        body_length = response_length;
        header_end = 0;
    }
    else
    {
        body_length =
            response_length - header_end;

        content_length =
            parse_content_length(
                response,
                header_end);

        /*
         * If Content-Length exists, don't copy
         * bytes beyond the declared body.
         */
        if (content_length != 0 &&
            content_length < body_length)
        {
            body_length = content_length;
        }
    }

    if (body_length >= body_capacity)
    {
        body_length = body_capacity - 1;
    }

    for (i = 0; i < body_length; i++)
    {
        body[i] =
            (char)response[header_end + i];
    }

    body[body_length] = '\0';
}


/* -------------------------------------------------
 * Build decimal number
 * ------------------------------------------------- */

static unsigned int append_number(
    char *buffer,
    unsigned int position,
    unsigned int capacity,
    unsigned int value)
{
    char digits[16];

    unsigned int digit_count = 0;
    unsigned int i;

    if (buffer == 0 ||
        position >= capacity)
    {
        return position;
    }

    if (value == 0)
    {
        if (position < capacity - 1)
            buffer[position++] = '0';

        return position;
    }

    while (value > 0 &&
           digit_count < sizeof(digits))
    {
        digits[digit_count++] =
            '0' + (value % 10);

        value /= 10;
    }

    for (i = 0;
         i < digit_count &&
         position < capacity - 1;
         i++)
    {
        buffer[position++] =
            digits[digit_count - 1 - i];
    }

    return position;
}


/* -------------------------------------------------
 * Append text to HTTP request
 * ------------------------------------------------- */

static unsigned int append_text(
    char *buffer,
    unsigned int position,
    unsigned int capacity,
    const char *text)
{
    unsigned int i = 0;

    if (buffer == 0 ||
        text == 0 ||
        position >= capacity)
    {
        return position;
    }

    while (text[i] != '\0' &&
           position < capacity - 1)
    {
        buffer[position++] =
            text[i++];

    }

    return position;
}


/* -------------------------------------------------
 * HTTP POST
 * ------------------------------------------------- */

int http_post(
    const unsigned char *server_ip,
    unsigned short server_port,
    const char *host,
    const char *path,
    const char *json_body,
    char *response_body,
    unsigned int response_capacity)
{
    tcp_connection_t connection;

    unsigned char response[HTTP_RESPONSE_SIZE];

    unsigned char flags;

    unsigned int request_length;
    unsigned int json_length;
    unsigned int received_length;

    unsigned int i;

    char request[HTTP_REQUEST_SIZE];


    /*
     * Validate parameters.
     */

    if (server_ip == 0 ||
        host == 0 ||
        path == 0 ||
        json_body == 0 ||
        response_body == 0 ||
        response_capacity == 0)
    {
        return 0;
    }


    response_body[0] = '\0';


    /*
     * Calculate JSON length.
     */

    json_length =
        string_length(json_body);


    /*
     * Build HTTP request.
     *
     * Example:
     *
     * POST /chat HTTP/1.0
     * Host: 10.0.2.2
     * Content-Type: application/json
     * Connection: close
     * Content-Length: 20
     *
     * {"message":"hello"}
     */

    request_length = 0;

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            "POST ");

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            path);

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            " HTTP/1.0\r\n"
            "Host: ");

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            host);

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            "\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Content-Length: ");

    request_length =
        append_number(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            json_length);

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            "\r\n\r\n");


    /*
     * Add JSON body.
     */

    request_length =
        append_text(
            request,
            request_length,
            HTTP_REQUEST_SIZE,
            json_body);


    request[request_length] = '\0';


    /*
     * Display HTTP client information.
     */

    print("\n");
    
    

    


    /*
     * Use a rotating local source port.
     *
     * First request:
     *     49156
     *
     * Next request:
     *     49157
     *
     * etc.
     *
     * This prevents repeated requests from
     * reusing exactly the same TCP 4-tuple.
     */

    if (!tcp_connect(
            &connection,
            server_ip,
            next_local_port,
            server_port))
    {
        print("TCP connection failed.\n");
        print("------------------------------\n\n");

        /*
         * Advance the port even when connection
         * establishment fails.
         */
        next_local_port++;

        if (next_local_port >= 49200)
            next_local_port = 49156;

        return 0;
    }


    /*
     * Connection succeeded.
     */

    


    /*
     * Advance port for the next request.
     */

    next_local_port++;

    if (next_local_port >= 49200)
        next_local_port = 49156;


    /*
     * Send HTTP request.
     */

    print("Sending POST ");
    print(path);
    print("...\n");

    if (!tcp_send(
            &connection,
            TCP_TRANSPORT_FLAG_ACK |
            TCP_TRANSPORT_FLAG_PSH,
            (const unsigned char *)request,
            request_length))
    {
        print("HTTP POST send failed.\n");
        print("------------------------------\n\n");

        return 0;
    }

    
    


    /*
     * Receive HTTP response.
     */

    received_length = 0;
    flags = 0;

    if (!tcp_receive(
            &connection,
            response,
            HTTP_RESPONSE_SIZE,
            &received_length,
            &flags))
    {
        
        print("------------------------------\n\n");

        return 0;
    }


    /*
     * Empty response.
     */

    if (received_length == 0)
    {
        
        print("------------------------------\n\n");

        return 0;
    }


    /*
     * Extract HTTP body.
     */

    extract_response_body(
        response,
        received_length,
        response_body,
        response_capacity);


    /*
     * The HTTP layer returns the response to ai.c.
     * Do not print it here, otherwise Annabelle's
     * answer appears twice in the chat.
     */

/*
     * Successful response.
     */

    
    
    print("------------------------------\n\n");

    return 1;
}


/* -------------------------------------------------
 * MeetOS "http" command
 * ------------------------------------------------- */

void http_command(char *arguments)
{
    unsigned char backend_ip[4];

    char response[HTTP_RESPONSE_SIZE];


    (void)arguments;


    /*
     * QEMU user-mode networking gateway.
     *
     * Host machine / backend:
     *
     *     10.0.2.2
     */

    backend_ip[0] = 10;
    backend_ip[1] = 0;
    backend_ip[2] = 2;
    backend_ip[3] = 2;


    /*
     * Test HTTP request.
     */

    if (http_post(
            backend_ip,
            8000,
            "10.0.2.2",
            "/chat",
            "{\"message\":\"Hello from MeetOS\"}",
            response,
            sizeof(response)))
    {
        print("HTTP body:\n");
        print(response);
        print("\n");
    }
}
