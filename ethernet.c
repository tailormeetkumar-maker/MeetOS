#include "ethernet.h"
#include "rtl8139.h"

extern void print(const char *text);

static unsigned char local_mac[6];

/* =========================================================
   Ethernet Initialization
   ========================================================= */

void ethernet_init(void)
{
    /*
     * Read the actual MAC address from the RTL8139 NIC.
     */
    rtl8139_get_mac(local_mac);
}

/* =========================================================
   Ethernet Test
   ========================================================= */

void ethernet_test(char *arguments)
{
    (void)arguments;

    unsigned char frame[60];

    /*
     * Destination MAC:
     *
     * FF:FF:FF:FF:FF:FF = Ethernet broadcast
     */
    frame[0] = 0xFF;
    frame[1] = 0xFF;
    frame[2] = 0xFF;
    frame[3] = 0xFF;
    frame[4] = 0xFF;
    frame[5] = 0xFF;

    /*
     * Source MAC:
     *
     * Use the actual MAC address read from
     * the RTL8139 network card.
     */
    frame[6]  = local_mac[0];
    frame[7]  = local_mac[1];
    frame[8]  = local_mac[2];
    frame[9]  = local_mac[3];
    frame[10] = local_mac[4];
    frame[11] = local_mac[5];

    /*
     * EtherType:
     *
     * 0x88B5 = local experimental protocol.
     */
    frame[12] = 0x88;
    frame[13] = 0xB5;

    /*
     * Test payload.
     */
    frame[14] = 'M';
    frame[15] = 'e';
    frame[16] = 'e';
    frame[17] = 't';
    frame[18] = 'O';
    frame[19] = 'S';
    frame[20] = ' ';
    frame[21] = 'E';
    frame[22] = 't';
    frame[23] = 'h';
    frame[24] = 'e';
    frame[25] = 'r';
    frame[26] = 'n';
    frame[27] = 'e';
    frame[28] = 't';
    frame[29] = ' ';
    frame[30] = 'T';
    frame[31] = 'e';
    frame[32] = 's';
    frame[33] = 't';

    /*
     * Zero-fill the rest of the Ethernet frame.
     */
    for (int i = 34; i < 60; i++)
    {
        frame[i] = 0;
    }

    print("\nEthernet\n");
    print("------------------------------\n");
    print("Creating Ethernet frame...\n");

    if (rtl8139_send(frame, 60))
    {
        print("Frame handed to RTL8139.\n");
        print("Destination : Broadcast\n");
        print("Source MAC  : RTL8139 MAC\n");
        print("EtherType   : 0x88B5\n");
        print("Payload     : MeetOS Ethernet Test\n");
        print("------------------------------\n");
        print("Ethernet transmission started.\n\n");
    }
    else
    {
        print("ERROR: Ethernet transmission failed.\n");
        print("------------------------------\n\n");
    }
}
