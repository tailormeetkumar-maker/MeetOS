#include "rtl8139.h"

/* ---------------------------------------------------------
   PCI configuration space
   --------------------------------------------------------- */

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

/* ---------------------------------------------------------
   RTL8139 registers
   --------------------------------------------------------- */

#define RTL_REG_MAC0       0x00
#define RTL_REG_MAR0       0x08
#define RTL_REG_TSD0       0x10
#define RTL_REG_TSAD0      0x20
#define RTL_REG_RBSTART    0x30
#define RTL_REG_CAPR       0x38
#define RTL_REG_CBR        0x3A
#define RTL_REG_IMR        0x3C
#define RTL_REG_ISR        0x3E
#define RTL_REG_TCR        0x40
#define RTL_REG_RCR        0x44
#define RTL_REG_COMMAND    0x37
#define RTL_REG_CONFIG1    0x52

#define RTL_CMD_RESET      0x10
#define RTL_CMD_RX_ENABLE  0x08
#define RTL_CMD_TX_ENABLE  0x04

/* ---------------------------------------------------------
   TX status bits
   --------------------------------------------------------- */

#define RTL_TSD_TOK        0x00008000
#define RTL_TSD_TUN        0x00004000
#define RTL_TSD_TER        0x00000004

/* ---------------------------------------------------------
   Buffer sizes
   --------------------------------------------------------- */

/*
 * RTL8139 8K receive mode actually needs 8K + 16 bytes.
 * Add a little extra alignment space as recommended by
 * the hardware design.
 */
#define RTL_RX_BUFFER_SIZE  (8192 + 16 + 1500)

#define RTL_TX_BUFFER_SIZE 1536
#define RTL_TX_COUNT       4

/* ---------------------------------------------------------
   Hardware state
   --------------------------------------------------------- */

static unsigned short rtl8139_io_base = 0;

static int rtl8139_found = 0;
static int rtl8139_initialized = 0;

static unsigned char mac_address[6];

static unsigned char rx_buffer[RTL_RX_BUFFER_SIZE]
    __attribute__((aligned(16)));

static unsigned char tx_buffers[RTL_TX_COUNT][RTL_TX_BUFFER_SIZE]
    __attribute__((aligned(16)));

static unsigned int current_tx_buffer = 0;
static unsigned int current_rx_offset = 0;

/* ---------------------------------------------------------
   Port I/O
   --------------------------------------------------------- */

static void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void outl(unsigned short port, unsigned int value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static unsigned short inw(unsigned short port)
{
    unsigned short value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static unsigned int inl(unsigned short port)
{
    unsigned int value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/* ---------------------------------------------------------
   Small delay
   --------------------------------------------------------- */

static void rtl8139_delay(void)
{
    volatile unsigned int i;

    for (i = 0; i < 10000; i++)
    {
        __asm__ volatile ("nop");
    }
}

/* ---------------------------------------------------------
   PCI configuration access
   --------------------------------------------------------- */

static unsigned int pci_config_read(unsigned char bus,
                                     unsigned char device,
                                     unsigned char function,
                                     unsigned char offset)
{
    unsigned int address;

    address =
        0x80000000 |
        ((unsigned int)bus << 16) |
        ((unsigned int)device << 11) |
        ((unsigned int)function << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);

    return inl(PCI_CONFIG_DATA);
}

static void pci_config_write(unsigned char bus,
                             unsigned char device,
                             unsigned char function,
                             unsigned char offset,
                             unsigned int value)
{
    unsigned int address;

    address =
        0x80000000 |
        ((unsigned int)bus << 16) |
        ((unsigned int)device << 11) |
        ((unsigned int)function << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/* ---------------------------------------------------------
   Find RTL8139 through PCI
   --------------------------------------------------------- */

static int find_rtl8139(void)
{
    unsigned int id;
    unsigned short vendor;
    unsigned short device;
    unsigned int bar0;

    unsigned int bus;
    unsigned int dev;

    for (bus = 0; bus < 256; bus++)
    {
        for (dev = 0; dev < 32; dev++)
        {
            id = pci_config_read(
                (unsigned char)bus,
                (unsigned char)dev,
                0,
                0x00
            );

            vendor = (unsigned short)(id & 0xFFFF);
            device = (unsigned short)((id >> 16) & 0xFFFF);

            if (vendor == RTL8139_VENDOR_ID &&
                device == RTL8139_DEVICE_ID)
            {
                bar0 = pci_config_read(
                    (unsigned char)bus,
                    (unsigned char)dev,
                    0,
                    0x10
                );

                if (bar0 & 1)
                {
                    rtl8139_io_base =
                        (unsigned short)(bar0 & 0xFFFC);
                }
                else
                {
                    rtl8139_io_base = 0;
                }

                /*
                 * Enable:
                 *   I/O space
                 *   Bus mastering
                 */
                {
                    unsigned int command;

                    command = pci_config_read(
                        (unsigned char)bus,
                        (unsigned char)dev,
                        0,
                        0x04
                    );

                    command |= 0x00000005;

                    pci_config_write(
                        (unsigned char)bus,
                        (unsigned char)dev,
                        0,
                        0x04,
                        command
                    );
                }

                rtl8139_found = 1;

                return 1;
            }
        }
    }

    return 0;
}

/* ---------------------------------------------------------
   Initialize RTL8139
   --------------------------------------------------------- */

void rtl8139_init(void)
{
    unsigned int i;

    rtl8139_found = 0;
    rtl8139_initialized = 0;

    if (!find_rtl8139())
        return;

    if (rtl8139_io_base == 0)
        return;

    /*
     * Wake NIC.
     */
    outb(
        rtl8139_io_base + RTL_REG_CONFIG1,
        0x00
    );

    rtl8139_delay();

    /*
     * Reset NIC.
     */
    outb(
        rtl8139_io_base + RTL_REG_COMMAND,
        RTL_CMD_RESET
    );

    /*
     * Wait for reset.
     */
    for (i = 0; i < 100000; i++)
    {
        if ((inb(rtl8139_io_base + RTL_REG_COMMAND) &
             RTL_CMD_RESET) == 0)
        {
            break;
        }

        __asm__ volatile ("nop");
    }

    if (i == 100000)
        return;

    /*
     * Read MAC.
     */
    for (i = 0; i < 6; i++)
    {
        mac_address[i] =
            inb(rtl8139_io_base + RTL_REG_MAC0 + i);
    }

    /*
     * Give physical address of RX buffer to NIC.
     */
    outl(
        rtl8139_io_base + RTL_REG_RBSTART,
        (unsigned int)rx_buffer
    );

    /*
     * Clear pending interrupts.
     */
    outw(
        rtl8139_io_base + RTL_REG_ISR,
        0xFFFF
    );

    /*
     * Polling mode.
     */
    outw(
        rtl8139_io_base + RTL_REG_IMR,
        0x0000
    );

    /*
     * RCR:
     *
     * AB  = Accept Broadcast
     * AM  = Accept Multicast
     * AAP = Accept All Packets
     * AP  = Accept Physical Match
     *
     * 0x0F enables all four.
     */
    outl(
        rtl8139_io_base + RTL_REG_RCR,
        0x0000000F
    );

    /*
     * TX configuration.
     */
    outl(
        rtl8139_io_base + RTL_REG_TCR,
        0x03000700
    );

    /*
     * Start receiver and transmitter.
     */
    outb(
        rtl8139_io_base + RTL_REG_COMMAND,
        RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE
    );

    /*
     * Software begins at offset 0.
     *
     * CAPR is always 16 bytes behind the software
     * read position.
     */
    current_tx_buffer = 0;
    current_rx_offset = 0;

    outw(
        rtl8139_io_base + RTL_REG_CAPR,
        0xFFF0
    );

    rtl8139_initialized = 1;
}

/* ---------------------------------------------------------
   Get MAC
   --------------------------------------------------------- */

void rtl8139_get_mac(unsigned char *out)
{
    unsigned int i;

    if (out == 0)
        return;

    for (i = 0; i < 6; i++)
    {
        out[i] = mac_address[i];
    }
}

/* ---------------------------------------------------------
   Send Ethernet frame
   --------------------------------------------------------- */

int rtl8139_send(const unsigned char *frame,
                 unsigned int length)
{
    unsigned int i;
    unsigned int transmit_length;
    unsigned int status;
    unsigned int timeout;

    if (!rtl8139_initialized)
        return 0;

    if (frame == 0)
        return 0;

    transmit_length = length;

    if (transmit_length < 60)
        transmit_length = 60;

    if (transmit_length > RTL_TX_BUFFER_SIZE)
        return 0;

    /*
     * Copy frame.
     */
    for (i = 0; i < length; i++)
    {
        tx_buffers[current_tx_buffer][i] = frame[i];
    }

    /*
     * Ethernet minimum-frame padding.
     */
    for (i = length; i < transmit_length; i++)
    {
        tx_buffers[current_tx_buffer][i] = 0;
    }

    /*
     * Set TX buffer address.
     */
    outl(
        rtl8139_io_base +
        RTL_REG_TSAD0 +
        (current_tx_buffer * 4),
        (unsigned int)tx_buffers[current_tx_buffer]
    );

    /*
     * Start TX.
     */
    outl(
        rtl8139_io_base +
        RTL_REG_TSD0 +
        (current_tx_buffer * 4),
        transmit_length
    );

    timeout = 100000;

    while (timeout--)
    {
        status =
            inl(
                rtl8139_io_base +
                RTL_REG_TSD0 +
                (current_tx_buffer * 4)
            );

        if (status & RTL_TSD_TOK)
        {
            current_tx_buffer++;

            if (current_tx_buffer >= RTL_TX_COUNT)
                current_tx_buffer = 0;

            return 1;
        }

        if (status & (RTL_TSD_TUN | RTL_TSD_TER))
        {
            return 0;
        }

        __asm__ volatile ("nop");
    }

    return 0;
}

/* ---------------------------------------------------------
   Advance RX position
   --------------------------------------------------------- */

static void rtl8139_advance_rx(unsigned int packet_length)
{
    unsigned int next_offset;

    /*
     * RTL8139 packet layout:
     *
     * 4-byte status/length header
     * packet data
     * 4-byte FCS
     *
     * Hardware aligns the complete packet to 4 bytes.
     */
    next_offset =
        current_rx_offset +
        (((unsigned int)packet_length + 4 + 3) & ~3U);

    /*
     * The RX ring is 8K.
     */
    next_offset %= 8192;

    current_rx_offset = next_offset;

    /*
     * CAPR must remain 16 bytes behind
     * the software read pointer.
     */
    if (current_rx_offset == 0)
    {
        outw(
            rtl8139_io_base + RTL_REG_CAPR,
            0xFFF0
        );
    }
    else
    {
        outw(
            rtl8139_io_base + RTL_REG_CAPR,
            (unsigned short)(current_rx_offset - 16)
        );
    }
}

/* ---------------------------------------------------------
   Receive one Ethernet frame
   --------------------------------------------------------- */

int rtl8139_receive(unsigned char *out,
                    unsigned int max_length)
{
    unsigned short packet_status;
    unsigned short packet_length;
    unsigned int frame_length;
    unsigned int i;
    unsigned int position;
    unsigned short hardware_offset;

    if (!rtl8139_initialized)
        return 0;

    if (out == 0)
        return 0;

    if (max_length == 0)
        return 0;

    /*
     * Read hardware receive position.
     */
    hardware_offset =
        inw(rtl8139_io_base + RTL_REG_CBR);

    hardware_offset &= 0x1FFF;

    /*
     * No new packet.
     */
    if (current_rx_offset ==
        (unsigned int)hardware_offset)
    {
        return 0;
    }

    /*
     * Read RTL8139 RX packet header.
     *
     * +0 : packet status
     * +2 : packet length including FCS
     */
    position = current_rx_offset;

    packet_status =
        (unsigned short)rx_buffer[position] |
        ((unsigned short)rx_buffer[
            (position + 1) % 8192
        ] << 8);

    packet_length =
        (unsigned short)rx_buffer[
            (position + 2) % 8192
        ] |
        ((unsigned short)rx_buffer[
            (position + 3) % 8192
        ] << 8);

    /*
     * Sanity check.
     */
    if (packet_length < 64 ||
        packet_length > RTL_TX_BUFFER_SIZE + 4)
    {
        current_rx_offset = 0;

        outw(
            rtl8139_io_base + RTL_REG_CAPR,
            0xFFF0
        );

        return 0;
    }

    /*
     * Receive OK bit.
     */
    if ((packet_status & 0x0001) == 0)
    {
        rtl8139_advance_rx(packet_length);
        return 0;
    }

    /*
     * Remove the four-byte Ethernet FCS.
     */
    frame_length =
        (unsigned int)packet_length - 4;

    if (frame_length > max_length)
    {
        rtl8139_advance_rx(packet_length);
        return 0;
    }

    /*
     * Copy Ethernet frame from circular RX buffer.
     */
    for (i = 0; i < frame_length; i++)
    {
        position =
            (current_rx_offset + 4 + i) % 8192;

        out[i] = rx_buffer[position];
    }

    /*
     * Advance to the next packet.
     */
    rtl8139_advance_rx(packet_length);

    return (int)frame_length;
}

/* ---------------------------------------------------------
   RTL8139 status
   --------------------------------------------------------- */

void rtl8139_status(char *arguments)
{
    unsigned int command;
    unsigned int isr;
    unsigned int rcr;
    unsigned int i;

    (void)arguments;

    extern void print_string(const char *text);

    print_string("\nRTL8139 status\n");
    print_string("------------------------------\n");

    if (!rtl8139_found)
    {
        print_string("NIC        : Not detected\n");
        print_string("------------------------------\n");
        return;
    }

    print_string("NIC        : RTL8139\n");

    print_string("I/O base   : ");

    {
        char digits[6];
        unsigned int value;
        unsigned int pos;

        value = rtl8139_io_base;
        pos = 0;

        if (value == 0)
        {
            print_string("0");
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
                char temp[2];

                temp[0] = digits[--pos];
                temp[1] = '\0';

                print_string(temp);
            }
        }
    }

    print_string("\n");

    print_string("MAC        : ");

    for (i = 0; i < 6; i++)
    {
        const char hex[] =
            "0123456789ABCDEF";

        char text[3];

        text[0] =
            hex[(mac_address[i] >> 4) & 0x0F];

        text[1] =
            hex[mac_address[i] & 0x0F];

        text[2] = '\0';

        print_string(text);

        if (i != 5)
            print_string(":");
    }

    print_string("\n");

    if (rtl8139_initialized)
    {
        print_string("TX         : Ready\n");
        print_string("RX         : Ready\n");
    }
    else
    {
        print_string("TX         : Not Ready\n");
        print_string("RX         : Not Ready\n");
    }

    command =
        inb(rtl8139_io_base + RTL_REG_COMMAND);

    isr =
        inw(rtl8139_io_base + RTL_REG_ISR);

    rcr =
        inl(rtl8139_io_base + RTL_REG_RCR);

    print_string("Command    : ");

    {
        char digits[4];
        unsigned int value;
        unsigned int pos;

        value = command;
        pos = 0;

        if (value == 0)
        {
            print_string("0");
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
                char temp[2];

                temp[0] = digits[--pos];
                temp[1] = '\0';

                print_string(temp);
            }
        }
    }

    print_string("\n");

    print_string("ISR        : ");

    {
        const char hex[] =
            "0123456789ABCDEF";

        char text[5];

        text[0] =
            hex[(isr >> 12) & 0x0F];

        text[1] =
            hex[(isr >> 8) & 0x0F];

        text[2] =
            hex[(isr >> 4) & 0x0F];

        text[3] =
            hex[isr & 0x0F];

        text[4] = '\0';

        print_string(text);
    }

    print_string("\n");

    print_string("RX config  : ");

    {
        char digits[11];
        unsigned int value;
        unsigned int pos;

        value = rcr;
        pos = 0;

        if (value == 0)
        {
            print_string("0");
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
                char temp[2];

                temp[0] = digits[--pos];
                temp[1] = '\0';

                print_string(temp);
            }
        }
    }

    print_string("\n");

    print_string("------------------------------\n");
}
