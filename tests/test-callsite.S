.section .notification_call,"ax",@progbits
.globl notification_get_pixbuf
.type notification_get_pixbuf,@function
notification_get_pixbuf:
    sub $8, %rsp
    call gdk_pixbuf_loader_get_pixbuf@PLT
    add $8, %rsp
    ret
.size notification_get_pixbuf, .-notification_get_pixbuf
.section .note.GNU-stack,"",@progbits
