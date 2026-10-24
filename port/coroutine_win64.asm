; coroutine_win64.asm — context-switch primitive for the Windows x86-64
; coroutine backend (port/coroutine_win64.cpp). MASM (ml64) syntax.
;
; Win32 fibers are opaque: their stack and register state cannot be copied,
; which rollback needs (sys/netstate.c snapshots suspended GObj-thread
; coroutines). This is the same approach as coroutine_aarch64.S: save the
; callee-saved set of the Windows x64 ABI, switch stacks, return into the
; other context.
;
; Windows x64 callee-saved state:
;   GP : rbx, rbp, rdi, rsi, r12..r15, rsp
;   SSE: xmm6..xmm15 (full 128 bits)
;   control: MXCSR, x87 control word
; plus the thread's stack bounds in the TIB (gs segment), which structured
; exception handling and __chkstk consult. Fibers swap those too; so do we.
;   gs:[0x08]   NT_TIB.StackBase
;   gs:[0x10]   NT_TIB.StackLimit
;   gs:[0x1478] TEB.DeallocationStack
;
; Context layout (272 bytes). Offsets are mirrored by static_asserts on the
; C++ CoroCtx struct in coroutine_win64.cpp:
;
;     0  rsp            |  72  StackBase          | 112  xmm6   | 192  xmm11
;     8  rbx            |  80  StackLimit         | 128  xmm7   | 208  xmm12
;    16  rbp            |  88  DeallocationStack  | 144  xmm8   | 224  xmm13
;    24  rdi            |  96  mxcsr (4 bytes)    | 160  xmm9   | 240  xmm14
;    32  rsi            | 100  x87 cw (2 bytes)   | 176  xmm10  | 256  xmm15
;    40  r12            | 102  pad
;    48  r13            | 104  pad
;    56  r14            |
;    64  r15            |

EXTERN port_coroutine_trampoline_c:PROC

.code

; void port_coroutine_swap(CoroCtx *from /* rcx */, CoroCtx *to /* rdx */);
;
; Saves the caller's callee-saved state into *from with rsp pointing at the
; return address of this call, then loads *to and `ret`s through *to's saved
; rsp. The "from" context pauses at the instruction after its call; the next
; swap back into it re-executes the ret below.
port_coroutine_swap PROC
    ; ---- save "from" (rcx)
    mov     qword ptr [rcx+0],  rsp
    mov     qword ptr [rcx+8],  rbx
    mov     qword ptr [rcx+16], rbp
    mov     qword ptr [rcx+24], rdi
    mov     qword ptr [rcx+32], rsi
    mov     qword ptr [rcx+40], r12
    mov     qword ptr [rcx+48], r13
    mov     qword ptr [rcx+56], r14
    mov     qword ptr [rcx+64], r15
    mov     rax, qword ptr gs:[8]
    mov     qword ptr [rcx+72], rax
    mov     rax, qword ptr gs:[16]
    mov     qword ptr [rcx+80], rax
    mov     rax, qword ptr gs:[5240]
    mov     qword ptr [rcx+88], rax
    stmxcsr dword ptr [rcx+96]
    fnstcw  word ptr [rcx+100]
    movups  xmmword ptr [rcx+112], xmm6
    movups  xmmword ptr [rcx+128], xmm7
    movups  xmmword ptr [rcx+144], xmm8
    movups  xmmword ptr [rcx+160], xmm9
    movups  xmmword ptr [rcx+176], xmm10
    movups  xmmword ptr [rcx+192], xmm11
    movups  xmmword ptr [rcx+208], xmm12
    movups  xmmword ptr [rcx+224], xmm13
    movups  xmmword ptr [rcx+240], xmm14
    movups  xmmword ptr [rcx+256], xmm15

    ; ---- restore "to" (rdx)
    mov     rsp, qword ptr [rdx+0]
    mov     rbx, qword ptr [rdx+8]
    mov     rbp, qword ptr [rdx+16]
    mov     rdi, qword ptr [rdx+24]
    mov     rsi, qword ptr [rdx+32]
    mov     r12, qword ptr [rdx+40]
    mov     r13, qword ptr [rdx+48]
    mov     r14, qword ptr [rdx+56]
    mov     r15, qword ptr [rdx+64]
    mov     rax, qword ptr [rdx+72]
    mov     qword ptr gs:[8], rax
    mov     rax, qword ptr [rdx+80]
    mov     qword ptr gs:[16], rax
    mov     rax, qword ptr [rdx+88]
    mov     qword ptr gs:[5240], rax
    ldmxcsr dword ptr [rdx+96]
    fldcw   word ptr [rdx+100]
    movups  xmm6,  xmmword ptr [rdx+112]
    movups  xmm7,  xmmword ptr [rdx+128]
    movups  xmm8,  xmmword ptr [rdx+144]
    movups  xmm9,  xmmword ptr [rdx+160]
    movups  xmm10, xmmword ptr [rdx+176]
    movups  xmm11, xmmword ptr [rdx+192]
    movups  xmm12, xmmword ptr [rdx+208]
    movups  xmm13, xmmword ptr [rdx+224]
    movups  xmm14, xmmword ptr [rdx+240]
    movups  xmm15, xmmword ptr [rdx+256]

    ret
port_coroutine_swap ENDP

; void port_coroutine_trampoline_win64(void);
;
; First entry point of a fresh coroutine. coroutine_win64.cpp leaves the
; PortCoroutine* in r12 (callee-saved, so it survives the swap) and this
; function's address at [rsp] so the swap's `ret` lands here with
; rsp == 8 mod 16, exactly like a normal function entry. Allocate the
; 32-byte shadow space (plus 8 to realign) and call the C side, which runs
; the user entry and yields for good. int 3 traps if it ever returns.
port_coroutine_trampoline_win64 PROC
    mov     rcx, r12
    sub     rsp, 40
    call    port_coroutine_trampoline_c
    int     3
port_coroutine_trampoline_win64 ENDP

END
