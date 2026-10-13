.section .notification_lookup,"ax",@progbits
.globl notification_lookup
.type notification_lookup,@function
notification_lookup:
    sub $8, %rsp
    call dlsym@PLT
    add $8, %rsp
    ret
.size notification_lookup, .-notification_lookup

.section .notification_update,"ax",@progbits
.globl notification_update
.type notification_update,@function
notification_update:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %rcx
    sub $8, %rsp
    call *%rax
    add $8, %rsp
    ret
.size notification_update, .-notification_update
.section .note.GNU-stack,"",@progbits
