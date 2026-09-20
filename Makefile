CC = gcc
LD = ld
NASM = nasm
OBJCOPY = objcopy

CFLAGS = -m32 \
         -ffreestanding \
         -fno-pie \
         -fno-stack-protector \
         -nostdlib \
         -nostdinc \
         -Wall \
         -Wextra

all: os-image.bin

boot.bin: boot.asm kernel_size.inc
	$(NASM) -f bin boot.asm -o boot.bin

kernel.o: kernel.c ai.h pci.h http.h roger.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

ai.o: ai.c ai.h http.h
	$(CC) $(CFLAGS) -c ai.c -o ai.o

pci.o: pci.c pci.h
	$(CC) $(CFLAGS) -c pci.c -o pci.o

rtl8139.o: rtl8139.c rtl8139.h
	$(CC) $(CFLAGS) -c rtl8139.c -o rtl8139.o

arp.o: arp.c arp.h rtl8139.h
	$(CC) $(CFLAGS) -c arp.c -o arp.o

ethernet.o: ethernet.c ethernet.h rtl8139.h
	$(CC) $(CFLAGS) -c ethernet.c -o ethernet.o

ipv4.o: ipv4.c ipv4.h rtl8139.h arp.h
	$(CC) $(CFLAGS) -c ipv4.c -o ipv4.o

udp.o: udp.c udp.h ipv4.h rtl8139.h
	$(CC) $(CFLAGS) -c udp.c -o udp.o

dns.o: dns.c dns.h udp.h rtl8139.h
	$(CC) $(CFLAGS) -c dns.c -o dns.o

tcp.o: tcp.c tcp.h rtl8139.h arp.h
	$(CC) $(CFLAGS) -c tcp.c -o tcp.o

tcp_transport.o: tcp_transport.c tcp_transport.h rtl8139.h arp.h
	$(CC) $(CFLAGS) -c tcp_transport.c -o tcp_transport.o

http.o: http.c http.h tcp_transport.h
	$(CC) $(CFLAGS) -c http.c -o http.o

annabelle_chat.o: annabelle_chat.c annabelle_chat.h ai.h
	$(CC) $(CFLAGS) -c annabelle_chat.c -o annabelle_chat.o

roger.o: roger.c roger.h
	$(CC) $(CFLAGS) -c roger.c -o roger.o

kernel.bin: kernel.o ai.o pci.o ac97.o rtl8139.o arp.o ethernet.o ipv4.o udp.o dns.o tcp.o tcp_transport.o http.o annabelle_chat.o roger.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o kernel.bin kernel.o ai.o pci.o ac97.o rtl8139.o arp.o ethernet.o ipv4.o udp.o dns.o tcp.o tcp_transport.o http.o annabelle_chat.o roger.o

kernel.raw: kernel.bin
	$(OBJCOPY) -O binary kernel.bin kernel.raw

kernel_size.inc: kernel.raw
	@python3 -c "import os; s=os.path.getsize('kernel.raw'); n=(s+511)//512; assert n>0 and n<=127; open('kernel_size.inc','w').write('%define KERNEL_SECTORS '+str(n)+'\n')"

os-image.bin: boot.bin kernel.raw
	cat boot.bin kernel.raw > os-image.bin

run: os-image.bin
	qemu-system-i386 -audiodev pa,id=audio0,in.name=RDPSource -drive format=raw,file=os-image.bin -nic user,model=rtl8139 -device AC97,audiodev=audio0,bus=pci.0

clean:
	rm -f boot.bin kernel.o ai.o pci.o ac97.o rtl8139.o arp.o ethernet.o ipv4.o udp.o dns.o tcp.o tcp_transport.o http.o annabelle_chat.o roger.o kernel.bin kernel.raw os-image.bin kernel_size.inc

.PHONY: all run clean
