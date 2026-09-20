#include "pci.h"

/*
 * MeetOS PCI subsystem
 *
 * PCI Configuration Mechanism #1
 *
 * CONFIG_ADDRESS = 0xCF8
 * CONFIG_DATA    = 0xCFC
 */

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA    0x0CFC

#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10

#define PCI_COMMAND_IO        0x0001
#define PCI_COMMAND_MEMORY    0x0002
#define PCI_COMMAND_BUSMASTER 0x0004


static void pci_outl(unsigned short port, unsigned int value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


static unsigned int pci_inl(unsigned short port)
{
    unsigned int value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


static void pci_outw(unsigned short port, unsigned short value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


static unsigned short pci_inw(unsigned short port)
{
    unsigned short value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


static unsigned int pci_make_address(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset)
{
    return
        0x80000000U |
        ((unsigned int)bus << 16) |
        ((unsigned int)slot << 11) |
        ((unsigned int)function << 8) |
        ((unsigned int)(offset & 0xFC));
}


/*
 * Read a complete 32-bit PCI configuration register.
 */
unsigned int pci_read_config_dword(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset)
{
    unsigned int address;
    unsigned int value;

    address = pci_make_address(
        bus,
        slot,
        function,
        offset
    );

    pci_outl(PCI_CONFIG_ADDRESS, address);
    value = pci_inl(PCI_CONFIG_DATA);

    return value;
}


/*
 * Read a 16-bit PCI configuration field.
 *
 * PCI configuration mechanism #1 accesses a 32-bit register,
 * so select the appropriate half of that register.
 */
unsigned short pci_read_config_word(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset)
{
    unsigned int address;
    unsigned int value;
    unsigned int shift;

    address = pci_make_address(
        bus,
        slot,
        function,
        offset
    );

    pci_outl(PCI_CONFIG_ADDRESS, address);

    value = pci_inl(PCI_CONFIG_DATA);

    shift = (unsigned int)(offset & 2) * 8U;

    return (unsigned short)((value >> shift) & 0xFFFFU);
}


/*
 * Write a 16-bit PCI configuration field.
 *
 * We use the appropriate half of the 32-bit configuration-data
 * register.
 */
void pci_write_config_word(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset,
    unsigned short value)
{
    unsigned int address;

    address = pci_make_address(
        bus,
        slot,
        function,
        offset
    );

    pci_outl(PCI_CONFIG_ADDRESS, address);

    if (offset & 2)
        pci_outw(PCI_CONFIG_DATA + 2, value);
    else
        pci_outw(PCI_CONFIG_DATA, value);
}


/*
 * Write a complete 32-bit PCI configuration register.
 */
void pci_write_config_dword(
    unsigned char bus,
    unsigned char slot,
    unsigned char function,
    unsigned char offset,
    unsigned int value)
{
    unsigned int address;

    address = pci_make_address(
        bus,
        slot,
        function,
        offset
    );

    pci_outl(PCI_CONFIG_ADDRESS, address);
    pci_outl(PCI_CONFIG_DATA, value);
}


/*
 * Find a PCI device by Vendor ID + Device ID.
 */
int pci_find_device(
    unsigned short vendor,
    unsigned short device,
    unsigned char *bus,
    unsigned char *slot,
    unsigned char *function)
{
    unsigned int b;
    unsigned int s;
    unsigned int f;
    unsigned int id;
    unsigned short found_vendor;
    unsigned short found_device;

    for (b = 0; b < 256; b++) {
        for (s = 0; s < 32; s++) {

            /*
             * Check function 0 first.
             */
            id = pci_read_config_dword(
                (unsigned char)b,
                (unsigned char)s,
                0,
                0x00
            );

            found_vendor = (unsigned short)(id & 0xFFFFU);

            if (found_vendor == 0xFFFF)
                continue;

            found_device = (unsigned short)((id >> 16) & 0xFFFFU);

            if (found_vendor == vendor &&
                found_device == device) {

                if (bus != 0)
                    *bus = (unsigned char)b;

                if (slot != 0)
                    *slot = (unsigned char)s;

                if (function != 0)
                    *function = 0;

                return 1;
            }

            /*
             * Check remaining functions only when the device
             * is multifunction.
             */
            {
                unsigned char header_type;

                header_type = (unsigned char)(
                    (pci_read_config_dword(
                        (unsigned char)b,
                        (unsigned char)s,
                        0,
                        PCI_HEADER_TYPE & 0xFC
                    ) >> 16) & 0xFF
                );

                if ((header_type & 0x80) == 0)
                    continue;
            }

            for (f = 1; f < 8; f++) {

                id = pci_read_config_dword(
                    (unsigned char)b,
                    (unsigned char)s,
                    (unsigned char)f,
                    0x00
                );

                found_vendor = (unsigned short)(id & 0xFFFFU);

                if (found_vendor == 0xFFFF)
                    continue;

                found_device = (unsigned short)(
                    (id >> 16) & 0xFFFFU
                );

                if (found_vendor == vendor &&
                    found_device == device) {

                    if (bus != 0)
                        *bus = (unsigned char)b;

                    if (slot != 0)
                        *slot = (unsigned char)s;

                    if (function != 0)
                        *function = (unsigned char)f;

                    return 1;
                }
            }
        }
    }

    return 0;
}


/*
 * Basic PCI enumeration.
 */
void pci_init(void)
{
    unsigned int bus;
    unsigned int slot;
    unsigned int function;
    unsigned int id;
    unsigned short vendor;
    unsigned short device;

    extern void print(const char *text);
    extern void print_number(unsigned int value);

    print("PCI subsystem\n\n");

    for (bus = 0; bus < 256; bus++) {

        for (slot = 0; slot < 32; slot++) {

            id = pci_read_config_dword(
                (unsigned char)bus,
                (unsigned char)slot,
                0,
                0x00
            );

            vendor = (unsigned short)(id & 0xFFFFU);

            if (vendor == 0xFFFF)
                continue;

            device = (unsigned short)(
                (id >> 16) & 0xFFFFU
            );

            print("PCI device: vendor=");
            print_number(vendor);
            print(" device=");
            print_number(device);
            print("\n");

            /*
             * Check whether function 0 is multifunction.
             */
            {
                unsigned int header;

                header = pci_read_config_dword(
                    (unsigned char)bus,
                    (unsigned char)slot,
                    0,
                    0x0C
                );

                if ((header & 0x00800000U) == 0)
                    continue;
            }

            for (function = 1; function < 8; function++) {

                id = pci_read_config_dword(
                    (unsigned char)bus,
                    (unsigned char)slot,
                    (unsigned char)function,
                    0x00
                );

                vendor = (unsigned short)(id & 0xFFFFU);

                if (vendor == 0xFFFF)
                    continue;

                device = (unsigned short)(
                    (id >> 16) & 0xFFFFU
                );

                print("PCI device: vendor=");
                print_number(vendor);
                print(" device=");
                print_number(device);
                print("\n");
            }
        }
    }

    print("\n");
}
