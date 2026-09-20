#include "tcp.h"
#include "rtl8139.h"
#include "arp.h"

extern void print(const char *text);

#define TCP_PROTOCOL 6

#define LOCAL_IP_0 10
#define LOCAL_IP_1 0
#define LOCAL_IP_2 2
#define LOCAL_IP_3 15

#define SERVER_IP_0 104
#define SERVER_IP_1 20
#define SERVER_IP_2 23
#define SERVER_IP_3 154

/*
 * QEMU user-mode networking gateway.
 */
#define GATEWAY_IP_0 10
#define GATEWAY_IP_1 0
#define GATEWAY_IP_2 2
#define GATEWAY_IP_3 2

#define LOCAL_PORT 49154
#define SERVER_PORT 80

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define HTTP_REQUEST_LENGTH 56

static unsigned char local_ip[4] =
{
    LOCAL_IP_0,
    LOCAL_IP_1,
    LOCAL_IP_2,
    LOCAL_IP_3
};

static unsigned char server_ip[4] =
{
    SERVER_IP_0,
    SERVER_IP_1,
    SERVER_IP_2,
    SERVER_IP_3
};

static unsigned char gateway_ip[4] =
{
    GATEWAY_IP_0,
    GATEWAY_IP_1,
    GATEWAY_IP_2,
    GATEWAY_IP_3
};


/*
 * Fold an Internet checksum.
 */
static unsigned short checksum_fold(
    unsigned long sum)
{
    while (sum >> 16)
    {
        sum =
            (sum & 0xFFFF) +
            (sum >> 16);
    }

    return (unsigned short)(~sum);
}


/*
 * Calculate TCP checksum.
 *
 * Includes:
 *   IPv4 pseudo-header
 *   TCP header
 *   TCP payload
 */
static unsigned short tcp_checksum(
    const unsigned char *tcp,
    unsigned int tcp_length)
{
    unsigned long sum;
    unsigned int i;

    sum = 0;

    /*
     * Source IP.
     */
    sum +=
        ((unsigned short)local_ip[0] << 8) |
        local_ip[1];

    sum +=
        ((unsigned short)local_ip[2] << 8) |
        local_ip[3];

    /*
     * Destination IP.
     */
    sum +=
        ((unsigned short)server_ip[0] << 8) |
        server_ip[1];

    sum +=
        ((unsigned short)server_ip[2] << 8) |
        server_ip[3];

    /*
     * Protocol.
     */
    sum += TCP_PROTOCOL;

    /*
     * TCP length.
     */
    sum += tcp_length;

    /*
     * TCP header + payload.
     */
    for (i = 0; i + 1 < tcp_length; i += 2)
    {
        unsigned short word;

        word =
            ((unsigned short)tcp[i] << 8) |
            tcp[i + 1];

        sum += word;
    }

    /*
     * Odd byte.
     */
    if (tcp_length & 1)
    {
        sum +=
            ((unsigned short)tcp[tcp_length - 1] << 8);
    }

    return checksum_fold(sum);
}


/*
 * Print unsigned decimal number.
 */
static void print_decimal(
    unsigned int value)
{
    char digits[11];
    unsigned int pos;

    pos = 0;

    if (value == 0)
    {
        print("0");
        return;
    }

    while (value > 0)
    {
        digits[pos++] =
            (char)('0' + (value % 10));

        value /= 10;
    }

    while (pos > 0)
    {
        char text[2];

        text[0] =
            digits[--pos];

        text[1] = '\0';

        print(text);
    }
}


/*
 * Print an IPv4 address.
 */
static void print_ip(
    const unsigned char *ip)
{
    unsigned int i;

    for (i = 0; i < 4; i++)
    {
        print_decimal(ip[i]);

        if (i != 3)
            print(".");
    }
}


/*
 * Compare IPv4 addresses.
 */
static int same_ip(
    const unsigned char *a,
    const unsigned char *b)
{
    unsigned int i;

    for (i = 0; i < 4; i++)
    {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}


/*
 * Build and send one TCP packet.
 *
 * This version supports TCP payloads.
 *
 * Ethernet:
 *     14 bytes
 *
 * IPv4:
 *     20 bytes
 *
 * TCP:
 *     20 bytes
 *
 * Payload:
 *     variable
 */
static int send_tcp_packet(
    const unsigned char *destination_mac,
    unsigned short source_port,
    unsigned short destination_port,
    unsigned int sequence_number,
    unsigned int acknowledgement_number,
    unsigned char flags,
    const unsigned char *payload,
    unsigned int payload_length)
{
    unsigned char frame[1600];
    unsigned char local_mac[6];

    unsigned short checksum;

    unsigned int tcp_length;
    unsigned int ip_length;
    unsigned int frame_length;

    unsigned int i;

    /*
     * Prevent oversized packets.
     */
    if (payload_length > 1460)
        return 0;

    tcp_length =
        20 + payload_length;

    ip_length =
        20 + tcp_length;

    frame_length =
        14 + ip_length;

    /*
     * Ethernet minimum frame size.
     */
    if (frame_length < 60)
        frame_length = 60;

    /*
     * Clear frame.
     */
    for (i = 0; i < frame_length; i++)
        frame[i] = 0;

    /*
     * Ethernet destination.
     */
    for (i = 0; i < 6; i++)
    {
        frame[i] =
            destination_mac[i];
    }

    /*
     * Ethernet source.
     */
    rtl8139_get_mac(local_mac);

    for (i = 0; i < 6; i++)
    {
        frame[6 + i] =
            local_mac[i];
    }

    /*
     * EtherType = IPv4.
     */
    frame[12] = 0x08;
    frame[13] = 0x00;

    /*
     * IPv4 version + IHL.
     */
    frame[14] = 0x45;
    frame[15] = 0x00;

    /*
     * IPv4 total length.
     */
    frame[16] =
        (unsigned char)(ip_length >> 8);

    frame[17] =
        (unsigned char)(ip_length & 0xFF);

    /*
     * Identification.
     */
    frame[18] = 0x34;
    frame[19] = 0x56;

    /*
     * Don't Fragment.
     */
    frame[20] = 0x40;
    frame[21] = 0x00;

    /*
     * TTL.
     */
    frame[22] = 64;

    /*
     * Protocol = TCP.
     */
    frame[23] = TCP_PROTOCOL;

    /*
     * IPv4 checksum initially zero.
     */
    frame[24] = 0;
    frame[25] = 0;

    /*
     * Source IP.
     */
    for (i = 0; i < 4; i++)
    {
        frame[26 + i] =
            local_ip[i];
    }

    /*
     * Destination IP.
     */
    for (i = 0; i < 4; i++)
    {
        frame[30 + i] =
            server_ip[i];
    }

    /*
     * Calculate IPv4 checksum.
     */
    {
        unsigned long sum;

        sum = 0;

        for (i = 14; i < 34; i += 2)
        {
            unsigned short word;

            word =
                ((unsigned short)frame[i] << 8) |
                frame[i + 1];

            sum += word;
        }

        checksum =
            checksum_fold(sum);
    }

    frame[24] =
        (unsigned char)(checksum >> 8);

    frame[25] =
        (unsigned char)(checksum & 0xFF);

    /*
     * TCP source port.
     */
    frame[34] =
        (unsigned char)(source_port >> 8);

    frame[35] =
        (unsigned char)(source_port & 0xFF);

    /*
     * TCP destination port.
     */
    frame[36] =
        (unsigned char)(destination_port >> 8);

    frame[37] =
        (unsigned char)(destination_port & 0xFF);

    /*
     * TCP sequence number.
     */
    frame[38] =
        (unsigned char)(sequence_number >> 24);

    frame[39] =
        (unsigned char)(sequence_number >> 16);

    frame[40] =
        (unsigned char)(sequence_number >> 8);

    frame[41] =
        (unsigned char)sequence_number;

    /*
     * TCP acknowledgement number.
     */
    frame[42] =
        (unsigned char)(acknowledgement_number >> 24);

    frame[43] =
        (unsigned char)(acknowledgement_number >> 16);

    frame[44] =
        (unsigned char)(acknowledgement_number >> 8);

    frame[45] =
        (unsigned char)acknowledgement_number;

    /*
     * Data offset = 5 words = 20 bytes.
     */
    frame[46] = 0x50;

    /*
     * TCP flags.
     */
    frame[47] = flags;

    /*
     * Window size.
     */
    frame[48] = 0xFF;
    frame[49] = 0xFF;

    /*
     * TCP checksum initially zero.
     */
    frame[50] = 0;
    frame[51] = 0;

    /*
     * Urgent pointer.
     */
    frame[52] = 0;
    frame[53] = 0;

    /*
     * Copy TCP payload.
     */
    for (i = 0; i < payload_length; i++)
    {
        frame[54 + i] =
            payload[i];
    }

    /*
     * TCP checksum covers:
     *
     * TCP header + payload.
     */
    checksum =
        tcp_checksum(
            &frame[34],
            tcp_length);

    frame[50] =
        (unsigned char)(checksum >> 8);

    frame[51] =
        (unsigned char)(checksum & 0xFF);

    return rtl8139_send(
        frame,
        frame_length);
}


/*
 * Parse TCP SYN-ACK.
 */
static int parse_tcp_synack(
    const unsigned char *frame,
    int length,
    unsigned int our_sequence,
    unsigned int *server_sequence)
{
    unsigned int ip_header_length;
    unsigned int tcp_header_length;

    unsigned short source_port;
    unsigned short destination_port;

    unsigned int sequence_number;
    unsigned int acknowledgement_number;

    unsigned char flags;

    if (length < 54)
        return 0;

    /*
     * Ethernet type = IPv4.
     */
    if (frame[12] != 0x08 ||
        frame[13] != 0x00)
    {
        return 0;
    }

    /*
     * IPv4.
     */
    if ((frame[14] >> 4) != 4)
        return 0;

    ip_header_length =
        (unsigned int)(frame[14] & 0x0F) * 4;

    if (ip_header_length < 20)
        return 0;

    if (length <
        (int)(14 + ip_header_length + 20))
    {
        return 0;
    }

    /*
     * TCP.
     */
    if (frame[23] != TCP_PROTOCOL)
        return 0;

    /*
     * Source IP must be server.
     */
    if (!same_ip(
            &frame[26],
            server_ip))
    {
        return 0;
    }

    /*
     * Destination IP must be us.
     */
    if (!same_ip(
            &frame[30],
            local_ip))
    {
        return 0;
    }

    /*
     * TCP header.
     */
    source_port =
        ((unsigned short)
            frame[14 + ip_header_length] << 8) |
        frame[15 + ip_header_length];

    destination_port =
        ((unsigned short)
            frame[16 + ip_header_length] << 8) |
        frame[17 + ip_header_length];

    if (source_port != SERVER_PORT)
        return 0;

    if (destination_port != LOCAL_PORT)
        return 0;

    /*
     * Sequence number.
     */
    sequence_number =
        ((unsigned int)
            frame[18 + ip_header_length] << 24) |
        ((unsigned int)
            frame[19 + ip_header_length] << 16) |
        ((unsigned int)
            frame[20 + ip_header_length] << 8) |
        frame[21 + ip_header_length];

    /*
     * Acknowledgement number.
     */
    acknowledgement_number =
        ((unsigned int)
            frame[22 + ip_header_length] << 24) |
        ((unsigned int)
            frame[23 + ip_header_length] << 16) |
        ((unsigned int)
            frame[24 + ip_header_length] << 8) |
        frame[25 + ip_header_length];

    /*
     * TCP data offset.
     */
    tcp_header_length =
        (unsigned int)
        ((frame[26 + ip_header_length] >> 4) & 0x0F) * 4;

    if (tcp_header_length < 20)
        return 0;

    /*
     * Flags.
     */
    flags =
        frame[27 + ip_header_length];

    /*
     * SYN + ACK required.
     */
    if ((flags & TCP_FLAG_SYN) == 0)
        return 0;

    if ((flags & TCP_FLAG_ACK) == 0)
        return 0;

    if (flags & TCP_FLAG_RST)
        return 0;

    /*
     * ACK must acknowledge our SYN.
     */
    if (acknowledgement_number !=
        our_sequence + 1)
    {
        return 0;
    }

    *server_sequence =
        sequence_number;

    return 1;
}


/*
 * Parse an incoming TCP data packet.
 *
 * Returns:
 *
 *   1 = valid TCP packet
 *   0 = not our TCP packet
 *
 * Outputs:
 *
 *   server_sequence
 *   acknowledgement_number
 *   payload_offset
 *   payload_length
 */
static int parse_tcp_data(
    const unsigned char *frame,
    int length,
    unsigned int *server_sequence,
    unsigned int *acknowledgement_number,
    unsigned int *payload_offset,
    unsigned int *payload_length,
    unsigned char *flags)
{
    unsigned int ip_header_length;
    unsigned int tcp_header_length;
    unsigned int total_ip_length;

    unsigned short source_port;
    unsigned short destination_port;

    unsigned int tcp_start;
    unsigned int data_start;
    unsigned int data_end;

    if (length < 54)
        return 0;

    /*
     * IPv4 Ethernet type.
     */
    if (frame[12] != 0x08 ||
        frame[13] != 0x00)
    {
        return 0;
    }

    /*
     * IPv4.
     */
    if ((frame[14] >> 4) != 4)
        return 0;

    ip_header_length =
        (unsigned int)(frame[14] & 0x0F) * 4;

    if (ip_header_length < 20)
        return 0;

    /*
     * Read IPv4 total length.
     */
    total_ip_length =
        ((unsigned int)frame[16] << 8) |
        frame[17];

    if (total_ip_length <
        ip_header_length + 20)
    {
        return 0;
    }

    if (length <
        (int)(14 + total_ip_length))
    {
        return 0;
    }

    /*
     * TCP protocol.
     */
    if (frame[23] != TCP_PROTOCOL)
        return 0;

    /*
     * Source IP.
     */
    if (!same_ip(
            &frame[26],
            server_ip))
    {
        return 0;
    }

    /*
     * Destination IP.
     */
    if (!same_ip(
            &frame[30],
            local_ip))
    {
        return 0;
    }

    tcp_start =
        14 + ip_header_length;

    /*
     * Ports.
     */
    source_port =
        ((unsigned short)
            frame[tcp_start] << 8) |
        frame[tcp_start + 1];

    destination_port =
        ((unsigned short)
            frame[tcp_start + 2] << 8) |
        frame[tcp_start + 3];

    if (source_port != SERVER_PORT)
        return 0;

    if (destination_port != LOCAL_PORT)
        return 0;

    /*
     * TCP sequence.
     */
    *server_sequence =
        ((unsigned int)
            frame[tcp_start + 4] << 24) |
        ((unsigned int)
            frame[tcp_start + 5] << 16) |
        ((unsigned int)
            frame[tcp_start + 6] << 8) |
        frame[tcp_start + 7];

    /*
     * TCP acknowledgement.
     */
    *acknowledgement_number =
        ((unsigned int)
            frame[tcp_start + 8] << 24) |
        ((unsigned int)
            frame[tcp_start + 9] << 16) |
        ((unsigned int)
            frame[tcp_start + 10] << 8) |
        frame[tcp_start + 11];

    /*
     * TCP header length.
     */
    tcp_header_length =
        ((unsigned int)
            ((frame[tcp_start + 12] >> 4) & 0x0F)) * 4;

    if (tcp_header_length < 20)
        return 0;

    if (tcp_start + tcp_header_length >
        14 + total_ip_length)
    {
        return 0;
    }

    /*
     * Flags.
     */
    *flags =
        frame[tcp_start + 13];

    /*
     * Payload starts after TCP header.
     */
    data_start =
        tcp_start + tcp_header_length;

    data_end =
        14 + total_ip_length;

    if (data_end < data_start)
        return 0;

    *payload_offset =
        data_start;

    *payload_length =
        data_end - data_start;

    return 1;
}


/*
 * Print an incoming HTTP payload.
 *
 * Only printable ASCII characters are displayed.
 */
static void print_http_payload(
    const unsigned char *data,
    unsigned int length)
{
    unsigned int i;

    for (i = 0; i < length; i++)
    {
        unsigned char c;

        c = data[i];

        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            print("\n");
            continue;
        }

        if (c >= 32 && c <= 126)
        {
            char text[2];

            text[0] = (char)c;
            text[1] = '\0';

            print(text);
        }
    }
}


/*
 * Send a basic HTTP GET request.
 */
static int send_http_get(
    const unsigned char *gateway_mac,
    unsigned int sequence_number,
    unsigned int acknowledgement_number)
{
    static const unsigned char request[] =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    return send_tcp_packet(
        gateway_mac,
        LOCAL_PORT,
        SERVER_PORT,
        sequence_number,
        acknowledgement_number,
        TCP_FLAG_PSH | TCP_FLAG_ACK,
        request,
        HTTP_REQUEST_LENGTH);
}


/*
 * Main TCP command.
 */
void tcp_command(
    char *arguments)
{
    unsigned char gateway_mac[6];
    unsigned char frame[1600];

    unsigned int our_sequence;
    unsigned int server_sequence;

    unsigned int client_sequence;
    unsigned int client_acknowledgement;

    unsigned int received_sequence;
    unsigned int received_acknowledgement;

    unsigned int payload_offset;
    unsigned int payload_length;

    unsigned int timeout;

    unsigned char flags;

    int length;

    (void)arguments;

    print("\nTCP / HTTP\n");
    print("------------------------------\n");

    print("Local IP       : ");
    print_ip(local_ip);
    print("\n");

    print("Server IP      : ");
    print_ip(server_ip);
    print("\n");

    print("Local port     : 49154\n");
    print("Server port    : 80\n");
    print("Protocol       : TCP\n");

    /*
     * Resolve gateway MAC.
     */
    print("Resolving gateway MAC...\n");

    if (!arp_resolve(
            gateway_ip,
            gateway_mac))
    {
        print("ERROR: Could not resolve gateway MAC.\n");
        print("------------------------------\n\n");
        return;
    }

    print("Gateway MAC resolved.\n");

    /*
     * Initial sequence number.
     */
    our_sequence =
        0x10000000;

    server_sequence = 0;

    /*
     * ---------------------------------------------------------
     * STEP 1: SYN
     * ---------------------------------------------------------
     */
    print("Sending TCP SYN...\n");

    if (!send_tcp_packet(
            gateway_mac,
            LOCAL_PORT,
            SERVER_PORT,
            our_sequence,
            0,
            TCP_FLAG_SYN,
            0,
            0))
    {
        print("ERROR: TCP SYN transmission failed.\n");
        print("------------------------------\n\n");
        return;
    }

    print("TCP SYN sent.\n");
    print("Waiting for SYN-ACK...\n");

    /*
     * ---------------------------------------------------------
     * STEP 2: SYN-ACK
     * ---------------------------------------------------------
     */
    timeout = 500000;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {     
              print("\nTCP packet received.\n");        
              if (parse_tcp_synack(
                    frame,
                    length,
                    our_sequence,
                    &server_sequence))
            {
                print("TCP SYN-ACK received.\n");
                break;
            }
        }

        __asm__ volatile ("nop");
    }

    if (timeout == 0)
    {
        print("ERROR: TCP SYN-ACK timeout.\n");
        print("------------------------------\n\n");
        return;
    }

    /*
     * ---------------------------------------------------------
     * STEP 3: ACK
     * ---------------------------------------------------------
     */
    print("Sending TCP ACK...\n");

    if (!send_tcp_packet(
            gateway_mac,
            LOCAL_PORT,
            SERVER_PORT,
            our_sequence + 1,
            server_sequence + 1,
            TCP_FLAG_ACK,
            0,
            0))
    {
        print("ERROR: TCP ACK transmission failed.\n");
        print("------------------------------\n\n");
        return;
    }

    print("TCP ACK sent.\n");

    print("------------------------------\n");
    print("TCP three-way handshake complete!\n");
    print("TCP connection established.\n");

    /*
     * ---------------------------------------------------------
     * STEP 4: HTTP GET
     * ---------------------------------------------------------
     *
     * SYN consumed one sequence number.
     */
    client_sequence =
        our_sequence + 1;

    client_acknowledgement =
        server_sequence + 1;

    print("\nSending HTTP GET...\n");

    if (!send_http_get(
            gateway_mac,
            client_sequence,
            client_acknowledgement))
    {
        print("ERROR: HTTP GET transmission failed.\n");
        print("------------------------------\n\n");
        return;
    }

    /*
     * HTTP request consumes its payload length.
     */
    client_sequence +=
        HTTP_REQUEST_LENGTH;

    print("HTTP GET sent.\n");
    print("Waiting for HTTP response...\n");

    /*
     * ---------------------------------------------------------
     * STEP 5: RECEIVE HTTP RESPONSE
     * ---------------------------------------------------------
     */
    timeout = 1000000;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            if (parse_tcp_data(
                    frame,
                    length,
                    &received_sequence,
                    &received_acknowledgement,
                    &payload_offset,
                    &payload_length,
                    &flags))
            {
                /*
                 * Ignore RST.
                 */
                if (flags & TCP_FLAG_RST)
                {
                    print("\nERROR: Server reset connection.\n");
                    break;
                }

                /*
                 * Ignore packets which do not acknowledge
                 * our HTTP request.
                 */
                if (received_acknowledgement <
                    client_sequence)
                {
                    continue;
                }

                /*
                 * TCP payload received.
                 */
                if (payload_length > 0)
                {
                    print("\n------------------------------\n");
                    print("HTTP RESPONSE\n");
                    print("------------------------------\n");

                    print_http_payload(
                        &frame[payload_offset],
                        payload_length);

                    print("\n------------------------------\n");

                    /*
                     * Acknowledge received TCP data.
                     */
                    client_acknowledgement =
                        received_sequence +
                        payload_length;

                    if (!send_tcp_packet(
                            gateway_mac,
                            LOCAL_PORT,
                            SERVER_PORT,
                            client_sequence,
                            client_acknowledgement,
                            TCP_FLAG_ACK,
                            0,
                            0))
                    {
                        print("ERROR: HTTP ACK transmission failed.\n");
                    }
                    else
                    {
                        print("HTTP data acknowledged.\n");
                    }

                    /*
                     * If FIN is present, acknowledge it too.
                     *
                     * FIN consumes one sequence number.
                     */
                    if (flags & TCP_FLAG_FIN)
                    {
                        client_acknowledgement++;

                        send_tcp_packet(
                            gateway_mac,
                            LOCAL_PORT,
                            SERVER_PORT,
                            client_sequence,
                            client_acknowledgement,
                            TCP_FLAG_ACK,
                            0,
                            0);

                        print("Server FIN acknowledged.\n");
                        print("HTTP connection closed by server.\n");
                        break;
                    }
                }
            }
        }

        __asm__ volatile ("nop");
    }

    if (timeout == 0)
    {
        print("\nERROR: HTTP response timeout.\n");
    }

    print("\nTCP / HTTP test complete.\n");
    print("------------------------------\n\n");
}
