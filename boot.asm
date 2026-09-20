bits 16
org 0x7C00

%include "kernel_size.inc"

start:
    cli
    mov [boot_drive], dl

    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov si, boot_message
    call print

    ; Check BIOS INT 13h extensions.
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    cmp bx, 0xAA55
    jne disk_error

    test cx, 1
    jz disk_error

    ; Load the complete kernel at physical address 0x10000.
    mov word [dap_sector_count], KERNEL_SECTORS
    mov word [dap_offset], 0x0000
    mov word [dap_segment], 0x1000
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Enter 32-bit protected mode.
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:protected_mode


disk_error:
    mov [disk_error_code], ah
    mov si, disk_error_message
    call print
    mov al, [disk_error_code]
    call print_hex
    mov si, newline
    call print

.halt:
    cli
    hlt
    jmp .halt


print:
.next:
    lodsb
    cmp al, 0
    je .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .next
.done:
    ret


print_hex:
    push ax
    push bx
    mov bl, al

    mov al, bl
    shr al, 4
    call print_hex_digit

    mov al, bl
    and al, 0x0F
    call print_hex_digit

    pop bx
    pop ax
    ret


print_hex_digit:
    cmp al, 10
    jb .number
    add al, 'A' - 10
    jmp .output
.number:
    add al, '0'
.output:
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    ret


bits 32

protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000
    mov ebp, esp

    jmp CODE_SEG:0x10000


gdt_start:

gdt_null:
    dq 0

gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

boot_drive:
    db 0

disk_error_code:
    db 0

boot_message:
    db "Booting MeetOS...", 13, 10, 0

disk_error_message:
    db "Disk error! BIOS code: ", 0

newline:
    db 13, 10, 0

; Disk Address Packet for INT 13h/AH=42h.
dap:
    db 0x10
    db 0x00

dap_sector_count:
    dw KERNEL_SECTORS

dap_offset:
    dw 0x0000

dap_segment:
    dw 0x1000

dap_lba_low:
    dd 1

dap_lba_high:
    dd 0

times 510-($-$$) db 0
dw 0xAA55
