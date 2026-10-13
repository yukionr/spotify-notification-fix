.section .notification_call,"ax",@progbits
.globl notification_get_pixbuf
.type notification_get_pixbuf,@function
notification_get_pixbuf:
    push %rbx
    push %r14
    sub $0x98, %rsp
    mov %rdi, %rbx
    /* Model the full-image write preceding the notification lookup. */
    mov %rbx, %rdi
    xor %ecx, %ecx
    call notification_image_written
    mov %rbx, %rdi
    call gdk_pixbuf_loader_get_pixbuf@PLT
    mov %rax, %r14
#ifdef BROKEN_PATTERN
    mov %rax, 0x78(%rsp)
#else
    mov %rax, 0x80(%rsp)
#endif
    test %rax, %rax
    add $0x98, %rsp
    pop %r14
    pop %rbx
    ret
.size notification_get_pixbuf, .-notification_get_pixbuf
.text
.type notification_image_written,@function
notification_image_written:
    ret
.section .note.GNU-stack,"",@progbits
