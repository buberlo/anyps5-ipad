#include <windows.h>
#include <stdint.h>
#include <stdio.h>
static void* region;
static volatile LONG writes, reads, expectedWrite;
static void log_text(const char* text) { DWORD n; WriteFile(GetStdHandle(STD_ERROR_HANDLE),text,(DWORD)lstrlenA(text),&n,NULL); }
static LONG CALLBACK fault(EXCEPTION_POINTERS* p) {
 EXCEPTION_RECORD* r=p->ExceptionRecord;
 if(r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || r->NumberParameters<2 || r->ExceptionInformation[1]<(ULONG_PTR)region || r->ExceptionInformation[1]>=(ULONG_PTR)region+0x10000)return EXCEPTION_CONTINUE_SEARCH;
 if((r->ExceptionInformation[0]==1)!=expectedWrite) {log_text("[atomic-qual] WRONG access direction\n");ExitProcess(8);}
 if(expectedWrite)InterlockedIncrement(&writes);else InterlockedIncrement(&reads);
 DWORD old;if(!VirtualProtect(region,0x10000,PAGE_READWRITE,&old))ExitProcess(9);
 return EXCEPTION_CONTINUE_EXECUTION;
}
int main(void) {
 region=VirtualAlloc(NULL,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!region)return 1;
 if(!AddVectoredExceptionHandler(1,fault))return 2;
 unsigned __int128* p=(unsigned __int128*)((char*)region+0x40);*p=0;
 DWORD old;expectedWrite=1;
 if(!VirtualProtect(region,0x10000,PAGE_READONLY,&old))return 3;
 if(!__sync_bool_compare_and_swap(p,(unsigned __int128)0,((unsigned __int128)0x5678<<64)|0x1234))return 4;
 if(writes!=1 || (uint64_t)*p!=0x1234)return 5;
 if(!VirtualProtect(region,0x10000,PAGE_READONLY,&old))return 6;
 // A failed comparison remains an RMW access; the instruction may write back
 // the observed value, and therefore still requires write permission.
 if(__sync_bool_compare_and_swap(p,(unsigned __int128)0,(unsigned __int128)0))return 7;
 if(writes!=2 || (uint64_t)*p!=0x1234)return 10;
 volatile LONG* scalar=(volatile LONG*)((char*)region+0x80);*scalar=7;
 if(!VirtualProtect(region,0x10000,PAGE_READONLY,&old))return 13;
 if(__sync_fetch_and_add(scalar,2)!=7 || writes!=3 || *scalar!=9)return 14;
 if(!VirtualProtect(region,0x10000,PAGE_READONLY,&old))return 15;
 if(__sync_lock_test_and_set(scalar,12)!=9 || writes!=4 || *scalar!=12)return 16;
 if(!VirtualProtect(region,0x10000,PAGE_READONLY,&old))return 17;
 if(!__sync_bool_compare_and_swap(scalar,12,13) || writes!=5 || *scalar!=13)return 18;
 expectedWrite=0;if(!VirtualProtect(region,0x10000,PAGE_NOACCESS,&old))return 11;
 volatile uint64_t value=*(volatile uint64_t*)p;
 if(reads!=1 || value!=0x1234)return 12;
 VirtualFree(region,0,MEM_RELEASE);
 log_text("[atomic-qual] PASS: CMPXCHG16B success/failure, XADD, XCHG and CMPXCHG classified WRITE; ordinary protected load classified READ\n");
 return 0;
}
