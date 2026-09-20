#include "ipv4.h"
#include "rtl8139.h"
#include "arp.h"

extern void print(const char *text);

#define IPV4_ETHERTYPE 0x0800
#define ICMP_PROTOCOL  1

static unsigned char local_mac[6];

static unsigned char gateway_ip[4] =
{
    10, 0, 2, 2
};

static unsigned char local_ip[4] =
{
    10, 0, 2, 15
};

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
        sum +=
            ((unsigned short)data[length - 1] << 8);

    while (sum >> 16)
        sum =
            (sum & 0xFFFF) +
            (sum >> 16);

    return (unsigned short)(~sum);
}

static unsigned short icmp_checksum(
    const unsigned char *data,
    unsigned int length)
{
    return ipv4_checksum(data, length);
}

static void print_hex_byte(unsigned char value)
{
    const char digits[] =
        "0123456789ABCDEF";

    char text[3];

    text[0] =
        digits[(value >> 4) & 0x0F];

    text[1] =
        digits[value & 0x0F];

    text[2] = '\0';

    print(text);
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

static void print_mac(
    const unsigned char *mac)
{
    unsigned int i;

    for (i = 0; i < 6; i++)
    {
        print_hex_byte(mac[i]);

        if (i != 5)
            print(":");
    }
}

static void copy_bytes(
    unsigned char *dst,
    const unsigned char *src,
    unsigned int length)
{
    unsigned int i;

    for (i = 0; i < length; i++)
        dst[i] = src[i];
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

static int build_icmp_echo(
    unsigned char *icmp)
{
    unsigned short checksum;
    unsigned int i;

    /*
     * ICMP Echo Request
     *
     * Type       = 8
     * Code       = 0
     * Checksum   = calculated
     * Identifier = 0x1234
     * Sequence   = 1
     */

    icmp[0] = 8;
    icmp[1] = 0;

    icmp[2] = 0;
    icmp[3] = 0;

    icmp[4] = 0x12;
    icmp[5] = 0x34;

    icmp[6] = 0x00;
    icmp[7] = 0x01;

    for (i = 8; i < 16; i++)
        icmp[i] = 0;

    checksum =
        icmp_checksum(icmp, 16);

    icmp[2] =
        (unsigned char)(checksum >> 8);

    icmp[3] =
        (unsigned char)(checksum & 0xFF);

    return 16;
}

static int send_ipv4_echo(
    const unsigned char *destination_mac)
{
    unsigned char frame[60];
    unsigned char *ip;
    unsigned char *icmp;

    unsigned short checksum;
    unsigned int i;

    for (i = 0; i < 60; i++)
        frame[i] = 0;

    /*
     * Ethernet destination
     */

    for (i = 0; i < 6; i++)
        frame[i] = destination_mac[i];

    /*
     * Ethernet source
     */

    rtl8139_get_mac(local_mac);

    for (i = 0; i < 6; i++)
        frame[6 + i] = local_mac[i];

    /*
     * Ethernet type = IPv4
     */

    frame[12] = 0x08;
    frame[13] = 0x00;

    /*
     * IPv4 header begins at byte 14.
     */

    ip = &frame[14];

    /*
     * Version = 4
     * IHL     = 5
     */

    ip[0] = 0x45;

    /*
     * DSCP / ECN
     */

    ip[1] = 0x00;

    /*
     * Total length = 20 + 16 = 36
     */

    ip[2] = 0x00;
    ip[3] = 0x24;

    /*
     * Identification
     */

    ip[4] = 0x12;
    ip[5] = 0x34;

    /*
     * Flags + Fragment offset
     */

    ip[6] = 0x40;
    ip[7] = 0x00;

    /*
     * TTL = 64
     */

    ip[8] = 64;

    /*
     * Protocol = ICMP
     */

    ip[9] = ICMP_PROTOCOL;

    /*
     * Header checksum initially zero.
     */

    ip[10] = 0;
    ip[11] = 0;

    /*
     * Source IP
     */

    for (i = 0; i < 4; i++)
        ip[12 + i] = local_ip[i];

    /*
     * Destination IP
     */

    for (i = 0; i < 4; i++)
        ip[16 + i] = gateway_ip[i];

    /*
     * Calculate IPv4 header checksum.
     */

    checksum =
        ipv4_checksum(ip, 20);

    ip[10] =
        (unsigned char)(checksum >> 8);

    ip[11] =
        (unsigned char)(checksum & 0xFF);

    /*
     * ICMP begins after IPv4 header.
     */

    icmp = &frame[34];

    build_icmp_echo(icmp);

    /*
     * Ethernet minimum frame size is 60 bytes.
     */

    return rtl8139_send(frame, 60);
}

static int process_ipv4_reply(
    const unsigned char *frame,
    int length)
{
    unsigned int ip_header_length;
    unsigned short total_length;
    unsigned short received_checksum;
    unsigned short calculated_checksum;
    unsigned char protocol;

    if (length < 34)
        return 0;

    /*
     * Ethernet must contain IPv4.
     */

    if (frame[12] != 0x08 ||
        frame[13] != 0x00)
    {
        return 0;
    }

    /*
     * IPv4 version must be 4.
     */

    if ((frame[14] >> 4) != 4)
        return 0;

    /*
     * Header length in bytes.
     */

    ip_header_length =
        (unsigned int)(frame[14] & 0x0F) * 4;

    if (ip_header_length < 20)
        return 0;

    if (length < (int)(14 + ip_header_length))
        return 0;

    /*
     * Total IPv4 packet length.
     */

    total_length =
        ((unsigned short)frame[16] << 8) |
        frame[17];

    if (total_length < ip_header_length)
        return 0;

    if (total_length >
        (unsigned short)(length - 14))
    {
        return 0;
    }

    /*
     * Verify destination is our IP.
     */

    if (!same_ip(&frame[30], local_ip))
        return 0;

    /*
     * Protocol must be ICMP.
     */

    protocol = frame[23];

    if (protocol != ICMP_PROTOCOL)
        return 0;

    /*
     * Verify IPv4 header checksum.
     */

    received_checksum =
        ((unsigned short)frame[24] << 8) |
        frame[25];

    frame = frame;

    calculated_checksum =
        ipv4_checksum(&frame[14],
                      ip_header_length);

    if (calculated_checksum != 0)
    {
        return 0;
    }

    /*
     * ICMP Echo Reply:
     * Type = 0
     */

    if (frame[14 + ip_header_length] != 0)
        return 0;

    return 1;
}

void ipv4_command(char *arguments)
{
    unsigned char gateway_mac[6];
    unsigned char frame[1600];

    int length;
    unsigned int timeout;

    (void)arguments;

    print("\nIPv4\n");
    print("------------------------------\n");

    print("Source IP      : ");
    print_ip(local_ip);
    print("\n");

    print("Destination IP : ");
    print_ip(gateway_ip);
    print("\n");

    print("Resolving gateway MAC...\n");

    /*
     * For the first version we use the MAC
     * learned from the working QEMU ARP test.
     *
     * QEMU user networking gateway:
     *
     * 10.0.2.2
     * 52:55:0A:00:02:02
     */

    gateway_mac[0] = 0x52;
    gateway_mac[1] = 0x55;
    gateway_mac[2] = 0x0A;
    gateway_mac[3] = 0x00;
    gateway_mac[4] = 0x02;
    gateway_mac[5] = 0x02;

    print("Gateway MAC    : ");
    print_mac(gateway_mac);
    print("\n");

    print("Building IPv4 packet...\n");

    if (!send_ipv4_echo(gateway_mac))
    {
        print("ERROR: IPv4 packet transmission failed.\n");
        print("------------------------------\n\n");
        return;
    }

    print("IPv4 packet sent.\n");
    print("Protocol       : ICMP\n");
    print("Waiting for reply...\n");

    timeout = 200000;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            if (process_ipv4_reply(
                    frame,
                    length))
            {
                print("IPv4 reply received!\n");
                print("ICMP Echo Reply received.\n");
                print("------------------------------\n");
                print("IPv4 communication successful.\n\n");

                return;
            }
        }

        __asm__ volatile ("nop");
    }

    print("No IPv4 reply received.\n");
    print("------------------------------\n");
    print("IPv4 communication failed.\n\n");
}
