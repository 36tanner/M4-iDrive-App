/* Process entry for QNX Neutrino 6.5 armle-v7, written for this project. The order of calls follows the
 * libc start-up protocol visible in the stock v0.7.6 workers: ldqnx passes its cleanup routine in r0 and
 * argc, argv, envp and auxv on the stack. */
        .syntax unified
        .arm
        .section .note.qnx,"a",%note
        .balign 4
        .word 4, 8, 3           /* name size, descriptor size, QNT_STACK */
        .ascii "QNX\0"
        .word 0, 0x100000       /* the same stack note the stock workers carry */

        .text
        .global _start
        .type _start, %function
_start:
        mov     r4, r0                  /* cleanup routine from the loader */
        ldr     r0, [sp]                /* argc */
        add     r1, sp, #4              /* argv */
        add     r2, r1, r0, lsl #2
        add     r2, r2, #4              /* envp */
        mov     r3, r2
1:      ldr     r12, [r3], #4           /* walk past envp's terminator: r3 = auxv */
        teq     r12, #0
        bne     1b
        str     r4, [sp, #-4]!          /* fifth argument */
        mov     r4, r0
        mov     r5, r1
        mov     r6, r2
        bl      _init_libc              /* (argc, argv, envp, auxv, cleanup) */
        add     sp, sp, #4
        ldr     r0, =__preinit_array_start
        ldr     r1, =__preinit_array_end
        bl      _preinit_array
        ldr     r0, =_fini
        bl      atexit
        ldr     r0, =__fini_array_start
        ldr     r1, =__fini_array_end
        bl      _fini_array
        bl      _init
        ldr     r0, =__init_array_start
        ldr     r1, =__init_array_end
        bl      _init_array
        bl      __get_errno_ptr         /* errno = 0 before main, as the stock start-up does */
        mov     r1, #0
        str     r1, [r0]
        mov     r0, r4
        mov     r1, r5
        mov     r2, r6
        bl      main
        bl      exit
2:      b       2b
        .size _start, . - _start
        .ltorg

        .section .init,"ax",%progbits
        .global _init
        .type _init, %function
_init:  push    {lr}
        pop     {pc}

        .section .fini,"ax",%progbits
        .global _fini
        .type _fini, %function
_fini:  push    {lr}
        pop     {pc}
