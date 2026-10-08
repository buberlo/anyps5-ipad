/* Independent protected-store CPU-state probe; no game or HLE data. */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "aps5_veh_state.h"
static LONG (WINAPI *QueryState)(HANDLE,DWORD,void*,ULONG,ULONG*);
static int BridgeActive;
void *Page, *NestedPage;
static int Nest;
/* Visible to synchronous exception re-entry; the faulting store cannot reorder it. */
static volatile LONG InNested;
static unsigned NestedFaults, QueryErrors, RejectedRequests;
int UseAvx, Mutate;
static const uint64_t Mutation=UINT64_C(0x0102030405060708);
uint64_t InputFlags, InputVectors[64], OutputVectors[64], OutputGprs[16];
static uint64_t ContextGprs[16], ContextVectors[32], CallbackFlags;
static unsigned Faults, InvalidFaults;
void __attribute__((sysv_abi)) FaultState(void);
static LONG CALLBACK repair_original(EXCEPTION_POINTERS *p) {
    __asm__ volatile("pushfq; popq %0; cld" : "=r"(CallbackFlags) : : "cc");
    EXCEPTION_RECORD *e=p->ExceptionRecord;
    if(e->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || e->NumberParameters<2 ||
       e->ExceptionInformation[1]!=(ULONG_PTR)Page) return EXCEPTION_CONTINUE_SEARCH;
    if(e->ExceptionInformation[0]!=1) {++InvalidFaults; return EXCEPTION_CONTINUE_SEARCH;}
    CONTEXT *c=p->ContextRecord;
    const uint64_t g[]={c->Rax,c->Rcx,c->Rdx,c->Rbx,c->Rbp,c->Rsi,c->Rdi,c->R8,c->R9,c->R10,c->R11,c->R12,c->R13,c->R14,c->R15,c->EFlags};
    memcpy(ContextGprs,g,sizeof(g));
    memcpy(ContextVectors,&c->Xmm0,sizeof(ContextVectors));
    DWORD old;
    if(!VirtualProtect(Page,0x4000,PAGE_READWRITE,&old))return EXCEPTION_CONTINUE_SEARCH;
    ++Faults;
    if(UseAvx) __asm__ volatile("vzeroall" : : : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15");
    return EXCEPTION_CONTINUE_EXECUTION;
}
static LONG CALLBACK repair(EXCEPTION_POINTERS *p) {
    struct aps5_veh_state_info info={0};
    EXCEPTION_RECORD *e=p->ExceptionRecord;
    if(BridgeActive && e->ExceptionCode==EXCEPTION_ACCESS_VIOLATION && e->NumberParameters>=2){
        info.version=1;info.code=e->ExceptionCode;info.direction=e->ExceptionInformation[0];
        info.rip=p->ContextRecord->Rip;info.rsp=p->ContextRecord->Rsp;info.address=e->ExceptionInformation[1];
        ULONG returned=0;
        LONG status=QueryState(GetCurrentThread(),APS5_THREAD_VEH_STATE,&info,sizeof(info),&returned);
        if(status==0 && returned==sizeof(info))p->ContextRecord->EFlags=info.flags;
        else {printf("[fault-state] native snapshot missing status=%lx bytes=%lu\n",(unsigned long)status,(unsigned long)returned);info.token=0;++QueryErrors;}
    }
    if(info.token){
        struct aps5_veh_state_info invalid=info;
        ULONG bytes=0;
        if(QueryState(GetCurrentThread(),APS5_THREAD_VEH_STATE,&invalid,sizeof(invalid)-1,&bytes)!= (LONG)0xc0000004)++QueryErrors;
        else ++RejectedRequests;
        invalid.operation=1;invalid.resume_rip=info.rip;invalid.resume_rsp=info.rsp;invalid.token++;
        if(QueryState(GetCurrentThread(),APS5_THREAD_VEH_STATE,&invalid,sizeof(invalid),&bytes)!= (LONG)0xc000000d)++QueryErrors;
        else ++RejectedRequests;
        invalid.token=info.token;invalid.flags|=0x80000000u;
        if(QueryState(GetCurrentThread(),APS5_THREAD_VEH_STATE,&invalid,sizeof(invalid),&bytes)!= (LONG)0xc000000d)++QueryErrors;
        else ++RejectedRequests;
    }
    LONG result;
    if(InNested && e->ExceptionInformation[1]==(ULONG_PTR)NestedPage){
        DWORD old;
        if(!VirtualProtect(NestedPage,0x4000,PAGE_READWRITE,&old))return EXCEPTION_CONTINUE_SEARCH;
        ++NestedFaults;
        if(UseAvx)__asm__ volatile("vzeroall" : : : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15");
        result=EXCEPTION_CONTINUE_EXECUTION;
    }else{
        if(Nest){
            DWORD old;
            if(!VirtualProtect(NestedPage,0x4000,PAGE_READONLY,&old))return EXCEPTION_CONTINUE_SEARCH;
            InNested=1;
            *(volatile uint64_t*)NestedPage=UINT64_C(0x1020304050607080);
            InNested=0;
            if(*(volatile uint64_t*)NestedPage!=UINT64_C(0x1020304050607080))++QueryErrors;
        }
        result=repair_original(p);
    }
    if(result==EXCEPTION_CONTINUE_EXECUTION && info.token){
        if(Mutate && !InNested){
            p->ContextRecord->EFlags ^= 0xcd5;
            p->ContextRecord->Rdx ^= Mutation;
            if(UseAvx){
                uint64_t *lo=(uint64_t*)&p->ContextRecord->Xmm0;
                for(unsigned i=0;i<32;i++){lo[i]^=Mutation;info.ymm_upper[i]^=Mutation;}
            }
        }
        info.operation=1;info.resume_rip=p->ContextRecord->Rip;info.resume_rsp=p->ContextRecord->Rsp;info.flags=p->ContextRecord->EFlags;
        ULONG returned=0;
        LONG status=QueryState(GetCurrentThread(),APS5_THREAD_VEH_STATE,&info,sizeof(info),&returned);
        if(status)printf("[fault-state] native preparation failed status=%lx\n",(unsigned long)status);
    }
    return result;
}
__asm__(
    ".text\n"
    ".global FaultState\n"
    "FaultState:\n"
    "pushq %rbp\n"
    "pushq %rbx\n"
    "pushq %r12\n"
    "pushq %r13\n"
    "pushq %r14\n"
    "pushq %r15\n"
    "cmpl $0, UseAvx(%rip)\n"
    "je 1f\n"
    "vmovdqu InputVectors+0(%rip), %ymm0\n"
    "vmovdqu InputVectors+32(%rip), %ymm1\n"
    "vmovdqu InputVectors+64(%rip), %ymm2\n"
    "vmovdqu InputVectors+96(%rip), %ymm3\n"
    "vmovdqu InputVectors+128(%rip), %ymm4\n"
    "vmovdqu InputVectors+160(%rip), %ymm5\n"
    "vmovdqu InputVectors+192(%rip), %ymm6\n"
    "vmovdqu InputVectors+224(%rip), %ymm7\n"
    "vmovdqu InputVectors+256(%rip), %ymm8\n"
    "vmovdqu InputVectors+288(%rip), %ymm9\n"
    "vmovdqu InputVectors+320(%rip), %ymm10\n"
    "vmovdqu InputVectors+352(%rip), %ymm11\n"
    "vmovdqu InputVectors+384(%rip), %ymm12\n"
    "vmovdqu InputVectors+416(%rip), %ymm13\n"
    "vmovdqu InputVectors+448(%rip), %ymm14\n"
    "vmovdqu InputVectors+480(%rip), %ymm15\n"
    "1:\n"
    "movabsq $0x123457789abcdef0, %rax\n"
    "movabsq $0x123454789abcdef0, %rcx\n"
    "movabsq $0x123455789abcdef0, %rdx\n"
    "movabsq $0x123452789abcdef0, %rbx\n"
    "movabsq $0x123453789abcdef0, %rbp\n"
    "movabsq $0x123450789abcdef0, %rsi\n"
    "movq Page(%rip), %rdi\n"
    "movabsq $0x12345e789abcdef0, %r8\n"
    "movabsq $0x12345f789abcdef0, %r9\n"
    "movabsq $0x12345c789abcdef0, %r10\n"
    "movabsq $0x12345d789abcdef0, %r11\n"
    "movabsq $0x12345a789abcdef0, %r12\n"
    "movabsq $0x12345b789abcdef0, %r13\n"
    "movabsq $0x123458789abcdef0, %r14\n"
    "movabsq $0x123459789abcdef0, %r15\n"
    "pushq InputFlags(%rip)\n"
    "popfq\n"
    "movq %rax, (%rdi)\n"
    "movq %rax, OutputGprs+0(%rip)\n"
    "movq %rcx, OutputGprs+8(%rip)\n"
    "movq %rdx, OutputGprs+16(%rip)\n"
    "movq %rbx, OutputGprs+24(%rip)\n"
    "movq %rbp, OutputGprs+32(%rip)\n"
    "movq %rsi, OutputGprs+40(%rip)\n"
    "movq %rdi, OutputGprs+48(%rip)\n"
    "movq %r8, OutputGprs+56(%rip)\n"
    "movq %r9, OutputGprs+64(%rip)\n"
    "movq %r10, OutputGprs+72(%rip)\n"
    "movq %r11, OutputGprs+80(%rip)\n"
    "movq %r12, OutputGprs+88(%rip)\n"
    "movq %r13, OutputGprs+96(%rip)\n"
    "movq %r14, OutputGprs+104(%rip)\n"
    "movq %r15, OutputGprs+112(%rip)\n"
    "pushfq\n"
    "popq OutputGprs+120(%rip)\n"
    "cld\n"
    "cmpl $0, UseAvx(%rip)\n"
    "je 2f\n"
    "vmovdqu %ymm0, OutputVectors+0(%rip)\n"
    "vmovdqu %ymm1, OutputVectors+32(%rip)\n"
    "vmovdqu %ymm2, OutputVectors+64(%rip)\n"
    "vmovdqu %ymm3, OutputVectors+96(%rip)\n"
    "vmovdqu %ymm4, OutputVectors+128(%rip)\n"
    "vmovdqu %ymm5, OutputVectors+160(%rip)\n"
    "vmovdqu %ymm6, OutputVectors+192(%rip)\n"
    "vmovdqu %ymm7, OutputVectors+224(%rip)\n"
    "vmovdqu %ymm8, OutputVectors+256(%rip)\n"
    "vmovdqu %ymm9, OutputVectors+288(%rip)\n"
    "vmovdqu %ymm10, OutputVectors+320(%rip)\n"
    "vmovdqu %ymm11, OutputVectors+352(%rip)\n"
    "vmovdqu %ymm12, OutputVectors+384(%rip)\n"
    "vmovdqu %ymm13, OutputVectors+416(%rip)\n"
    "vmovdqu %ymm14, OutputVectors+448(%rip)\n"
    "vmovdqu %ymm15, OutputVectors+480(%rip)\n"
    "vzeroupper\n"
    "2:\n"
    "popq %r15\n"
    "popq %r14\n"
    "popq %r13\n"
    "popq %r12\n"
    "popq %rbx\n"
    "popq %rbp\n"
    "ret\n"
);
int main(void) {
    __builtin_cpu_init();
    const int avx=__builtin_cpu_supports("avx")!=0;
    Page=VirtualAlloc((void*)UINT64_C(0x7400000000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if((uintptr_t)Page!=UINT64_C(0x7400000000))return 10;
    NestedPage=(char*)Page+0x4000;
    BridgeActive=getenv("APS5_VEH_CONTEXT_BRIDGE") && strcmp(getenv("APS5_VEH_CONTEXT_BRIDGE"),"1")==0;
    QueryState=(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQueryInformationThread");
    if(BridgeActive && !QueryState)return 14;
    void *h=AddVectoredExceptionHandler(1,repair);if(!h)return 11;
    for(unsigned i=0;i<64;i++)InputVectors[i]=UINT64_C(0xa1b2c3d4e5f60718) ^ ((uint64_t)(i+1)*UINT64_C(0x0101010101010101));
    unsigned errors=0,cases=0;
    const uint64_t flags[]={0x202,0xad7,0xed7};
    for(Nest=0;Nest<=BridgeActive;Nest++)for(Mutate=0;Mutate<=BridgeActive;Mutate++)for(UseAvx=0;UseAvx<=avx;UseAvx++)for(unsigned f=0;f<3;f++){
        InputFlags=flags[f];memset(OutputGprs,0,sizeof(OutputGprs));memset(OutputVectors,0,sizeof(OutputVectors));
        DWORD old; if(!VirtualProtect(Page,0x4000,PAGE_READONLY,&old))return 12;
        FaultState();
        unsigned gprErrors=0,vectorErrors=0,contextVectorErrors=0;
        for(unsigned i=0;i<15;i++){
            const uint64_t expected=i==6?(uintptr_t)Page:UINT64_C(0x123456789abcdef0)^((uint64_t)(i+1)<<40);
            if(ContextGprs[i]!=expected || OutputGprs[i]!=(expected ^ ((Mutate && i==2)?Mutation:0))){++gprErrors;printf("GPR=%u expected=%llx context=%llx resumed=%llx\n",i,(unsigned long long)expected,(unsigned long long)ContextGprs[i],(unsigned long long)OutputGprs[i]);}
        }
        if(UseAvx)for(unsigned i=0;i<64;i++){
            if(OutputVectors[i]!=(InputVectors[i]^(Mutate?Mutation:0))){++vectorErrors;printf("YMM=%u lane=%u expected=%llx resumed=%llx\n",i/4,i%4,(unsigned long long)InputVectors[i],(unsigned long long)OutputVectors[i]);}
            if(i%4<2 && ContextVectors[(i/4)*2+i%4]!=InputVectors[i])++contextVectorErrors;
        }
        unsigned flagErrors=(ContextGprs[15]&0xcd5)!=(InputFlags&0xcd5) || (OutputGprs[15]&0xcd5)!=((InputFlags^(Mutate?0xcd5:0))&0xcd5);
        unsigned callbackError=(CallbackFlags&0x400)!=0;
        unsigned dataError=*(uint64_t*)Page!=UINT64_C(0x123457789abcdef0);
        errors+=gprErrors+vectorErrors+contextVectorErrors+flagErrors+callbackError+dataError;
        ++cases;
        printf("[fault-state] case=%u avx=%d gpr_errors=%u vector_errors=%u saved_xmm_errors=%u flags=%llx/%llx/%llx callback_df=%u data_error=%u faults=%u\n",cases,UseAvx,gprErrors,vectorErrors,contextVectorErrors,(unsigned long long)(InputFlags&0xcd5),(unsigned long long)(ContextGprs[15]&0xcd5),(unsigned long long)(OutputGprs[15]&0xcd5),callbackError,dataError,Faults);fflush(stdout);
    }
    RemoveVectoredExceptionHandler(h);VirtualFree(Page,0,MEM_RELEASE);
    printf("[fault-state] cases=%u faults=%u errors=%u AVX=%s\n",cases,Faults,errors,avx?"verified":"skipped");fflush(stdout);
    printf("[fault-state] nested_faults=%u rejected_requests=%u query_errors=%u\n",NestedFaults,RejectedRequests,QueryErrors);fflush(stdout);
    return errors || InvalidFaults || Faults!=cases || NestedFaults!=(BridgeActive?cases/2:0) || QueryErrors || (BridgeActive && RejectedRequests!=3*(Faults+NestedFaults));
}
