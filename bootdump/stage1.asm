; stage1: 446-byte MBR boot code. Loads stage2 (8 sectors from LBA 1) to 0x7e00 and jumps to it.
bits 16
org 0x7c00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [bootdrv], dl
    ; try INT13 extensions
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc  .chs
    cmp bx, 0xaa55
    jne .chs
    mov dl, [bootdrv]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jnc .go
.chs:
    mov dl, [bootdrv]
    mov ax, 0x0208          ; read 8 sectors
    mov cx, 0x0002          ; cyl 0, sector 2
    xor dh, dh              ; head 0
    mov bx, 0x7e00
    int 0x13
    jc  fail
.go:
    mov dl, [bootdrv]
    jmp 0x0000:0x7e00
fail:
    mov si, msg
.l: lodsb
    or al, al
    jz .h
    mov ah, 0x0e
    int 0x10
    jmp .l
.h: hlt
    jmp .h
msg db 'stage2 load failed', 0
bootdrv db 0
align 4
dap: db 0x10, 0
     dw 8
     dw 0x7e00, 0x0000
     dd 1, 0
times 446-($-$$) db 0
