#include "udp.h"
#include "rtl8139.h"

extern void print(const char *text);

#define UDP_PROTOCOL 17

static unsigned char local_ip[4] =
{
    10, 0, 2, 15
};

static unsigned char gateway_ip[4] =
{
    10, 0, 2, 2
};

static unsigned char gateway_mac[6] =
{
    0x52, 0x55, 0x0A,
    0x00, 0x02, 0x02
};

static unsigned short checksum_add(
    unsigned long sum)
{
    while (sum >> 16)
        sum =
            (sum & 0xFFFF) +
            (sum >> 16);

    return (unsigned short)(~sum);
}

static unsigned short ipv4_checksum(
    const unsigned char *data,
    unsigned int length)
{
    unsigned long sum;
    unsigned int i;

    sum = 0;

    for (i = 0; i + 1 < length; i += 2)
    {
        unsigned short word;

        word =
            ((unsigned short)data[i] << 8) |
            data[i + 1];

        sum += word;
    }

    if (length & 1)
    {
        sum +=
            ((unsigned short)data[length - 1] << 8);
    }

    return checksum_add(sum);
}

static unsigned short udp_checksum(
    const unsigned char *udp,
    unsigned int udp_length)
{
    unsigned long sum;
    unsigned int i;

    sum = 0;

    /*
     * IPv4 pseudo-header
     *
     * Source IP
     */
    sum +=
        ((unsigned short)local_ip[0] << 8) |
        local_ip[1];

    sum +=
        ((unsigned short)local_ip[2] << 8) |
        local_ip[3];

    /*
     * Destination IP
     */
    sum +=
        ((unsigned short)gateway_ip[0] << 8) |
        gateway_ip[1];

    sum +=
        ((unsigned short)gateway_ip[2] << 8) |
        gateway_ip[3];

    /*
     * Zero + protocol
     */
    sum += UDP_PROTOCOL;

    /*
     * UDP length
     */
    sum += udp_length;

    /*
     * UDP header + payload
     */
    for (i = 0; i + 1 < udp_length; i += 2)
    {
        unsigned short word;

        word =
            ((unsigned short)udp[i] << 8) |
            udp[i + 1];

        sum += word;
    }

    if (udp_length & 1)
    {
        sum +=
            ((unsigned short)udp[udp_length - 1] << 8);
    }

    return checksum_add(sum);
}

static void print_ip(
    const unsigned char *ip)
{
    unsigned int i;

    for (i = 0; i < 4; i++)
    {
        char digits[4];
        unsigned int value;
        unsigned int pos;

        value = ip[i];
        pos = 0;

        if (value == 0)
        {
            print("0");
        }
        else
        {
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

        if (i != 3)
            print(".");
    }
}

static int send_udp_test(void)
{
    unsigned char frame[60];

    const unsigned char payload[] =
        "MeetOS UDP Test";

    unsigned int payload_length;
    unsigned int udp_length;
    unsigned int ip_length;

    unsigned short checksum;

    unsigned char local_mac[6];

    unsigned int i;

    payload_length =
        sizeof(payload) - 1;

    udp_length =
        8 + payload_length;

    ip_length =
        20 + udp_length;

    for (i = 0; i < 60; i++)
        frame[i] = 0;

    /*
     * Ethernet destination MAC
     */

    for (i = 0; i < 6; i++)
        frame[i] = gateway_mac[i];

    /*
     * Ethernet source MAC
     */

    rtl8139_get_mac(local_mac);

    for (i = 0; i < 6; i++)
        frame[6 + i] = local_mac[i];

    /*
     * EtherType = IPv4
     */

    frame[12] = 0x08;
    frame[13] = 0x00;

    /*
     * IPv4 header
     */

    frame[14] = 0x45;
    frame[15] = 0x00;

    frame[16] =
        (unsigned char)(ip_length >> 8);

    frame[17] =
        (unsigned char)(ip_length & 0xFF);

    /*
     * Identification
     */

    frame[18] = 0x56;
    frame[19] = 0x78;

    /*
     * Don't fragment
     */

    frame[20] = 0x40;
    frame[21] = 0x00;

    /*
     * TTL
     */

    frame[22] = 64;

    /*
     * Protocol = UDP
     */

    frame[23] = UDP_PROTOCOL;

    /*
     * Header checksum initially zero
     */

    frame[24] = 0;
    frame[25] = 0;

    /*
     * Source IP
     */

    for (i = 0; i < 4; i++)
        frame[26 + i] = local_ip[i];

    /*
     * Destination IP
     */

    for (i = 0; i < 4; i++)
        frame[30 + i] = gateway_ip[i];

    /*
     * IPv4 checksum
     */

    checksum =
        ipv4_checksum(&frame[14], 20);

    frame[24] =
        (unsigned char)(checksum >> 8);

    frame[25] =
        (unsigned char)(checksum & 0xFF);

    /*
     * UDP header starts at byte 34.
     *
     * Source port = 49152
     * Destination port = 12345
     */

    frame[34] = 0xC0;
    frame[35] = 0x00;

    frame[36] = 0x30;
    frame[37] = 0x39;

    /*
     * UDP length
     */

    frame[38] =
        (unsigned char)(udp_length >> 8);

    frame[39] =
        (unsigned char)(udp_length & 0xFF);

    /*
     * UDP checksum initially zero
     */

    frame[40] = 0;
    frame[41] = 0;

    /*
     * UDP payload
     */

    for (i = 0; i < payload_length; i++)
        frame[42 + i] = payload[i];

    /*
     * UDP checksum
     */

    checksum =
        udp_checksum(
            &frame[34],
            udp_length);

    /*
     * A calculated zero checksum is transmitted
     * as 0xFFFF for UDP over IPv4.
     */

    if (checksum == 0)
        checksum = 0xFFFF;

    frame[40] =
        (unsigned char)(checksum >> 8);

    frame[41] =
        (unsigned char)(checksum & 0xFF);

    /*
     * Ethernet minimum frame size = 60 bytes.
     */

    return rtl8139_send(frame, 60);
}

void udp_command(char *arguments)
{
    (void)arguments;

    print("\nUDP\n");
    print("------------------------------\n");

    print("Source IP      : ");
    print_ip(local_ip);
    print("\n");

    print("Destination IP : ");
    print_ip(gateway_ip);
    print("\n");

    print("Source port    : 49152\n");
    print("Destination port: 12345\n");
    print("Protocol       : UDP\n");
    print("Payload        : MeetOS UDP Test\n");

    print("Building UDP packet...\n");

    if (send_udp_test())
    {
        print("UDP packet sent successfully.\n");
        print("------------------------------\n");
        print("UDP transmission successful.\n\n");
    }
    else
    {
        print("ERROR: UDP transmission failed.\n");
        print("------------------------------\n\n");
    }
}
