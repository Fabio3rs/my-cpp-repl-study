static void __attribute__((naked)) loadFn__Z4execv();
extern "C" void *_Z4execv_ptr = reinterpret_cast<void*>(loadFn__Z4execv);

extern "C" void __attribute__ ((naked)) _Z4execv() {
    __asm__ __volatile__ (
        "jmp *%0\n"
        :
        : "r" (_Z4execv_ptr)
    );
}
static void __attribute__((naked)) loadFn__Z4execv() {

    __asm__(
        // Save all general-purpose registers
        "pushq   %rax                \n"
        "pushq   %rbx                \n"
        "pushq   %rcx                \n"
        "pushq   %rdx                \n"
        "pushq   %rsi                \n"
        "pushq   %rdi                \n"
        "pushq   %rbp                \n"
        "pushq   %r8                 \n"
        "pushq   %r9                 \n"
        "pushq   %r10                \n"
        "pushq   %r11                \n"
        "pushq   %r12                \n"
        "pushq   %r13                \n"
        "pushq   %r14                \n"
        "pushq   %r15                \n"
        "movq    %rsp, %rbp          \n" // Set Base Pointer
    );
        // Push parameters onto the stack
    __asm__ __volatile__ (
        "movq %0, %%rax"
        :
        : "r" (&_Z4execv_ptr)
    );

    __asm__(
        // Push parameters onto the stack
        "movq    %rax, %rdi          \n" // Parameter 1: pointer address
        "leaq    .LC_Z4execv(%rip), %rsi    \n" // Address of string

        // Call loadfnToPtr function
        "call    loadfnToPtr  \n" // Call loadfnToPtr function

        // Restore all general-purpose registers
        "popq    %r15                \n"
        "popq    %r14                \n"
        "popq    %r13                \n"
        "popq    %r12                \n"
        "popq    %r11                \n"
        "popq    %r10                \n"
        "popq    %r9                 \n"
        "popq    %r8                 \n"
        "popq    %rbp                \n"
        "popq    %rdi                \n"
        "popq    %rsi                \n"
        "popq    %rdx                \n"
        "popq    %rcx                \n"
        "popq    %rbx                \n"
        "popq    %rax                \n");
    __asm__ __volatile__("jmp *%0\n"
                         :
                         : "r"(_Z4execv_ptr));

    __asm__(".section .rodata            \n"
            ".LC_Z4execv:                        \n"
            ".string \"_Z4execv\"                \n");
    __asm__(".section .text            \n");
}
            
