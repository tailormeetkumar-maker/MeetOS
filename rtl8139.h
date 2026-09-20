#ifndef RTL8139_H
#define RTL8139_H

void rtl8139_init(void);
void rtl8139_status(char *arguments);

/*
 * Transmit one complete Ethernet frame.
 *
 * Returns:
 *   1 = transmission started
 *   0 = transmission failed
 */
int rtl8139_send(const unsigned char *frame, unsigned int length);

/*
 * Copy the NIC's actual MAC address into the supplied buffer.
 */
void rtl8139_get_mac(unsigned char *out);

/*
 * Receive one Ethernet frame.
 *
 * Returns:
 *   0 = no packet available
 *   >0 = received frame length
 */
int rtl8139_receive(unsigned char *out, unsigned int max_length);

#endif
