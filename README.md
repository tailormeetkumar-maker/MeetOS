# MeetOS

**MeetOS** is a custom 32-bit x86 educational operating system built from scratch in C and x86 assembly.

The project is designed to explore operating-system fundamentals at a low level, including booting, protected mode, kernel development, terminal systems, keyboard input, PCI devices, networking, TCP/IP protocols, and an AI-assisted subsystem.

> **Status:** Active development
> **Architecture:** x86 / 32-bit
> **Language:** C, x86 Assembly, Python
> **Execution environment:** QEMU
> **Development environment:** Linux / WSL

---

## Overview

MeetOS started as a low-level operating-system development project and has grown into a multi-subsystem environment containing:

* Custom bootloader
* 32-bit protected-mode kernel
* Custom terminal/shell
* PS/2 keyboard input
* PCI device discovery
* RTL8139 network support
* Ethernet networking
* ARP
* IPv4
* UDP
* DNS
* TCP
* HTTP
* Annabelle.AI integration
* Roger/Matey communication subsystem
* QEMU-based development and testing

The goal is not to reproduce an existing operating system, but to understand how the different layers of a computer system interact by implementing them directly.

---

## Architecture

```text
                    +----------------------+
                    |       MeetOS         |
                    |      32-bit x86      |
                    +----------+-----------+
                               |
                    +----------v-----------+
                    |      Bootloader      |
                    |      boot.asm        |
                    +----------+-----------+
                               |
                    +----------v-----------+
                    |        Kernel        |
                    |      kernel.c        |
                    +----------+-----------+
                               |
              +----------------+----------------+
              |                |                |
      +-------v------+  +------v------+  +------v------+
      |   Terminal   |  |   Devices   |  | Networking  |
      |    Shell     |  |    PCI      |  |    Stack    |
      +-------+------+  +------+------+  +------+------+
              |                |                |
              |                |        +-------v-------+
              |                |        | Ethernet      |
              |                |        | ARP           |
              |                |        | IPv4          |
              |                |        | UDP           |
              |                |        | DNS           |
              |                |        | TCP           |
              |                |        | HTTP          |
              |                |        +---------------+
              |
      +-------+-------------------------------+
      |                                       |
+-----v------+                         +------v-------+
| Annabelle  |                         | Roger/Matey |
|    .AI     |                         | Subsystem   |
+------------+                         +--------------+
```

---

## Boot Process

The system begins with the custom x86 bootloader.

The boot process includes:

1. BIOS loads the boot sector.
2. The bootloader performs the required disk operations.
3. The processor enters 32-bit protected mode.
4. The kernel is loaded into memory.
5. Control is transferred to the MeetOS kernel.
6. The kernel initializes the available subsystems.
7. The custom terminal becomes available.

The kernel is linked using a custom linker script and converted into a raw binary before being combined with the bootloader to produce the final disk image.

---

## Kernel

The MeetOS kernel is written primarily in freestanding C.

The build avoids the normal host operating-system runtime by using flags such as:

```text
-m32
-ffreestanding
-fno-pie
-fno-stack-protector
-nostdlib
-nostdinc
```

The kernel is linked directly using:

```text
ld -m elf_i386
```

This allows the project to operate at a much lower level than a conventional user-space application.

---

## Terminal

MeetOS contains a custom terminal/shell rather than relying on a Linux or Windows shell.

The terminal provides commands for interacting with different parts of the operating system.

Examples include:

```text
help
about
version
clear
cls
echo
calc
calculator
uname
whoami
net
netsend
netrx
arp
ipv4
udp
dns
tcp
http
prompt
ai
historyai
reboot
```

The terminal also provides interaction with the Annabelle.AI subsystem.

---

## Networking Stack

One of the major parts of MeetOS is its custom networking implementation.

The project contains separate modules for several layers and protocols:

```text
RTL8139
   |
Ethernet
   |
 ARP
   |
 IPv4
   |
 +---------+
 |         |
UDP       TCP
 |         |
DNS       HTTP
```

Implemented networking components include:

* RTL8139 network controller support
* Ethernet frames
* ARP
* IPv4
* UDP
* DNS
* TCP
* HTTP

The networking code is implemented inside the MeetOS environment rather than relying on the operating system's normal networking APIs.

---

## Annabelle.AI

MeetOS includes an AI subsystem called **Annabelle.AI**.

Annabelle.AI provides an interactive AI interface from the MeetOS terminal.

The current backend uses a locally running Ollama service rather than requiring an OpenAI API key.

Current local configuration includes:

```text
Ollama
127.0.0.1:11434
Model: qwen3:4b-instruct
```

The backend maintains conversation context and communicates with the MeetOS AI interface through the project's networking components.

Annabelle.AI is intentionally kept as a separate subsystem so that the core operating-system components remain independent from the AI backend.

---

## Roger / Matey

MeetOS also contains a separate communication subsystem called **Roger**, designed to communicate with the companion **Matey** application.

The subsystem is maintained separately from Annabelle.AI.

Relevant source files include:

```text
roger.c
roger.h
```

The companion Android application is being developed separately as **Matey**.

This subsystem is an experimental part of the project and is still under active development.

---

## Hardware / Device Support

MeetOS includes low-level work with hardware and virtualized devices.

Current project components include:

* PCI device discovery
* RTL8139 network controller
* AC'97 audio device support
* PS/2 keyboard input

The project is primarily developed and tested using QEMU, which provides a convenient environment for experimenting with low-level hardware interfaces without requiring dedicated physical hardware.

---

## Project Structure

```text
MeetOS/
├── boot.asm
├── linker.ld
├── Makefile
│
├── kernel.c
│
├── ai.c
├── ai.h
├── annabelle_chat.c
├── annabelle_chat.h
├── annabelle_backend.py
│
├── pci.c
├── pci.h
├── ac97.c
├── ac97.h
│
├── rtl8139.c
├── rtl8139.h
├── ethernet.c
├── ethernet.h
│
├── arp.c
├── arp.h
├── ipv4.c
├── ipv4.h
├── udp.c
├── udp.h
├── dns.c
├── dns.h
├── tcp.c
├── tcp.h
├── tcp_transport.c
├── tcp_transport.h
├── http.c
├── http.h
│
├── roger.c
└── roger.h
```

---

## Building MeetOS

### Requirements

A Linux-based development environment is recommended.

Required tools include:

* GCC
* NASM
* GNU Binutils
* Python 3
* QEMU

For the current build system, the compiler must support 32-bit compilation.

### Build

Clone the repository:

```bash
git clone git@github.com:tailormeetkumar-maker/MeetOS.git
cd MeetOS
```

Build the operating-system image:

```bash
make clean && make
```

This generates:

```text
boot.bin
kernel.bin
kernel.raw
os-image.bin
```

Build artifacts are intentionally excluded from Git using `.gitignore`.

---

## Running in QEMU

MeetOS is designed to run under QEMU.

Use:

```bash
make run
```

The current QEMU configuration provides the virtual hardware required by the project, including the RTL8139 network device and AC'97 audio device.

---

## Development Philosophy

MeetOS is primarily a learning and systems-programming project.

The project focuses on understanding:

* How a computer boots
* How a bootloader transfers control to a kernel
* How protected mode works
* How a kernel interacts with hardware
* How keyboard input reaches a terminal
* How network packets are constructed and processed
* How protocols such as ARP, IPv4, UDP and TCP operate
* How HTTP can be implemented at a low level
* How an AI subsystem can be connected to a custom environment
* How different operating-system components communicate with each other

Rather than hiding these mechanisms behind high-level libraries, MeetOS attempts to expose and implement them directly.

---

## Current Development Status

MeetOS is an ongoing project.

The core project currently contains:

* Custom bootloader
* 32-bit kernel
* Custom terminal
* Keyboard input
* PCI support
* Networking stack
* TCP/UDP functionality
* DNS and HTTP components
* Annabelle.AI subsystem
* Roger/Matey communication subsystem

Some components are experimental and continue to evolve as the project develops.

---

## Roadmap

Possible future development includes:

* Improved memory management
* Process/task management
* More complete filesystem support
* Improved networking reliability
* Additional device drivers
* More kernel abstractions
* Improved terminal functionality
* Expanded Matey communication
* Better system documentation
* Additional debugging tools
* More hardware support

---

## Why I Built MeetOS

MeetOS is a practical exploration of computer systems from the ground up.

Instead of only using operating systems and networking APIs, the project provides hands-on experience implementing the layers underneath them.

It combines:

```text
Operating Systems
        +
Computer Architecture
        +
Networking
        +
Embedded / Low-Level Programming
        +
AI Integration
        +
Systems Programming
```

---

## Author

**Meetkumar Tailor**

Computer Engineering Student
Interested in Operating Systems, Computer Networks, Systems Programming, and Artificial Intelligence.

GitHub:

```text
https://github.com/tailormeetkumar-maker
```

---

## License

This project is currently maintained as an educational and experimental operating-system project.

License information will be added as the project evolves.
EOF

````

### 2. Check the README

Run:

```bash
wc -l README.md
````

Then:

```bash
git diff -- README.md
```

### 3. Commit it

If everything looks good:

```bash
git add README.md
git commit -m "Add professional project documentation"
git push
```

Then verify:

```bash
git status
```

You should get:

```text
nothing to commit, working tree clean
```

**This only adds `README.md`; none of your existing `.c`, `.h`, `.asm`, or build files are modified.**

After you push it, we'll do the GitHub-side polish: **repository description + topics + screenshots**, which will make the project much more presentable when an interviewer opens it.
