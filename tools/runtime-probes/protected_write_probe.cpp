// Own runtime diagnostic: checked AVX stores after intentional page protection.
// Build APS5_UNREPAIRED_PROBE=1 only for a bounded watchdog rejection test.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <immintrin.h>
#include <stdint.h>
static void report(const char* stage,unsigned mode,unsigned iteration,bool passed){
 char b[256];unsigned n=0;
 const auto text=[&](const char* s){while(*s)b[n++]=*s++;};
 const auto number=[&](unsigned v){char a[16];unsigned c=0;do{a[c++]=char('0'+v%10);v/=10;}while(v);while(c)b[n++]=a[--c];};
#ifdef APS5_UNREPAIRED_PROBE
 text("{\"schema\":1,\"probe\":\"unrepaired-write\",\"stage\":\"");
#else
 text("{\"schema\":1,\"probe\":\"protected-write\",\"stage\":\"");
#endif
 text(stage);text("\",\"protection\":");number(mode);text(",\"iteration\":");number(iteration);text(",\"status\":\"");text(passed?"pass":"fail");text("\"}\n");DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),b,n,&written,0);
}
__attribute__((noinline)) static void write16(void* p,unsigned i){_mm_storeu_si128((__m128i*)p,_mm_set_epi32(i+3,i+2,i+1,i));}
static void* watched;
static volatile unsigned handled;
static LONG CALLBACK repair(EXCEPTION_POINTERS* e) {
 const auto* r=e->ExceptionRecord;
 if(r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||r->NumberParameters<2||r->ExceptionInformation[0]!=1) return EXCEPTION_CONTINUE_SEARCH;
 uintptr_t at=r->ExceptionInformation[1],lo=(uintptr_t)watched;
 if(at<lo||at-lo>=0x10000) return EXCEPTION_CONTINUE_SEARCH;
 #ifdef APS5_UNREPAIRED_PROBE
 return EXCEPTION_CONTINUE_EXECUTION;
#else
 DWORD previous=0;
 if(!VirtualProtect(watched,0x10000,PAGE_READWRITE,&previous)) return EXCEPTION_CONTINUE_SEARCH;
 ++handled;
 return EXCEPTION_CONTINUE_EXECUTION;
#endif
}
extern "C" void mainCRTStartup(){
 watched=VirtualAlloc((void*)UINT64_C(0x7403000000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 report("allocate",4,0,watched==(void*)UINT64_C(0x7403000000));if(!watched)ExitProcess(1);
 void* handler=AddVectoredExceptionHandler(1,repair);report("handler",4,0,handler!=0);if(!handler)ExitProcess(2);
 void* target=(char*)watched+0x1620;
 for(unsigned i=1;i<=5000;i++){
  DWORD previous=0;
  if(!VirtualProtect(watched,0x10000,PAGE_READONLY,&previous)){report("arm",4,i,false);ExitProcess(3);}
  write16(target,i);
  const volatile unsigned* v=(const volatile unsigned*)target;
  if(v[0]!=i||v[1]!=i+1||v[2]!=i+2||v[3]!=i+3||handled!=i){report("store_and_repair",4,i,false);ExitProcess(4);}
  if(i==1||i%100==0)report("store_and_repair",4,i,true);
 }
 report("remove_handler",4,5000,RemoveVectoredExceptionHandler(handler)!=0);
 report("release",4,5000,VirtualFree(watched,0,MEM_RELEASE)!=0);
 report("complete",0,5000,true);
 ExitProcess(0);
}
