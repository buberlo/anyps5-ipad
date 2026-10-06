// The ELF local-exec TLS addressing used by the pinned relinker. Keep the
// FS load explicit: the relinker supports loading the TCB pointer, while
// arbitrary FS store opcodes are intentionally rejected.
.section .tdata,"awT",@progbits
.p2align 3
.type fixture_initialized,@tls_object
fixture_initialized:
    .quad 0x1020304050607080
.size fixture_initialized, 8
.section .tbss,"awT",@nobits
.p2align 3
.type fixture_zero,@tls_object
fixture_zero:
    .zero 8
.size fixture_zero, 8
.text
.globl guest_tls_initialized
.hidden guest_tls_initialized
.type guest_tls_initialized,@function
guest_tls_initialized:
    movq %fs:0, %rax
    leaq fixture_initialized@tpoff(%rax), %rax
    ret
.size guest_tls_initialized, .-guest_tls_initialized
.globl guest_tls_zero
.hidden guest_tls_zero
.type guest_tls_zero,@function
guest_tls_zero:
    movq %fs:0, %rax
    leaq fixture_zero@tpoff(%rax), %rax
    ret
.size guest_tls_zero, .-guest_tls_zero
.section .note.GNU-stack,"",@progbits
