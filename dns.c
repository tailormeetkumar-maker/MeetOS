#include "dns.h"
#include "rtl8139.h"
#include "arp.h"

extern void print(const char *text);

#define DNS_SERVER_PORT 53
#define LOCAL_PORT      49153
#define DNS_QUERY_ID    0x1234

static unsigned char local_ip[4] =
{
    10, 0, 2, 15
};

static unsigned char dns_server_ip[4] =
{
    10, 0, 2, 3
};

static unsigned short fold_checksum(
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

    return fold_checksum(sum);
}

static unsigned short udp_checksum(
    const unsigned char *udp,
    unsigned int udp_length)
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
        ((unsigned short)dns_server_ip[0] << 8) |
        dns_server_ip[1];

    sum +=
        ((unsigned short)dns_server_ip[2] << 8) |
        dns_server_ip[3];

    sum += 17;
    sum += udp_length;

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

    return fold_checksum(sum);
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

static void print_hex16(
    unsigned short value)
{
    const char hex[] =
        "0123456789ABCDEF";

    char text[5];

    text[0] =
        hex[(value >> 12) & 0x0F];

    text[1] =
        hex[(value >> 8) & 0x0F];

    text[2] =
        hex[(value >> 4) & 0x0F];

    text[3] =
        hex[value & 0x0F];

    text[4] = '\0';

    print(text);
}

static void encode_domain(
    unsigned char *buffer,
    unsigned int *position,
    const char *domain)
{
    unsigned int label_start;
    unsigned int label_length;

    unsigned int i;
    unsigned int pos;

    pos = *position;

    label_start = pos;
    buffer[pos++] = 0;

    label_length = 0;
    i = 0;

    while (domain[i] != '\0')
    {
        if (domain[i] == '.')
        {
            buffer[label_start] =
                (unsigned char)label_length;

            label_start = pos;
            buffer[pos++] = 0;

            label_length = 0;
        }
        else
        {
            buffer[pos++] =
                (unsigned char)domain[i];

            label_length++;
        }

        i++;
    }

    buffer[label_start] =
        (unsigned char)label_length;

    buffer[pos++] = 0;

    *position = pos;
}

static unsigned int build_dns_query(
    unsigned char *dns)
{
    unsigned int position;

    position = 0;

    dns[position++] =
        (unsigned char)(DNS_QUERY_ID >> 8);

    dns[position++] =
        (unsigned char)(DNS_QUERY_ID & 0xFF);

    /*
     * Standard query + recursion desired.
     */
    dns[position++] = 0x01;
    dns[position++] = 0x00;

    /*
     * QDCOUNT = 1
     */
    dns[position++] = 0x00;
    dns[position++] = 0x01;

    /*
     * ANCOUNT = 0
     */
    dns[position++] = 0x00;
    dns[position++] = 0x00;

    /*
     * NSCOUNT = 0
     */
    dns[position++] = 0x00;
    dns[position++] = 0x00;

    /*
     * ARCOUNT = 0
     */
    dns[position++] = 0x00;
    dns[position++] = 0x00;

    encode_domain(
        dns,
        &position,
        "example.com");

    /*
     * QTYPE = A
     */
    dns[position++] = 0x00;
    dns[position++] = 0x01;

    /*
     * QCLASS = IN
     */
    dns[position++] = 0x00;
    dns[position++] = 0x01;

    return position;
}

static int send_dns_query(
    const unsigned char *destination_mac)
{
    unsigned char frame[128];
    unsigned char dns[64];
    unsigned char local_mac[6];

    unsigned int dns_length;
    unsigned int udp_length;
    unsigned int ip_length;

    unsigned short checksum;

    unsigned int i;

    dns_length =
        build_dns_query(dns);

    udp_length =
        8 + dns_length;

    ip_length =
        20 + udp_length;

    for (i = 0; i < 128; i++)
        frame[i] = 0;

    /*
     * Ethernet destination.
     */
    for (i = 0; i < 6; i++)
        frame[i] =
            destination_mac[i];

    /*
     * Ethernet source.
     */
    rtl8139_get_mac(local_mac);

    for (i = 0; i < 6; i++)
        frame[6 + i] =
            local_mac[i];

    /*
     * IPv4.
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

    frame[18] = 0x22;
    frame[19] = 0x22;

    /*
     * Don't fragment.
     */
    frame[20] = 0x40;
    frame[21] = 0x00;

    frame[22] = 64;

    /*
     * UDP.
     */
    frame[23] = 17;

    frame[24] = 0;
    frame[25] = 0;

    /*
     * Source IP.
     */
    for (i = 0; i < 4; i++)
        frame[26 + i] =
            local_ip[i];

    /*
     * Destination IP.
     */
    for (i = 0; i < 4; i++)
        frame[30 + i] =
            dns_server_ip[i];

    checksum =
        ipv4_checksum(
            &frame[14],
            20);

    frame[24] =
        (unsigned char)(checksum >> 8);

    frame[25] =
        (unsigned char)(checksum & 0xFF);

    /*
     * Source port 49153.
     */
    frame[34] = 0xC0;
    frame[35] = 0x01;

    /*
     * Destination port 53.
     */
    frame[36] = 0x00;
    frame[37] = 0x35;

    frame[38] =
        (unsigned char)(udp_length >> 8);

    frame[39] =
        (unsigned char)(udp_length & 0xFF);

    frame[40] = 0;
    frame[41] = 0;

    /*
     * DNS data.
     */
    for (i = 0; i < dns_length; i++)
        frame[42 + i] =
            dns[i];

    checksum =
        udp_checksum(
            &frame[34],
            udp_length);

    if (checksum == 0)
        checksum = 0xFFFF;

    frame[40] =
        (unsigned char)(checksum >> 8);

    frame[41] =
        (unsigned char)(checksum & 0xFF);

    print("DNS packet length : ");
    print_decimal(ip_length + 14);
    print("\n");

    print("UDP checksum      : 0x");
    print_hex16(checksum);
    print("\n");

    return rtl8139_send(
        frame,
        14 + ip_length);
}

static int parse_dns_response(
    const unsigned char *frame,
    int length,
    unsigned char *answer_ip)
{
    const unsigned char *dns;

    unsigned int ip_header_length;
    unsigned int dns_length;
    unsigned int position;

    unsigned short total_length;
    unsigned short udp_length;

    unsigned short transaction_id;
    unsigned short flags;
    unsigned short questions;
    unsigned short answers;

    unsigned int i;

    if (length < 42)
    {
        print("DNS debug: frame too short.\n");
        return 0;
    }

    if (frame[12] != 0x08 ||
        frame[13] != 0x00)
    {
        print("DNS debug: not IPv4.\n");
        return 0;
    }

    if ((frame[14] >> 4) != 4)
    {
        print("DNS debug: invalid IPv4 version.\n");
        return 0;
    }

    ip_header_length =
        (unsigned int)(frame[14] & 0x0F) * 4;

    if (ip_header_length < 20)
    {
        print("DNS debug: invalid IP header length.\n");
        return 0;
    }

    if (length <
        (int)(14 + ip_header_length + 8))
    {
        print("DNS debug: incomplete UDP packet.\n");
        return 0;
    }

    total_length =
        ((unsigned short)frame[16] << 8) |
        frame[17];

    if (total_length <
        ip_header_length + 8)
    {
        print("DNS debug: invalid IP total length.\n");
        return 0;
    }

    if (total_length >
        (unsigned short)(length - 14))
    {
        print("DNS debug: IP length exceeds frame.\n");
        return 0;
    }

    if (frame[23] != 17)
    {
        print("DNS debug: protocol is not UDP.\n");
        return 0;
    }

    /*
     * Destination IP.
     */
    for (i = 0; i < 4; i++)
    {
        if (frame[30 + i] != local_ip[i])
        {
            print("DNS debug: wrong destination IP.\n");
            return 0;
        }
    }

    /*
     * Source IP.
     */
    for (i = 0; i < 4; i++)
    {
        if (frame[26 + i] != dns_server_ip[i])
        {
            print("DNS debug: wrong source IP.\n");
            return 0;
        }
    }

    /*
     * UDP header.
     */
    if (frame[34] != 0x00 ||
        frame[35] != 0x35)
    {
        print("DNS debug: wrong UDP source port.\n");
        return 0;
    }

    if (frame[36] != 0xC0 ||
        frame[37] != 0x01)
    {
        print("DNS debug: wrong UDP destination port.\n");
        return 0;
    }

    udp_length =
        ((unsigned short)frame[38] << 8) |
        frame[39];

    print("DNS debug: UDP response length = ");
    print_decimal(udp_length);
    print("\n");

    if (udp_length < 8)
    {
        print("DNS debug: invalid UDP length.\n");
        return 0;
    }

    if ((unsigned int)udp_length >
        total_length - ip_header_length)
    {
        print("DNS debug: UDP length exceeds IP packet.\n");
        return 0;
    }

    dns =
        &frame[
            14 +
            ip_header_length +
            8];

    dns_length =
        (unsigned int)udp_length - 8;

    if (dns_length < 12)
    {
        print("DNS debug: DNS data too short.\n");
        return 0;
    }

    transaction_id =
        ((unsigned short)dns[0] << 8) |
        dns[1];

    print("DNS debug: transaction ID = 0x");
    print_hex16(transaction_id);
    print("\n");

    if (transaction_id != DNS_QUERY_ID)
    {
        print("DNS debug: wrong transaction ID.\n");
        return 0;
    }

    flags =
        ((unsigned short)dns[2] << 8) |
        dns[3];

    print("DNS debug: flags = 0x");
    print_hex16(flags);
    print("\n");

    if ((flags & 0x8000) == 0)
    {
        print("DNS debug: packet is not a response.\n");
        return 0;
    }

    if ((flags & 0x000F) != 0)
    {
        print("DNS debug: DNS server returned an error.\n");
        return 0;
    }

    questions =
        ((unsigned short)dns[4] << 8) |
        dns[5];

    answers =
        ((unsigned short)dns[6] << 8) |
        dns[7];

    print("DNS debug: questions = ");
    print_decimal(questions);
    print("\n");

    print("DNS debug: answers = ");
    print_decimal(answers);
    print("\n");

    if (questions != 1)
    {
        print("DNS debug: unexpected question count.\n");
        return 0;
    }

    if (answers == 0)
    {
        print("DNS debug: no answers.\n");
        return 0;
    }

    /*
     * Skip question name.
     */
    position = 12;

    while (position < dns_length)
    {
        unsigned char label_length;

        label_length =
            dns[position++];

        if (label_length == 0)
            break;

        if ((label_length & 0xC0) != 0)
        {
            print("DNS debug: invalid question name.\n");
            return 0;
        }

        if (position + label_length > dns_length)
        {
            print("DNS debug: question exceeds packet.\n");
            return 0;
        }

        position += label_length;
    }

    if (position + 4 > dns_length)
    {
        print("DNS debug: question fields missing.\n");
        return 0;
    }

    position += 4;

    /*
     * Process answers.
     */
    for (i = 0; i < answers; i++)
    {
        unsigned short type;
        unsigned short class_value;
        unsigned short data_length;

        if (position + 2 > dns_length)
        {
            print("DNS debug: answer name missing.\n");
            return 0;
        }

        if ((dns[position] & 0xC0) == 0xC0)
        {
            position += 2;
        }
        else
        {
            while (position < dns_length)
            {
                unsigned char label_length;

                label_length =
                    dns[position++];

                if (label_length == 0)
                    break;

                if (position + label_length >
                    dns_length)
                {
                    print("DNS debug: answer name invalid.\n");
                    return 0;
                }

                position += label_length;
            }
        }

        if (position + 10 > dns_length)
        {
            print("DNS debug: answer header missing.\n");
            return 0;
        }

        type =
            ((unsigned short)dns[position] << 8) |
            dns[position + 1];

        class_value =
            ((unsigned short)dns[position + 2] << 8) |
            dns[position + 3];

        data_length =
            ((unsigned short)dns[position + 8] << 8) |
            dns[position + 9];

        position += 10;

        if (type == 1 &&
            class_value == 1 &&
            data_length == 4)
        {
            if (position + 4 > dns_length)
            {
                print("DNS debug: invalid A record.\n");
                return 0;
            }

            for (i = 0; i < 4; i++)
                answer_ip[i] =
                    dns[position + i];

            return 1;
        }

        if (position + data_length > dns_length)
        {
            print("DNS debug: answer data exceeds packet.\n");
            return 0;
        }

        position += data_length;
    }

    print("DNS debug: no IPv4 A record found.\n");

    return 0;
}
int dns_resolve(
    const char *domain,
    unsigned char *answer_ip)
{
    unsigned char dns_mac[6];
    unsigned char frame[1600];

    unsigned int timeout;
    int length;

    if (domain == 0 ||
        answer_ip == 0)
    {
        return 0;
    }

    /*
     * The current DNS packet builder uses
     * example.com internally.
     *
     * For this first reusable version,
     * only example.com is supported.
     */
    if (domain[0] != 'e' ||
        domain[1] != 'x' ||
        domain[2] != 'a' ||
        domain[3] != 'm' ||
        domain[4] != 'p' ||
        domain[5] != 'l' ||
        domain[6] != 'e' ||
        domain[7] != '.' ||
        domain[8] != 'c' ||
        domain[9] != 'o' ||
        domain[10] != 'm' ||
        domain[11] != '\0')
    {
        return 0;
    }

    /*
     * Resolve DNS server MAC address.
     */
    if (!arp_resolve(
            dns_server_ip,
            dns_mac))
    {
        return 0;
    }

    /*
     * Send DNS query.
     */
    if (!send_dns_query(dns_mac))
    {
        return 0;
    }

    /*
     * Wait for DNS response.
     */
    timeout = 300000;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            if (parse_dns_response(
                    frame,
                    length,
                    answer_ip))
            {
                return 1;
            }
        }

        __asm__ volatile ("nop");
    }

    return 0;
}

void dns_command(
    char *arguments)
{
    unsigned char dns_mac[6];

    unsigned char frame[1600];
    unsigned char answer_ip[4];

    int length;

    unsigned int timeout;

    (void)arguments;

    print("\nDNS\n");
    print("------------------------------\n");

    print("Domain         : example.com\n");

    print("DNS server     : ");
    print_ip(dns_server_ip);
    print("\n");

    print("Resolving DNS server MAC...\n");

    if (!arp_resolve(
            dns_server_ip,
            dns_mac))
    {
        print("ERROR: Could not resolve DNS server MAC.\n");
        print("------------------------------\n\n");
        return;
    }

    print("DNS server MAC : ");

    {
        const char digits[] =
            "0123456789ABCDEF";

        unsigned int i;

        for (i = 0; i < 6; i++)
        {
            char text[3];

            text[0] =
                digits[(dns_mac[i] >> 4) & 0x0F];

            text[1] =
                digits[dns_mac[i] & 0x0F];

            text[2] = '\0';

            print(text);

            if (i != 5)
                print(":");
        }
    }

    print("\n");

    print("Sending DNS query...\n");

    if (!send_dns_query(dns_mac))
    {
        print("ERROR: DNS query transmission failed.\n");
        print("------------------------------\n\n");
        return;
    }

    print("DNS query sent.\n");
    print("Waiting for DNS response...\n");

    timeout = 300000;

    while (timeout--)
    {
        length =
            rtl8139_receive(
                frame,
                sizeof(frame));

        if (length > 0)
        {
            print("\nDNS debug: Ethernet frame received.\n");
            print("DNS debug: frame length = ");
            print_decimal((unsigned int)length);
            print("\n");

            if (parse_dns_response(
                    frame,
                    length,
                    answer_ip))
            {
                print("DNS response received!\n");

                print("example.com IP : ");
                print_ip(answer_ip);
                print("\n");

                print("------------------------------\n");
                print("DNS resolution successful.\n\n");

                return;
            }
        }

        __asm__ volatile ("nop");
    }

    print("No DNS response received.\n");
    print("------------------------------\n");
    print("DNS resolution failed.\n\n");
}
