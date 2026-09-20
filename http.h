#ifndef HTTP_H
#define HTTP_H

/*
 * Send an HTTP POST request and return the response body.
 *
 * server_ip:
 *     IPv4 address of the HTTP server.
 *
 * server_port:
 *     TCP port of the HTTP server.
 *
 * host:
 *     HTTP Host header.
 *
 * path:
 *     HTTP request path.
 *
 * json_body:
 *     JSON request body.
 *
 * response_body:
 *     Buffer where the HTTP response body will be stored.
 *
 * response_capacity:
 *     Maximum size of response_body including '\0'.
 *
 * Returns:
 *     1 = success
 *     0 = failure
 */
int http_post(
    const unsigned char *server_ip,
    unsigned short server_port,
    const char *host,
    const char *path,
    const char *json_body,
    char *response_body,
    unsigned int response_capacity
);

/*
 * Terminal test command.
 */
void http_command(char *arguments);

#endif
