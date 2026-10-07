/* SysV leaf red-zone preservation across a handled x64 write fault.
 * Bit i of mismatch corresponds to RSP - 8*(i+1). The explicit assembly
 * owns all 128 bytes; no compiler-generated call occurs in that leaf.
 * Four writable controls must not fault. Four protected writes must each
 * fault exactly once, resume the store, preserve all slots, and exit 0.
 * Native Windows/Wine SEH is a separate ABI path, not an iPad reference pass.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
static volatile uint64_t *target;
static LONG faults;
extern __attribute__((sysv_abi)) uint64_t redzone_fault(volatile uint64_t *,uint64_t);
__asm__(
    ".text\n"
    ".global redzone_fault\n"
    "redzone_fault:\n"
    "movq %rsi, -8(%rsp)\n"
    "movq %rsi, -16(%rsp)\n"
    "movq %rsi, -24(%rsp)\n"
    "movq %rsi, -32(%rsp)\n"
    "movq %rsi, -40(%rsp)\n"
    "movq %rsi, -48(%rsp)\n"
    "movq %rsi, -56(%rsp)\n"
    "movq %rsi, -64(%rsp)\n"
    "movq %rsi, -72(%rsp)\n"
    "movq %rsi, -80(%rsp)\n"
    "movq %rsi, -88(%rsp)\n"
    "movq %rsi, -96(%rsp)\n"
    "movq %rsi, -104(%rsp)\n"
    "movq %rsi, -112(%rsp)\n"
    "movq %rsi, -120(%rsp)\n"
    "movq %rsi, -128(%rsp)\n"
    "movq $123, (%rdi)\n"
    "xorl %eax, %eax\n"
    "cmpq %rsi, -8(%rsp)\n"
    "je redzone_ok_0\n"
    "orq $1, %rax\n"
    "redzone_ok_0:\n"
    "cmpq %rsi, -16(%rsp)\n"
    "je redzone_ok_1\n"
    "orq $2, %rax\n"
    "redzone_ok_1:\n"
    "cmpq %rsi, -24(%rsp)\n"
    "je redzone_ok_2\n"
    "orq $4, %rax\n"
    "redzone_ok_2:\n"
    "cmpq %rsi, -32(%rsp)\n"
    "je redzone_ok_3\n"
    "orq $8, %rax\n"
    "redzone_ok_3:\n"
    "cmpq %rsi, -40(%rsp)\n"
    "je redzone_ok_4\n"
    "orq $16, %rax\n"
    "redzone_ok_4:\n"
    "cmpq %rsi, -48(%rsp)\n"
    "je redzone_ok_5\n"
    "orq $32, %rax\n"
    "redzone_ok_5:\n"
    "cmpq %rsi, -56(%rsp)\n"
    "je redzone_ok_6\n"
    "orq $64, %rax\n"
    "redzone_ok_6:\n"
    "cmpq %rsi, -64(%rsp)\n"
    "je redzone_ok_7\n"
    "orq $128, %rax\n"
    "redzone_ok_7:\n"
    "cmpq %rsi, -72(%rsp)\n"
    "je redzone_ok_8\n"
    "orq $256, %rax\n"
    "redzone_ok_8:\n"
    "cmpq %rsi, -80(%rsp)\n"
    "je redzone_ok_9\n"
    "orq $512, %rax\n"
    "redzone_ok_9:\n"
    "cmpq %rsi, -88(%rsp)\n"
    "je redzone_ok_10\n"
    "orq $1024, %rax\n"
    "redzone_ok_10:\n"
    "cmpq %rsi, -96(%rsp)\n"
    "je redzone_ok_11\n"
    "orq $2048, %rax\n"
    "redzone_ok_11:\n"
    "cmpq %rsi, -104(%rsp)\n"
    "je redzone_ok_12\n"
    "orq $4096, %rax\n"
    "redzone_ok_12:\n"
    "cmpq %rsi, -112(%rsp)\n"
    "je redzone_ok_13\n"
    "orq $8192, %rax\n"
    "redzone_ok_13:\n"
    "cmpq %rsi, -120(%rsp)\n"
    "je redzone_ok_14\n"
    "orq $16384, %rax\n"
    "redzone_ok_14:\n"
    "cmpq %rsi, -128(%rsp)\n"
    "je redzone_ok_15\n"
    "orq $32768, %rax\n"
    "redzone_ok_15:\n"
    "ret\n"
);
static LONG CALLBACK handler(EXCEPTION_POINTERS *info) {
 EXCEPTION_RECORD *r=info->ExceptionRecord;
 if(r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || r->NumberParameters<2 || r->ExceptionInformation[0]!=1 || r->ExceptionInformation[1]!=(ULONG_PTR)target) return EXCEPTION_CONTINUE_SEARCH;
 DWORD old;if(!VirtualProtect((void*)target,0x4000,PAGE_READWRITE,&old)) return EXCEPTION_CONTINUE_SEARCH;
 ++faults;return EXCEPTION_CONTINUE_EXECUTION;
}
int main(void) {
 target=VirtualAlloc((void*)UINT64_C(0x7400000000),0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if((uintptr_t)target!=UINT64_C(0x7400000000))return 10;
 void *veh=AddVectoredExceptionHandler(1,handler);if(!veh)return 11;
 unsigned failures=0;
 for(unsigned mode=0;mode<2;++mode)for(unsigned i=0;i<4;++i) {
  DWORD old;if(!VirtualProtect((void*)target,0x4000,mode?PAGE_READONLY:PAGE_READWRITE,&old))return 12;
  uint64_t mismatch=redzone_fault(target,UINT64_C(0x13579bdf2468ace0)+i);
  printf("[redzone] protected=%u iteration=%u mismatch=%llx faults=%ld\n",mode,i,(unsigned long long)mismatch,(long)faults);fflush(stdout);
  if(mismatch || *target!=123 || faults!=(LONG)(mode?i+1:0))++failures;
 }
 RemoveVectoredExceptionHandler(veh);VirtualFree((void*)target,0,MEM_RELEASE);
 printf("[redzone] checks=8 failures=%u faults=%ld\n",failures,(long)faults);fflush(stdout);
 return failures?1:0;
}
