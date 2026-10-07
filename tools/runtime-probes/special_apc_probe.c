/* Verify whether a special Windows user APC interrupts translated code.
 * The target performs no calls or waits while the sender queues the APC.
 * Only after the bounded observation does it enter an alertable wait, which
 * distinguishes unsupported preemption from a missing callback/queue path.
 * No game data is used. Exit 0 requires preemptive target-thread delivery.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

struct spin_state { volatile LONG ready, release; };
static struct spin_state state;
static volatile LONG calls, wrong_thread;
static DWORD target_id;
static volatile uint64_t scratch_errors;
typedef LONG (WINAPI *queue_apc_ex)(HANDLE, HANDLE, void (CALLBACK *)(ULONG_PTR, ULONG_PTR, ULONG_PTR),
                                  ULONG_PTR, ULONG_PTR, ULONG_PTR);
extern __attribute__((sysv_abi)) uint64_t spin_without_calls(struct spin_state *);
__asm__(
    ".text\n.global spin_without_calls\nspin_without_calls:\n"
    "movabsq $0x13579bdf2468ace0, %r8\n"
    "movq %r8, -8(%rsp)\nmovq %r8, -16(%rsp)\n"
    "movq %r8, -24(%rsp)\nmovq %r8, -32(%rsp)\n"
    "movq %r8, -40(%rsp)\nmovq %r8, -48(%rsp)\n"
    "movq %r8, -56(%rsp)\nmovq %r8, -64(%rsp)\n"
    "movq %r8, -72(%rsp)\nmovq %r8, -80(%rsp)\n"
    "movq %r8, -88(%rsp)\nmovq %r8, -96(%rsp)\n"
    "movq %r8, -104(%rsp)\nmovq %r8, -112(%rsp)\n"
    "movq %r8, -120(%rsp)\nmovq %r8, -128(%rsp)\n"
    "movl $1, (%rdi)\n"
    "apc_spin:\ncmpl $0, 4(%rdi)\nje apc_spin\n"
    "xorq %rax, %rax\nmovq $16, %rcx\nleaq -128(%rsp), %r9\n"
    "apc_scratch_check:\ncmpq %r8, (%r9)\nje apc_scratch_ok\nincq %rax\n"
    "apc_scratch_ok:\naddq $8, %r9\ndecq %rcx\njnz apc_scratch_check\nret\n"
);
static void CALLBACK callback(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c) {
    if (a != 0x1234 || b != 0x5678 || c != 0x9abc || GetCurrentThreadId() != target_id)
        InterlockedIncrement(&wrong_thread);
    InterlockedIncrement(&calls);
}
static DWORD WINAPI worker(void *unused) {
    (void)unused;
    scratch_errors = spin_without_calls(&state);
    for (unsigned i = 0; i < 20 && !InterlockedCompareExchange(&calls, 0, 0); ++i)
        SleepEx(50, TRUE);
    return 0;
}
int main(void) {
    queue_apc_ex queue = (queue_apc_ex)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueueApcThreadEx");
    if (!queue) return 10;
    HANDLE target = CreateThread(NULL, 0, worker, NULL, 0, &target_id);
    if (!target) return 11;
    ULONGLONG deadline = GetTickCount64() + 5000;
    while (!InterlockedCompareExchange(&state.ready, 0, 0) && GetTickCount64() < deadline) Sleep(1);
    if (!state.ready) return 12;
    /* Reserve handle low bit 1 selects special user APC delivery. */
    LONG status = queue(target, (HANDLE)(ULONG_PTR)1, callback, 0x1234, 0x5678, 0x9abc);
    deadline = GetTickCount64() + 1000;
    while (!InterlockedCompareExchange(&calls, 0, 0) && GetTickCount64() < deadline) Sleep(1);
    LONG preemptive = InterlockedCompareExchange(&calls, 0, 0);
    InterlockedExchange(&state.release, 1);
    if (WaitForSingleObject(target, 5000) != WAIT_OBJECT_0) return 13;
    DWORD code;
    if (!GetExitCodeThread(target, &code) || code) return 14;
    CloseHandle(target);
    printf("[special-apc] status=%08lx preemptive=%ld total=%ld wrong_thread=%ld scratch=%llu\n",
        (unsigned long)status, (long)preemptive, (long)calls, (long)wrong_thread,
        (unsigned long long)scratch_errors);
    fflush(stdout);
    return status || preemptive != 1 || calls != 1 || wrong_thread || scratch_errors ? 1 : 0;
}
