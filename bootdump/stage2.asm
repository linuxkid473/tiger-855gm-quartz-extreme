; stage2: dump Intel 855GM state (PCI config of 00:00.0, 00:02.0, 00:02.1 and key MMIO ranges) to
; a RAM buffer, write it to the boot disk at DUMP_LBA (32 sectors), and show a summary on screen.
bits 16
org 0x7e00

DUMP_LBA  equ 1228800
DUMP_SECS equ 32
BUFSEG    equ 0x2000

s2:
    mov [drv], dl
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, m_banner
    call puts

    ; ---- A20 + unreal mode (FS gets a 4 GB limit) ----
    in al, 0x92
    or al, 2
    out 0x92, al
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short $+2
    mov bx, 8
    mov fs, bx
    mov eax, cr0
    and al, 0xfe
    mov cr0, eax
    jmp 0x0000:.rm
.rm:
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7000
    mov ax, BUFSEG
    mov es, ax
    xor di, di

    ; ---- buffer header ----
    mov dword [es:di], 'GDMP'
    mov dword [es:di+4], 1
    add di, 8

    ; ---- PCI sections (bus 0): 00:00.0, 00:02.0, 00:02.1 ----
    mov ebx, 0x80000000
    call dump_pci
    mov ebx, 0x80001000
    call dump_pci
    mov ebx, 0x80001100
    call dump_pci

    ; ---- is it an Intel GPU at 00:02.0 ? get MMIO base from BAR1 ----
    mov eax, 0x80001000
    call pcird
    cmp ax, 0x8086
    jne .nommio
    mov eax, 0x80001014
    call pcird
    and eax, 0xfffffff0
    cmp eax, 0xfffffff0
    je  .nommio
    test eax, eax
    jz  .nommio
    mov ebp, eax
    mov esi, 0x2000
    mov ecx, 256
    call dump_mmio
    mov esi, 0x6000
    mov ecx, 256
    call dump_mmio
    mov esi, 0x61000
    mov ecx, 256
    call dump_mmio
    mov esi, 0x70000
    mov ecx, 2048
    call dump_mmio
.nommio:
    mov dword [es:di], 'END!'
    add di, 4
    sti

    ; ---- save to disk ----
    call save_dump
    ; ---- screen summary ----
    call summary
    mov si, m_key
    call puts
    xor ax, ax
    int 0x16
    ; warm reboot
    int 0x19
    jmp $

; ---------------------------------------------------------------- helpers
pcird:                       ; eax = config address -> eax = value
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    in eax, dx
    ret

dump_pci:                    ; ebx = 0x80000000|bus<<16|dev<<11|fn<<8
    mov dword [es:di], 'PCI '
    mov [es:di+4], ebx
    add di, 8
    xor esi, esi
.l: lea eax, [ebx+esi]
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    in eax, dx
    stosd
    add esi, 4
    cmp esi, 256
    jb .l
    ret

dump_mmio:                   ; ebp = MMIO base, esi = start offset, ecx = dword count
    mov dword [es:di], 'MMIO'
    mov [es:di+4], esi
    mov [es:di+8], ecx
    add di, 12
    add esi, ebp
.l: mov eax, [fs:esi]
    stosd
    add esi, 4
    dec ecx
    jnz .l
    ret

save_dump:
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, [drv]
    int 0x13
    jc .chs
    cmp bx, 0xaa55
    jne .chs
    mov dl, [drv]
    mov si, dap2
    mov ah, 0x43
    xor al, al
    int 0x13
    jc .fail
    mov si, m_ok
    jmp puts
.chs:
    mov dl, [drv]
    mov ah, 0x08
    int 0x13
    jc .fail
    movzx ax, dh
    inc ax
    mov [heads], ax
    and cl, 0x3f
    movzx ax, cl
    mov [spt], ax
    xor cx, cx               ; sector index 0..DUMP_SECS-1
.sl:
    push cx
    movzx eax, cx
    add eax, DUMP_LBA
    movzx ebx, word [spt]
    xor edx, edx
    div ebx                  ; eax = lba/spt, edx = lba%spt
    inc dx                   ; sector number 1-based
    mov cl, dl               ; sector (bits 0-5)
    push cx
    movzx ebx, word [heads]
    xor edx, edx
    div ebx                  ; eax = cylinder, edx = head
    pop cx
    mov dh, dl               ; head
    mov ch, al               ; cyl low
    shl ah, 6
    or cl, ah                ; cyl high bits
    mov dl, [drv]
    pop bx                   ; sector index
    push bx
    shl bx, 9                ; buffer offset
    mov ax, BUFSEG
    mov es, ax
    mov ax, 0x0301
    int 0x13
    pop cx
    jc .fail
    inc cx
    cmp cx, DUMP_SECS
    jb .sl
    mov si, m_ok
    jmp puts
.fail:
    mov si, m_fail
    jmp puts

puts:                        ; ds:si = zero-terminated string
    lodsb
    or al, al
    jz .d
    mov ah, 0x0e
    mov bx, 7
    int 0x10
    jmp puts
.d: ret

phex32:                      ; print eax as 8 hex digits
    push cx
    mov cx, 8
.l: rol eax, 4
    push eax
    and al, 0x0f
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p: mov ah, 0x0e
    mov bx, 7
    int 0x10
    pop eax
    dec cx
    jnz .l
    pop cx
    ret

pnl:
    mov si, m_nl
    jmp puts

summary:                     ; show PCI 00:02.0 dwords 0..15 and MMIO 0x2000.. first 64 dwords, 8 per line
    mov ax, BUFSEG
    mov es, ax
    ; PCI sections: header 8 bytes, each section 8 + 256 = 264 bytes. section1 data at 8+264+8 = 280
    mov si, m_pci
    call puts
    mov di, 280
    mov cx, 2
    call lines
    ; MMIO section 0 data at 8 + 3*264 + 12 = 812
    mov si, m_eng
    call puts
    mov di, 812
    mov cx, 8
    call lines
    ret

lines:                       ; cx lines, 8 dwords each from es:di
.o: push cx
    mov cx, 8
.i: mov eax, [es:di]
    add di, 4
    call phex32
    mov ax, 0x0e20
    mov bx, 7
    int 0x10
    dec cx
    jnz .i
    call pnl
    pop cx
    dec cx
    jnz .o
    ret

; ---------------------------------------------------------------- data
drv     db 0
heads   dw 0
spt     dw 0
align 4
dap2:   db 0x10, 0
        dw DUMP_SECS
        dw 0x0000, BUFSEG
        dd DUMP_LBA, 0
gdtr:   dw gdt_end - gdt - 1
        dd gdt
align 8
gdt:    dq 0
        dw 0xffff, 0x0000
        db 0x00, 0x92, 0xcf, 0x00      ; 4 GB flat data
gdt_end:
m_banner db 'GPU dumper (855GM). Reading PCI + MMIO...', 13, 10, 0
m_ok     db 'Dump written to disk (LBA 1228800, 32 sectors).', 13, 10, 0
m_fail   db 'DISK WRITE FAILED - photograph the screen instead.', 13, 10, 0
m_pci    db 13, 10, 'PCI 00:02.0 first 16 dwords:', 13, 10, 0
m_eng    db 13, 10, 'MMIO 0x2000.. first 64 dwords:', 13, 10, 0
m_key    db 13, 10, 'Press any key to reboot.', 13, 10, 0
m_nl     db 13, 10, 0
times 4096-($-$$) db 0
