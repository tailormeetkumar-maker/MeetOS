#include "tcp_transport.h"
#include "rtl8139.h"
#include "arp.h"

extern void print(const char *text);

#define TCP_PROTOCOL 6

#define LOCAL_IP_0 10
#define LOCAL_IP_1 0
#define LOCAL_IP_2 2
#define LOCAL_IP_3 15

#define GATEWAY_IP_0 10
#define GATEWAY_IP_1 0
#define GATEWAY_IP_2 2
#define GATEWAY_IP_3 2

#define TCP_MAX_PAYLOAD 1460
#define TCP_FRAME_SIZE 1600

#define TCP_TIMEOUT 500000

static unsigned char local_ip[4] =
{
    LOCAL_IP_0,
    LOCAL_IP_1,
    LOCAL_IP_2,
    LOCAL_IP_3
};

static unsigned char gateway_ip[4] =
{
    GATEWAY_IP_0,
    GATEWAY_IP_1,
    GATEWAY_IP_2,
    GATEWAY_IP_3
};

static unsigned int next_sequence =
    0x10000000;

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

static unsigned short ipv4_checksum(
    const unsigned char *header)
{
    unsigned long sum;
    unsigned int i;

    sum = 0;

    for (i = 0; i < 20; i += 2)
    {
        unsigned short word;

        word =
            ((unsigned short)header[i] << 8) |
            header[i + 1];

        sum += word;
    }

    return checksum_fold(sum);
}

static unsigned short tcp_checksum(
    const unsigned char *server_ip,
    const unsigned char *tcp,
    unsigned int tcp_length)
{
    unsigned long sum;
    unsigned int i;

    sum = 0;

    sum +=
        ((unsigned short)local_ip[0] << 8) |
        local_ip[1];

    sum +=
        ((unsigned short)local_ip[2] << 8) |
        local_ip[3];

    sum +=
        ((unsigned short)server_ip[0] << 8) |
        server_ip[1];

    sum +=
        ((unsigned short)server_ip[2] << 8) |
        server_ip[3];

    sum += TCP_PROTOCOL;
    sum += tcp_length;

    for (i = 0; i + 1 < tcp_length; i += 2)
    {
        unsigned short word;

        word =
            ((unsigned short)tcp[i] << 8) |
            tcp[i + 1];

        sum += word;
    }

    if (tcp_length & 1)
    {
        sum +=
            ((unsigned short)tcp[tcp_length - 1] << 8);
    }

    return checksum_fold(sum);
}

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

static unsigned int read_u32(
    const unsigned char *data)
{
    return
        ((unsigned int)data[0] << 24) |
        ((unsigned int)data[1] << 16) |
        ((unsigned int)data[2] << 8) |
        data[3];
}

static int send_packet(
    tcp_connection_t *connection,
    unsigned char flags,
    const unsigned char *payload,
    unsigned int payload_length)
{
    unsigned char frame[TCP_FRAME_SIZE];
    unsigned char local_mac[6];

    unsigned int ip_length;
    unsigned int tcp_length;
    unsigned int frame_length;

    unsigned short checksum;

    unsigned int i;

    if (payload_length > TCP_MAX_PAYLOAD)
        return 0;

    tcp_length =
        20 + payload_length;

    ip_length =
        20 + tcp_length;

    frame_length =
        14 + ip_length;

    if (frame_length < 60)
        frame_length = 60;

    for (i = 0; i < frame_length; i++)
        frame[i] = 0;

    /*
     * Ethernet destination = gateway MAC.
     */
    for (i = 0; i < 6; i++)
    {
        frame[i] =
            connection->gateway_mac[i];
    }

    rtl8139_get_mac(local_mac);

    /*
     * Ethernet source = our MAC.
     */
    for (i = 0; i < 6; i++)
    {
        frame[6 + i] =
            local_mac[i];
    }

    /*
     * IPv4 EtherType.
     */
    frame[12] = 0x08;
    frame[13] = 0x00;

    /*
     * IPv4 header.
     */
    frame[14] = 0x45;
    frame[15] = 0x00;

    frame[16] =
        (unsigned char)(ip_length >> 8);

    frame[17] =
        (unsigned char)(ip_length & 0xFF);

    frame[18] = 0x12;
    frame[19] = 0x34;

    frame[20] = 0x40;
    frame[21] = 0x00;

    frame[22] = 64;
    frame[23] = TCP_PROTOCOL;

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
            connection->server_ip[i];
    }

    checksum =
        ipv4_checksum(&frame[14]);

    frame[24] =
        (unsigned char)(checksum >> 8);

    frame[25] =
        (unsigned char)(checksum & 0xFF);

    /*
     * TCP source port.
     */
    frame[34] =
        (unsigned char)
        (connection->local_port >> 8);

    frame[35] =
        (unsigned char)
        connection->local_port;

    /*
     * TCP destination port.
     */
    frame[36] =
        (unsigned char)
        (connection->server_port >> 8);

    frame[37] =
        (unsigned char)
        connection->server_port;

    /*
     * TCP sequence number.
     */
    frame[38] =
        (unsigned char)
        (connection->sequence_number >> 24);

    frame[39] =
        (unsigned char)
        (connection->sequence_number >> 16);

    frame[40] =
        (unsigned char)
        (connection->sequence_number >> 8);

    frame[41] =
        (unsigned char)
        connection->sequence_number;

    /*
     * TCP acknowledgement number.
     */
    frame[42] =
        (unsigned char)
        (connection->acknowledgement_number >> 24);

    frame[43] =
        (unsigned char)
        (connection->acknowledgement_number >> 16);

    frame[44] =
        (unsigned char)
        (connection->acknowledgement_number >> 8);

    frame[45] =
        (unsigned char)
        connection->acknowledgement_number;

    /*
     * Data offset = 5 -> 20 byte TCP header.
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

    frame[52] = 0;
    frame[53] = 0;

    /*
     * Payload.
     */
    for (i = 0; i < payload_length; i++)
    {
        frame[54 + i] =
            payload[i];
    }

    checksum =
        tcp_checksum(
            connection->server_ip,
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

static int parse_packet(
    tcp_connection_t *connection,
    const unsigned char *frame,
    int length,
    unsigned char *payload,
    unsigned int payload_capacity,
    unsigned int *payload_length,
    unsigned char *flags,
    unsigned int *packet_sequence,
    unsigned int *packet_acknowledgement)
{
    unsigned int ip_header_length;
    unsigned int tcp_header_length;

    unsigned int tcp_start;
    unsigned int payload_start;

    unsigned int ip_total_length;
    unsigned int available_payload;

    unsigned short source_port;
    unsigned short destination_port;

    unsigned int sequence_number;
    unsigned int acknowledgement_number;

    unsigned char tcp_flags;

    unsigned int i;

    if (length < 54)
        return 0;

    /*
     * Ethernet must be IPv4.
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
        (unsigned int)
        (frame[14] & 0x0F) * 4;

    if (ip_header_length < 20)
        return 0;

    if (length <
        (int)(14 + ip_header_length + 20))
    {
        return 0;
    }

    /*
     * TCP protocol.
     */
    if (frame[23] != TCP_PROTOCOL)
        return 0;

    /*
     * Source IP must be our backend/server.
     */
    if (!same_ip(
            &frame[26],
            connection->server_ip))
    {
        return 0;
    }

    /*
     * Destination IP must be MeetOS.
     */
    if (!same_ip(
            &frame[30],
            local_ip))
    {
        return 0;
    }

    tcp_start =
        14 + ip_header_length;

    source_port =
        ((unsigned short)frame[tcp_start] << 8) |
        frame[tcp_start + 1];

    destination_port =
        ((unsigned short)frame[tcp_start + 2] << 8) |
        frame[tcp_start + 3];

    if (source_port !=
        connection->server_port)
    {
        return 0;
    }

    if (destination_port !=
        connection->local_port)
    {
        return 0;
    }

    sequence_number =
        read_u32(&frame[tcp_start + 4]);

    acknowledgement_number =
        read_u32(&frame[tcp_start + 8]);

    tcp_header_length =
        (unsigned int)
        ((frame[tcp_start + 12] >> 4) & 0x0F) * 4;

    if (tcp_header_length < 20)
        return 0;

    if (length <
        (int)(tcp_start + tcp_header_length))
    {
        return 0;
    }

    tcp_flags =
        frame[tcp_start + 13];

    ip_total_length =
        ((unsigned int)frame[16] << 8) |
        frame[17];

    if (ip_total_length <
        ip_header_length + tcp_header_length)
    {
        return 0;
    }

    payload_start =
        tcp_start + tcp_header_length;

    available_payload =
        ip_total_length -
        ip_header_length -
        tcp_header_length;

    if (payload_start +
        available_payload >
        (unsigned int)length)
    {
        return 0;
    }

    if (available_payload >
        payload_capacity)
    {
        available_payload =
            payload_capacity;
    }

    if (payload != 0)
    {
        for (i = 0; i < available_payload; i++)
        {
            payload[i] =
                frame[payload_start + i];
        }
    }

    *payload_length =
        available_payload;

    *flags =
        tcp_flags;

    *packet_sequence =
        sequence_number;

    *packet_acknowledgement =
        acknowledgement_number;

    return 1;
}

int tcp_connect(
    tcp_connection_t *connection,
    const unsigned char *server_ip,
    unsigned short local_port,
    unsigned short server_port)
{
    unsigned char frame[TCP_FRAME_SIZE];

    unsigned int timeout;

    unsigned int server_sequence;
    unsigned int our_sequence;

    unsigned char flags;

    unsigned int payload_length;
    unsigned int packet_sequence;
    unsigned int packet_acknowledgement;

    int length;

    unsigned int i;

    if (connection == 0 ||
        server_ip == 0)
    {
        return 0;
    }

    for (i = 0; i < 4; i++)
    {
        connection->server_ip[i] =
            server_ip[i];
    }

    connection->local_port =
        local_port;

    connection->server_port =
        server_port;

    connection->sequence_number =
        next_sequence;

    next_sequence += 0x1000;

    connection->acknowledgement_number =
        0;

    connection->connected =
        0;

    if (!arp_resolve(
            gateway_ip,
            connection->gateway_mac))
    {
        return 0;
    }

    our_sequence =
        connection->sequence_number;

    if (!send_packet(
            connection,
            TCP_TRANSPORT_FLAG_SYN,
            0,
            0))
    {
        return 0;
    }

    connection->sequence_number =
        our_sequence + 1;

    timeout = TCP_TIMEOUT;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            if (parse_packet(
                    connection,
                    frame,
                    length,
                    0,
                    0,
                    &payload_length,
                    &flags,
                    &packet_sequence,
                    &packet_acknowledgement))
            {
                if ((flags &
                     TCP_TRANSPORT_FLAG_SYN) &&
                    (flags &
                     TCP_TRANSPORT_FLAG_ACK))
                {
                    if (packet_acknowledgement ==
                        our_sequence + 1)
                    {
                        server_sequence =
                            packet_sequence;

                        connection->
                            acknowledgement_number =
                            server_sequence + 1;

                        break;
                    }
                }
            }
        }

        __asm__ volatile ("nop");
    }

    if (timeout == 0)
        return 0;

    if (!send_packet(
            connection,
            TCP_TRANSPORT_FLAG_ACK,
            0,
            0))
    {
        return 0;
    }

    connection->connected =
        1;

    return 1;
}

int tcp_send(
    tcp_connection_t *connection,
    unsigned char flags,
    const unsigned char *payload,
    unsigned int payload_length)
{
    if (connection == 0)
        return 0;

    if (!connection->connected)
        return 0;

    if (payload_length > TCP_MAX_PAYLOAD)
        return 0;

    if (payload_length > 0 &&
        payload == 0)
    {
        return 0;
    }

    if (!send_packet(
            connection,
            flags,
            payload,
            payload_length))
    {
        return 0;
    }

    connection->sequence_number +=
        payload_length;

    if (flags &
        TCP_TRANSPORT_FLAG_SYN)
    {
        connection->sequence_number++;
    }

    if (flags &
        TCP_TRANSPORT_FLAG_FIN)
    {
        connection->sequence_number++;
    }

    return 1;
}

int tcp_receive(
    tcp_connection_t *connection,
    unsigned char *payload,
    unsigned int payload_capacity,
    unsigned int *payload_length,
    unsigned char *flags)
{
    unsigned char frame[TCP_FRAME_SIZE];

    unsigned int timeout;
    unsigned int packet_sequence;
    unsigned int packet_acknowledgement;

    int length;

    if (connection == 0 ||
        payload_length == 0 ||
        flags == 0)
    {
        return 0;
    }

    *payload_length = 0;
    *flags = 0;

    /*
     * Give the backend enough time to generate the
     * AI response and send the HTTP response.
     */
    timeout = 500000000;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            

            if (parse_packet(
                    connection,
                    frame,
                    length,
                    payload,
                    payload_capacity,
                    payload_length,
                    flags,
                    &packet_sequence,
                    &packet_acknowledgement))
            {
                

                /*
                 * The server ACK tells us how much of our
                 * HTTP request it has received.
                 */
                if (packet_acknowledgement >
                    connection->sequence_number)
                {
                    connection->sequence_number =
                        packet_acknowledgement;
                }

                /*
                 * ACK-only packet.
                 *
                 * This is normal. It only acknowledges our
                 * HTTP request. Keep waiting for actual data.
                 */
                if (*payload_length == 0 &&
                    !(*flags &
                      TCP_TRANSPORT_FLAG_FIN))
                {
                    

                    /*
                     * Reset the receive timeout because we
                     * successfully received a packet.
                     */
                    timeout = 500000000;

                    continue;
                }

                /*
                 * Actual server/application data.
                 */
                if (*payload_length > 0)
                {
                    

                    /*
                     * Accept the payload without imposing
                     * an overly strict sequence-number check.
                     */
                    connection->
                        acknowledgement_number =
                        packet_sequence +
                        *payload_length;

                    if (send_packet(
                            connection,
                            TCP_TRANSPORT_FLAG_ACK,
                            0,
                            0))
                    {
                        
                    }
                    else
                    {
                        
                    }

                    return 1;
                }

                /*
                 * Server closed its sending side.
                 */
                if (*flags &
                    TCP_TRANSPORT_FLAG_FIN)
                {
                    

                    connection->
                        acknowledgement_number =
                        packet_sequence + 1;

                    send_packet(
                        connection,
                        TCP_TRANSPORT_FLAG_ACK,
                        0,
                        0);

                    return 1;
                }
            }
            else
            {
                
            }
        }

        __asm__ volatile ("nop");
    }

    

    return 0;
}
