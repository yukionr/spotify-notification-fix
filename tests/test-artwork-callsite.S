.text
.globl notification_close_loader
.type notification_close_loader,@function
notification_close_loader:
    push %rbx
    sub $0x90, %rsp
    mov %rdi, %rbx
    mov %rdx, 0x78(%rsp)
    movq $0, 0x70(%rsp)
    mov %rbx, %rdi
    xor %esi, %esi
    call gdk_pixbuf_loader_close@PLT
    mov $0x20, %edi
    add $0x90, %rsp
    pop %rbx
    ret
.size notification_close_loader, .-notification_close_loader
.section .note.GNU-stack,"",@progbits
