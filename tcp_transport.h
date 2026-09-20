#ifndef TCP_TRANSPORT_H
#define TCP_TRANSPORT_H

#define TCP_TRANSPORT_FLAG_FIN 0x01
#define TCP_TRANSPORT_FLAG_SYN 0x02
#define TCP_TRANSPORT_FLAG_RST 0x04
#define TCP_TRANSPORT_FLAG_PSH 0x08
#define TCP_TRANSPORT_FLAG_ACK 0x10

typedef struct
{
    unsigned char server_ip[4];
    unsigned char gateway_mac[6];

    unsigned short local_port;
    unsigned short server_port;

    unsigned int sequence_number;
    unsigned int acknowledgement_number;

    int connected;
} tcp_connection_t;

/*
 * Establish a TCP connection.
 *
 * Returns:
 *   1 = success
 *   0 = failure
 */
int tcp_connect(
    tcp_connection_t *connection,
    const unsigned char *server_ip,
    unsigned short local_port,
    unsigned short server_port);

/*
 * Send TCP data or control flags.
 *
 * Returns:
 *   1 = success
 *   0 = failure
 */
int tcp_send(
    tcp_connection_t *connection,
    unsigned char flags,
    const unsigned char *payload,
    unsigned int payload_length);

/*
 * Receive one TCP packet belonging to this connection.
 *
 * payload_length receives the number of payload bytes.
 * flags receives the TCP flags.
 *
 * Returns:
 *   1 = packet received
 *   0 = timeout / failure
 */
int tcp_receive(
    tcp_connection_t *connection,
    unsigned char *payload,
    unsigned int payload_capacity,
    unsigned int *payload_length,
    unsigned char *flags);

#endif
