#ifndef ARP_H
#define ARP_H

void arp_command(char *arguments);

int arp_resolve(
    const unsigned char *target_ip,
    unsigned char *target_mac);

#endif
