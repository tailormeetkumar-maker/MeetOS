#include "ac97.h"
#include "pci.h"

/*
 * MeetOS AC97 microphone subsystem
 *
 * QEMU:
 *   Intel 82801AA AC97
 *   Vendor 8086
 *   Device 2415
 *
 * BAR0 = mixer / codec I/O
 * BAR1 = bus-master I/O
 *
 * This driver explicitly assigns the I/O BARs because MeetOS is
 * a bare-metal kernel and does not have a PCI resource allocator.
 */

extern void print(const char *text);
extern void print_number(unsigned int value);
extern void outb(unsigned short port, unsigned char value);
extern unsigned char inb(unsigned short port);


/* --------------------------------------------------------- */
/* PCI                                                        */
/* --------------------------------------------------------- */

#define AC97_VENDOR_ID          0x8086
#define AC97_DEVICE_ID          0x2415

#define PCI_COMMAND             0x04
#define PCI_COMMAND_IO          0x0001
#define PCI_COMMAND_BUSMASTER   0x0004


/* --------------------------------------------------------- */
/* AC97 mixer registers                                       */
/* --------------------------------------------------------- */

#define AC97_RESET                  0x00
#define AC97_MASTER_VOLUME          0x02
#define AC97_MIC_VOLUME             0x0E
#define AC97_RECORD_SELECT          0x1A
#define AC97_RECORD_GAIN            0x1C
#define AC97_PCM_FRONT_DAC_RATE     0x2C
#define AC97_MIC_ADC_RATE           0x32


/* --------------------------------------------------------- */
/* AC97 MC (Mic-In) bus-master channel                        */
/* --------------------------------------------------------- */

#define MC_BDBAR       0x20
#define MC_CIV         0x24
#define MC_LVI         0x25
#define MC_SR          0x26
#define MC_PICB        0x28
#define MC_PIV         0x2A
#define MC_CR          0x2B


/* Status */

#define SR_DCH         0x0001
#define SR_CELV        0x0002
#define SR_LVBCI       0x0004
#define SR_BCIS        0x0008
#define SR_FIFOE       0x0010


/* Control */

#define CR_RPBM        0x01
#define CR_RR          0x02
#define CR_LVBIE       0x04
#define CR_IOCE        0x08
#define CR_FEIE        0x10


/* --------------------------------------------------------- */
/* Capture buffers                                            */
/* --------------------------------------------------------- */

#define AC97_CAPTURE_SAMPLES 4096

/*
 * QEMU's AC97 BDL:
 *
 *   DWORD 0 = physical address
 *   WORD  4 = number of 16-bit samples
 *   WORD  6 = control
 *
 * IOC = bit 15
 * BUP = bit 14
 *
 * Therefore 0xC000 is correct for the control field.
 */

typedef struct
{
    unsigned int address;
    unsigned short samples;
    unsigned short control;
} ac97_bdl_entry_t;


static volatile unsigned short ac97_mic_buffer[AC97_CAPTURE_SAMPLES]
    __attribute__((aligned(4096)));

static volatile unsigned short ac97_mic_buffer_2[AC97_CAPTURE_SAMPLES]
    __attribute__((aligned(4096)));

static volatile ac97_bdl_entry_t ac97_bdl[2]
    __attribute__((aligned(4096)));


/* --------------------------------------------------------- */
/* State                                                       */
/* --------------------------------------------------------- */

static unsigned char ac97_bus = 0;
static unsigned char ac97_slot = 0;
static unsigned char ac97_function = 0;

static unsigned short ac97_mixer_base = 0;
static unsigned short ac97_busmaster_base = 0;

static int ac97_found = 0;


/* --------------------------------------------------------- */
/* Port I/O                                                    */
/* --------------------------------------------------------- */

static void ac97_outw(
    unsigned short port,
    unsigned short value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


static unsigned short ac97_inw(
    unsigned short port)
{
    unsigned short value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


static void ac97_outl(
    unsigned short port,
    unsigned int value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


static unsigned int ac97_inl(
    unsigned short port)
{
    unsigned int value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


/* --------------------------------------------------------- */
/* Printing                                                    */
/* --------------------------------------------------------- */

static void ac97_print_hex(unsigned int value)
{
    char buffer[11];
    const char *digits = "0123456789ABCDEF";
    int i;

    buffer[0] = '0';
    buffer[1] = 'x';

    for (i = 0; i < 8; i++)
    {
        int shift = 28 - i * 4;

        buffer[2 + i] =
            digits[(value >> shift) & 0x0F];
    }

    buffer[10] = '\0';

    print(buffer);
}


static void ac97_print_line_hex(
    const char *text,
    unsigned int value)
{
    print(text);
    ac97_print_hex(value);
    print("\n");
}


/* --------------------------------------------------------- */
/* Timing                                                     */
/* --------------------------------------------------------- */

/*
 * Simple PIT-based delay.
 *
 * PIT channel 0 runs at approximately 1193182 Hz.
 * The BIOS timer tick occurs about every 54.9 ms.
 *
 * We don't use BIOS memory at 0x046C because MeetOS is already
 * running in protected mode.
 */

static void ac97_wait_ms(unsigned int milliseconds)
{
    volatile unsigned int loops;
    unsigned int i;

    /*
     * This is deliberately conservative.
     * QEMU executes these I/O/CPU operations very quickly,
     * so this provides a short delay without depending on
     * BIOS data structures.
     */
    loops = milliseconds * 50000U;

    for (i = 0; i < loops; i++)
        __asm__ volatile ("pause");
}


/* --------------------------------------------------------- */
/* Mixer                                                       */
/* --------------------------------------------------------- */

static void ac97_mixer_write(
    unsigned char reg,
    unsigned short value)
{
    if (ac97_mixer_base == 0)
        return;

    ac97_outw(
        ac97_mixer_base + reg,
        value);
}


static unsigned short ac97_mixer_read(
    unsigned char reg)
{
    if (ac97_mixer_base == 0)
        return 0;

    return ac97_inw(
        ac97_mixer_base + reg);
}


/* --------------------------------------------------------- */
/* Find controller                                             */
/* --------------------------------------------------------- */

static int ac97_find_controller(void)
{
    unsigned char bus;
    unsigned char slot;
    unsigned char function;

    if (pci_find_device(
            AC97_VENDOR_ID,
            AC97_DEVICE_ID,
            &bus,
            &slot,
            &function))
    {
        ac97_bus = bus;
        ac97_slot = slot;
        ac97_function = function;

        return 1;
    }

    return 0;
}


/* --------------------------------------------------------- */
/* Assign AC97 I/O BARs                                       */
/* --------------------------------------------------------- */

static int ac97_assign_bars(void)
{
    unsigned int bar0;
    unsigned int bar1;

    /*
     * The AC97 controller needs:
     *
     *   BAR0: 0x400 bytes
     *   BAR1: 0x100 bytes
     *
     * Use dedicated aligned I/O addresses.
     *
     * These are I/O BAR values, hence bit 0 = 1.
     */
    pci_write_config_dword(
        ac97_bus,
        ac97_slot,
        ac97_function,
        0x10,
        0x0000C001U);

    pci_write_config_dword(
        ac97_bus,
        ac97_slot,
        ac97_function,
        0x14,
        0x0000C501U);

    /*
     * Read them back immediately.
     */
    bar0 = pci_read_config_dword(
        ac97_bus,
        ac97_slot,
        ac97_function,
        0x10);

    bar1 = pci_read_config_dword(
        ac97_bus,
        ac97_slot,
        ac97_function,
        0x14);

    ac97_print_line_hex(
        "BAR0 after assignment=",
        bar0);

    ac97_print_line_hex(
        "BAR1 after assignment=",
        bar1);

    /*
     * Verify that QEMU accepted the assignments.
     */
    if ((bar0 & 0xFFFCU) != 0xC000U ||
        (bar1 & 0xFFFCU) != 0xC500U)
    {
        print("ERROR: AC97 PCI BAR assignment was rejected.\n");
        return 0;
    }

    ac97_mixer_base = 0xC000;
    ac97_busmaster_base = 0xC500;

    print("AC97 I/O BARs assigned successfully.\n");

    return 1;
}


/* --------------------------------------------------------- */
/* Enable PCI controller                                      */
/* --------------------------------------------------------- */

static void ac97_enable_pci(void)
{
    unsigned short command;

    command = pci_read_config_word(
        ac97_bus,
        ac97_slot,
        ac97_function,
        PCI_COMMAND);

    command |= PCI_COMMAND_IO;
    command |= PCI_COMMAND_BUSMASTER;

    pci_write_config_word(
        ac97_bus,
        ac97_slot,
        ac97_function,
        PCI_COMMAND,
        command);

    command = pci_read_config_word(
        ac97_bus,
        ac97_slot,
        ac97_function,
        PCI_COMMAND);

    print("PCI command register=");
    ac97_print_hex(command);
    print("\n");

    if ((command & PCI_COMMAND_IO) == 0)
        print("WARNING: PCI I/O space is disabled.\n");

    if ((command & PCI_COMMAND_BUSMASTER) == 0)
        print("WARNING: PCI bus mastering is disabled.\n");
}


/* --------------------------------------------------------- */
/* Codec setup                                                 */
/* --------------------------------------------------------- */

static void ac97_setup_codec(void)
{
    print("Resetting AC97 codec...\n");

    ac97_mixer_write(
        AC97_RESET,
        0);

    ac97_wait_ms(20);

    /*
     * Record source:
     *
     * AC97 record select:
     *   0 = microphone
     */
    ac97_mixer_write(
        AC97_RECORD_SELECT,
        0x0000);

    /*
     * Microphone volume:
     *
     * 0 attenuation.
     */
    ac97_mixer_write(
        AC97_MIC_VOLUME,
        0x0000);

    /*
     * Record gain:
     * maximum supported positive gain field for
     * the simple QEMU codec path is not required;
     * keep it at zero for a clean baseline.
     */
    ac97_mixer_write(
        AC97_RECORD_GAIN,
        0x0000);

    /*
     * Capture rate.
     */
    ac97_mixer_write(
        AC97_MIC_ADC_RATE,
        44100);

    /*
     * DAC rate as well.
     */
    ac97_mixer_write(
        AC97_PCM_FRONT_DAC_RATE,
        44100);

    /*
     * Master volume.
     */
    ac97_mixer_write(
        AC97_MASTER_VOLUME,
        0x0000);

    print("AC97 codec configured.\n");
}


/* --------------------------------------------------------- */
/* Capture buffers                                             */
/* --------------------------------------------------------- */

static void ac97_clear_capture_buffer(void)
{
    unsigned int i;

    for (i = 0; i < AC97_CAPTURE_SAMPLES; i++)
    {
        ac97_mic_buffer[i] = 0;
        ac97_mic_buffer_2[i] = 0;
    }
}


/* --------------------------------------------------------- */
/* BDL                                                         */
/* --------------------------------------------------------- */

static void ac97_prepare_bdl(void)
{
    /*
     * IMPORTANT:
     *
     * AC97 BDL length is a count of 16-bit samples,
     * NOT a byte count.
     */
    ac97_bdl[0].address =
        (unsigned int)&ac97_mic_buffer[0];

    ac97_bdl[0].samples =
        AC97_CAPTURE_SAMPLES;

    ac97_bdl[0].control =
        0xC000;

    ac97_bdl[1].address =
        (unsigned int)&ac97_mic_buffer_2[0];

    ac97_bdl[1].samples =
        AC97_CAPTURE_SAMPLES;

    ac97_bdl[1].control =
        0xC000;

    /*
     * Tell the MC channel where the descriptor list lives.
     */
    ac97_outl(
        ac97_busmaster_base + MC_BDBAR,
        (unsigned int)&ac97_bdl[0]);

    /*
     * Current descriptor.
     */
    outb(
        ac97_busmaster_base + MC_CIV,
        0);

    /*
     * Last valid descriptor.
     */
    outb(
        ac97_busmaster_base + MC_LVI,
        1);

    /*
     * Clear completion/error bits.
     */
    ac97_outw(
        ac97_busmaster_base + MC_SR,
        SR_LVBCI |
        SR_BCIS |
        SR_FIFOE);
}


/* --------------------------------------------------------- */
/* Start DMA                                                   */
/* --------------------------------------------------------- */

static void ac97_start_mic_dma(void)
{
    unsigned char control;

    control =
        inb(ac97_busmaster_base + MC_CR);

    control &= (unsigned char)~CR_RPBM;

    outb(
        ac97_busmaster_base + MC_CR,
        control);

    outb(
        ac97_busmaster_base + MC_CIV,
        0);

    outb(
        ac97_busmaster_base + MC_LVI,
        1);

    ac97_outw(
        ac97_busmaster_base + MC_SR,
        SR_LVBCI |
        SR_BCIS |
        SR_FIFOE);

    control =
        inb(ac97_busmaster_base + MC_CR);

    control |= CR_RPBM;

    outb(
        ac97_busmaster_base + MC_CR,
        control);
}


/* --------------------------------------------------------- */
/* Stop DMA                                                    */
/* --------------------------------------------------------- */

static void ac97_stop_mic_dma(void)
{
    unsigned char control;

    control =
        inb(ac97_busmaster_base + MC_CR);

    control &= (unsigned char)~CR_RPBM;

    outb(
        ac97_busmaster_base + MC_CR,
        control);
}


/* --------------------------------------------------------- */
/* Analyse samples                                             */
/* --------------------------------------------------------- */

static void ac97_analyse_samples(void)
{
    unsigned int i;
    unsigned int nonzero;
    unsigned int minimum;
    unsigned int maximum;
    unsigned int sum;
    unsigned int average;
    unsigned int mean;
    unsigned int peak;
    unsigned short sample;
    int signed_sample;
    int difference;

    nonzero = 0;
    minimum = 65535;
    maximum = 0;
    sum = 0;
    mean = 0;
    peak = 0;

    /*
     * First pass:
     * Find the unsigned range and the DC average.
     */
    for (i = 0; i < AC97_CAPTURE_SAMPLES; i++)
    {
        sample = ac97_mic_buffer[i];

        if (sample != 0)
            nonzero++;

        if (sample < minimum)
            minimum = sample;

        if (sample > maximum)
            maximum = sample;

        mean += (unsigned int)sample / AC97_CAPTURE_SAMPLES;
    }

    /*
     * Second pass:
     * Measure distance from the average.
     */
    for (i = 0; i < AC97_CAPTURE_SAMPLES; i++)
    {
        sample = ac97_mic_buffer[i];

        signed_sample = (int)sample;
        difference = signed_sample - (int)mean;

        if (difference < 0)
            difference = -difference;

        if ((unsigned int)difference > peak)
            peak = (unsigned int)difference;

        sum += (unsigned int)difference;
    }

    average = sum / AC97_CAPTURE_SAMPLES;

    print("\n========== AUDIO ANALYSIS ==========\n");

    print("Non-zero samples = ");
    print_number(nonzero);
    print("\n");

    print("Minimum raw sample = ");
    print_number(minimum);
    print("\n");

    print("Maximum raw sample = ");
    print_number(maximum);
    print("\n");

    print("DC average = ");
    print_number(mean);
    print("\n");

    print("Average distance from DC = ");
    print_number(average);
    print("\n");

    print("Peak distance from DC = ");
    print_number(peak);
    print("\n");

    /*
     * Print the first 32 samples so we can see the waveform.
     */
    print("\nFirst 32 samples:\n");

    for (i = 0; i < 32; i++)
    {
        print("[");
        print_number(i);
        print("]=");
        print_number((unsigned int)ac97_mic_buffer[i]);
        print("  ");

        if ((i & 3U) == 3U)
            print("\n");
    }

    print("\n====================================\n");

    if (nonzero == 0)
    {
        print("RESULT: no microphone samples.\n");
    }
    else if (peak == 0)
    {
        print("RESULT: samples exist but show no variation.\n");
    }
    else
    {
        print("RESULT: varying microphone waveform detected.\n");
    }
}

/* --------------------------------------------------------- */
/* Initialization                                              */
/* --------------------------------------------------------- */

void ac97_init(void)
{
    unsigned short mixer_reset;

    print("\n");
    print("AC97 microphone subsystem\n");
    print("------------------------------\n");

    if (!ac97_find_controller())
    {
        print("AC97 controller not found.\n");
        print("------------------------------\n\n");
        return;
    }

    ac97_found = 1;

    print("AC97 controller found.\n");

    print("PCI bus ");
    print_number(ac97_bus);

    print(" slot=");
    print_number(ac97_slot);

    print(" function=");
    print_number(ac97_function);

    print("\n");

    /*
     * First assign and verify the I/O BARs.
     */
    if (!ac97_assign_bars())
    {
        ac97_found = 0;

        print("------------------------------\n\n");
        return;
    }

    /*
     * Enable I/O and bus mastering.
     */
    ac97_enable_pci();

    /*
     * Now it is safe to touch the mixer.
     */
    mixer_reset =
        ac97_mixer_read(AC97_RESET);

    ac97_print_line_hex(
        "Codec reset register=",
        mixer_reset);

    /*
     * Configure codec.
     */
    ac97_setup_codec();

    /*
     * Prepare DMA but don't start it yet.
     */
    ac97_clear_capture_buffer();
    ac97_prepare_bdl();

    print("Microphone input path configured.\n");
    print("MC capture channel prepared.\n");
    print("------------------------------\n\n");
}


/* --------------------------------------------------------- */
/* !ac97test                                                   */
/* --------------------------------------------------------- */

void ac97_test_command(char *arguments)
{
    unsigned short status;
    unsigned short position;

    unsigned char civ;
    unsigned char lvi;
    unsigned char control;

    unsigned int i;
    unsigned int nonzero;

    (void)arguments;

    if (!ac97_found)
    {
        print("AC97 controller is not initialized.\n");
        return;
    }

    print("\n");
    print("AC97 MICROPHONE CAPTURE TEST\n");
    print("============================\n");
    print("Using MC (Mic-In) channel.\n");
    print("Speak or make some sound near the microphone.\n");
    print("Capturing for approximately one second...\n\n");

    ac97_stop_mic_dma();

    ac97_clear_capture_buffer();

    ac97_prepare_bdl();

    ac97_start_mic_dma();

    ac97_wait_ms(1000);

    ac97_stop_mic_dma();

    status =
        ac97_inw(
            ac97_busmaster_base + MC_SR);

    position =
        ac97_inw(
            ac97_busmaster_base + MC_PICB);

    civ =
        inb(
            ac97_busmaster_base + MC_CIV);

    lvi =
        inb(
            ac97_busmaster_base + MC_LVI);

    control =
        inb(
            ac97_busmaster_base + MC_CR);

    print("MC status = ");
    ac97_print_hex(status);
    print("\n");

    print("MC CIV = ");
    print_number(civ);
    print("\n");

    print("MC LVI = ");
    print_number(lvi);
    print("\n");

    print("MC PICB = ");
    print_number(position);
    print("\n");

    print("MC control = ");
    ac97_print_hex(control);
    print("\n");

    print("\nDCH = ");
    print((status & SR_DCH) ? "1\n" : "0\n");

    print("BCIS = ");
    print((status & SR_BCIS) ? "1\n" : "0\n");

    print("LVBCI = ");
    print((status & SR_LVBCI) ? "1\n" : "0\n");

    print("FIFOE = ");
    print((status & SR_FIFOE) ? "1\n" : "0\n");

    nonzero = 0;

    for (i = 0; i < AC97_CAPTURE_SAMPLES; i++)
    {
        if (ac97_mic_buffer[i] != 0)
            nonzero++;
    }

    print("\nBuffer 1 non-zero samples = ");
    print_number(nonzero);
    print("\n");

    ac97_analyse_samples();
}
