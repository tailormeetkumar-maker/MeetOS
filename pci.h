#ifndef PCI_H
#define PCI_H

unsigned int pci_read_config_dword(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset
);

unsigned short pci_read_config_word(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset
);

void pci_write_config_word(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset,
    unsigned short value
);

void pci_write_config_dword(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset,
    unsigned int value
);

int pci_find_device(
    unsigned short vendor,
    unsigned short device,
    unsigned char *bus,
    unsigned char *slot,
    unsigned char *function
);

void pci_init(void);

#endif
