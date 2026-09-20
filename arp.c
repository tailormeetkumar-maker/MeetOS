#include "arp.h"
#include "rtl8139.h"

extern void print(const char *text);

#define ARP_FRAME_SIZE 42
#define ARP_TIMEOUT    200000

static unsigned char local_mac[6];

static unsigned char cached_mac[6];
static unsigned char cached_ip[4];

static int cache_valid = 0;

static void print_hex_byte(
    unsigned char value)
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

static int same_mac(
    const unsigned char *a,
    const unsigned char *b)
{
    unsigned int i;

    for (i = 0; i < 6; i++)
    {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}

static void arp_send_request(
    const unsigned char *target_ip)
{
    unsigned char frame[ARP_FRAME_SIZE];

    unsigned int i;

    rtl8139_get_mac(local_mac);

    /*
     * Ethernet destination:
     * Broadcast FF:FF:FF:FF:FF:FF
     */

    for (i = 0; i < 6; i++)
        frame[i] = 0xFF;

    /*
     * Ethernet source.
     */

    for (i = 0; i < 6; i++)
        frame[6 + i] =
            local_mac[i];

    /*
     * EtherType = ARP.
     */

    frame[12] = 0x08;
    frame[13] = 0x06;

    /*
     * Hardware type = Ethernet.
     */

    frame[14] = 0x00;
    frame[15] = 0x01;

    /*
     * Protocol type = IPv4.
     */

    frame[16] = 0x08;
    frame[17] = 0x00;

    /*
     * Hardware address length = 6.
     */

    frame[18] = 0x06;

    /*
     * Protocol address length = 4.
     */

    frame[19] = 0x04;

    /*
     * Operation = request.
     */

    frame[20] = 0x00;
    frame[21] = 0x01;

    /*
     * Sender MAC.
     */

    for (i = 0; i < 6; i++)
        frame[22 + i] =
            local_mac[i];

    /*
     * Sender IP = 10.0.2.15.
     */

    frame[28] = 10;
    frame[29] = 0;
    frame[30] = 2;
    frame[31] = 15;

    /*
     * Target MAC unknown.
     */

    for (i = 0; i < 6; i++)
        frame[32 + i] = 0x00;

    /*
     * Target IP.
     */

    for (i = 0; i < 4; i++)
        frame[38 + i] =
            target_ip[i];

    rtl8139_send(
        frame,
        ARP_FRAME_SIZE);
}

static int arp_process_reply(
    const unsigned char *frame,
    int length,
    const unsigned char *wanted_ip)
{
    unsigned short ether_type;
    unsigned short operation;

    unsigned int i;

    if (length < ARP_FRAME_SIZE)
        return 0;

    /*
     * Ethernet type must be ARP.
     */

    ether_type =
        ((unsigned short)frame[12] << 8) |
        frame[13];

    if (ether_type != 0x0806)
        return 0;

    /*
     * Hardware type = Ethernet.
     */

    if (frame[14] != 0x00 ||
        frame[15] != 0x01)
        return 0;

    /*
     * Protocol = IPv4.
     */

    if (frame[16] != 0x08 ||
        frame[17] != 0x00)
        return 0;

    /*
     * Operation = ARP reply.
     */

    operation =
        ((unsigned short)frame[20] << 8) |
        frame[21];

    if (operation != 2)
        return 0;

    /*
     * Sender IP must be the IP we requested.
     */

    if (!same_ip(
            &frame[28],
            wanted_ip))
    {
        return 0;
    }

    /*
     * Save sender MAC.
     */

    for (i = 0; i < 6; i++)
        cached_mac[i] =
            frame[22 + i];

    /*
     * Save sender IP.
     */

    for (i = 0; i < 4; i++)
        cached_ip[i] =
            frame[28 + i];

    cache_valid = 1;

    return 1;
}

int arp_resolve(
    const unsigned char *target_ip,
    unsigned char *target_mac)
{
    unsigned char frame[1600];

    int length;
    unsigned int timeout;

    unsigned int i;

    if (target_ip == 0)
        return 0;

    if (target_mac == 0)
        return 0;

    /*
     * Check existing ARP cache.
     */

    if (cache_valid &&
        same_ip(
            cached_ip,
            target_ip))
    {
        for (i = 0; i < 6; i++)
            target_mac[i] =
                cached_mac[i];

        return 1;
    }

    /*
     * Get our MAC.
     */

    rtl8139_get_mac(local_mac);

    /*
     * Send ARP request.
     */

    arp_send_request(target_ip);

    timeout = ARP_TIMEOUT;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            if (arp_process_reply(
                    frame,
                    length,
                    target_ip))
            {
                for (i = 0; i < 6; i++)
                    target_mac[i] =
                        cached_mac[i];

                return 1;
            }
        }

        __asm__ volatile ("nop");
    }

    return 0;
}

void arp_command(
    char *arguments)
{
    unsigned char target_ip[4];
    unsigned char target_mac[6];

    (void)arguments;

    target_ip[0] = 10;
    target_ip[1] = 0;
    target_ip[2] = 2;
    target_ip[3] = 2;

    print("\nARP\n");
    print("------------------------------\n");

    rtl8139_get_mac(local_mac);

    print("Local MAC   : ");
    print_mac(local_mac);
    print("\n");

    print("Request IP  : ");
    print_ip(target_ip);
    print("\n");

    print("Resolving...\n");

    if (arp_resolve(
            target_ip,
            target_mac))
    {
        print("ARP reply received!\n");

        print("IP          : ");
        print_ip(target_ip);
        print("\n");

        print("MAC         : ");
        print_mac(target_mac);
        print("\n");

        print("------------------------------\n");
        print("ARP resolution successful.\n\n");
    }
    else
    {
        print("No ARP reply received.\n");
        print("------------------------------\n");
        print("ARP resolution failed.\n\n");
    }
}
