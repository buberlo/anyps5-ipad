/* Bounded same-process reads across native 16-KiB protection boundaries. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned exceptions, cases, failures;
static LONG CALLBACK count_exception(EXCEPTION_POINTERS* p) {
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION || p->ExceptionRecord->ExceptionCode==EXCEPTION_GUARD_PAGE)++exceptions;
    return EXCEPTION_CONTINUE_SEARCH;
}
static unsigned char* source;
static unsigned char* destination;
static void check_read(unsigned offset,unsigned size,int valid,const char* name) {
    SIZE_T copied=999;
    memset(destination,0xa5,0x10000);
    SetLastError(0);
    const BOOL ok=ReadProcessMemory(GetCurrentProcess(),source+offset,destination,size,&copied);
    const DWORD error=GetLastError();
    unsigned bad=(!!ok!=!!valid) || (valid && copied!=size) || (!valid && copied!=0);
    if(valid && memcmp(destination,source+offset,size))++bad;
    failures+=bad;++cases;
    printf("[self-read] case=%u name=%s offset=%u size=%u valid=%d ok=%u bytes=%llu error=%lu failures=%u exceptions=%u\n",cases,name,offset,size,valid,(unsigned)ok,(unsigned long long)copied,error,bad,exceptions);fflush(stdout);
}
int main(void) {
    source=VirtualAlloc((void*)UINT64_C(0x7400000000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    destination=VirtualAlloc((void*)UINT64_C(0x7400020000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if((uintptr_t)source!=UINT64_C(0x7400000000) || (uintptr_t)destination!=UINT64_C(0x7400020000))return 10;
    for(unsigned i=0;i<0x10000;i++)source[i]=(unsigned char)(i*37u+11u);
    void* h=AddVectoredExceptionHandler(1,count_exception);if(!h)return 11;
    check_read(0,0,1,"empty");check_read(0,1,1,"byte");check_read(16,32,1,"unaligned-vector");check_read(0,0x4000,1,"page");check_read(0x3ff0,32,1,"cross-page-readable");
    DWORD old;if(!VirtualProtect(source+0x4000,0x4000,PAGE_READONLY,&old))return 12;
    check_read(0x4000,0x4000,1,"read-only");check_read(0x3ff0,32,1,"cross-read-only");
    puts("[self-read] about to read PAGE_NOACCESS; no exception callback expected");fflush(stdout);
    if(!VirtualProtect(source+0x4000,0x4000,PAGE_NOACCESS,&old))return 12;
    check_read(0x4000,1,0,"no-access-byte");check_read(0x4000,0x4000,0,"no-access-page");check_read(0x3ff0,32,0,"cross-no-access");
    if(!VirtualProtect(source+0x4000,0x4000,PAGE_READWRITE|PAGE_GUARD,&old))return 12;
    check_read(0x4000,1,0,"guard-byte");
    if(!VirtualProtect(source+0x4000,0x4000,PAGE_READWRITE,&old))return 12;
    if(!VirtualFree(source+0x4000,0x4000,MEM_DECOMMIT))return 13;
    check_read(0x4000,1,0,"decommitted-byte");check_read(0x3ff0,32,0,"cross-decommitted");
    RemoveVectoredExceptionHandler(h);VirtualFree(source,0,MEM_RELEASE);VirtualFree(destination,0,MEM_RELEASE);
    printf("[self-read] cases=%u failures=%u exceptions=%u\n",cases,failures,exceptions);fflush(stdout);
    return failures || exceptions;
}
