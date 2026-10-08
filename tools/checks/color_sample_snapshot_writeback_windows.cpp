// Actual production Windows Read/WriteChanged/GuestArena checks. CPU fence
// interleaving is synthetic; this fixture does not run Vulkan or a guest game.
#define main PreservedWriteTrackingMain
#include "prx/libSceAgcDriver/tests/WriteTracking.cpp"
#undef main
namespace {
std::uint64_t expectedAddress=0;std::size_t expectedSize=0;unsigned flushes=0;bool updatePending=false;
LONG CALLBACK recover(EXCEPTION_POINTERS* exception){const auto& r=*exception->ExceptionRecord;if(r.ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||r.NumberParameters<2||r.ExceptionInformation[0]!=1||!GuestArena::GuestArenaHandleWrite_nid_postfix(r.ExceptionInformation[1]))return EXCEPTION_CONTINUE_SEARCH;return EXCEPTION_CONTINUE_EXECUTION;}
void pending(std::uint64_t address,std::size_t bytes){Require(address==expectedAddress&&bytes==expectedSize,"snapshot pending flush range changed");++flushes;if(updatePending)reinterpret_cast<std::byte*>(address)[13]=std::byte{0x91};}
void readIntoOwnedBaseline(){
 constexpr std::size_t n=65536;auto* source=static_cast<std::byte*>(VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Require(source!=nullptr,"snapshot source allocation failed");
 struct Release{void* p;~Release(){SetFlushHook(nullptr);if(p)VirtualFree(p,0,MEM_RELEASE);}}release{source};
 std::memset(source,0x4b,n);std::vector<std::byte> baseline(n),result(n);expectedAddress=reinterpret_cast<std::uint64_t>(source);expectedSize=n;flushes=0;updatePending=true;SetFlushHook(pending);Read(expectedAddress,baseline,1);SetFlushHook(nullptr);
 Require(flushes==1&&baseline[13]==std::byte{0x91},"owned baseline was read before pending producer flush");
 result=baseline;source[13]=std::byte{0x77};source[n-1]=std::byte{0x88};Require(baseline[13]==std::byte{0x91}&&baseline.back()==std::byte{0x4b},"newer CPU store changed owned baseline");
 // Readable read-only memory is accepted by Read. The ordinary post-fence
 // changed-byte output check, rather than snapshot construction, rejects it.
 DWORD previous=0;Require(VirtualProtect(source,n,PAGE_READONLY,&previous)!=0,"snapshot readonly protection failed");flushes=0;updatePending=false;SetFlushHook(pending);Read(expectedAddress,result,1);SetFlushHook(nullptr);Require(flushes==1&&result[13]==std::byte{0x77}&&result.back()==std::byte{0x88},"readonly baseline Read was not accepted");
 auto rejectCommit=[&]{bool threw=false;flushes=0;SetFlushHook(pending);try{WriteChanged(expectedAddress,baseline,baseline);}catch(const std::runtime_error&){threw=true;}SetFlushHook(nullptr);Require(threw&&flushes==1,"readonly/unmapped result commit bypassed writable validation or flush");};rejectCommit();
 Require(VirtualFree(source,0,MEM_RELEASE)!=0,"snapshot unmap failed");release.p=nullptr;bool threw=false;flushes=0;SetFlushHook(pending);try{Read(expectedAddress,result,1);}catch(const std::runtime_error&){threw=true;}SetFlushHook(nullptr);Require(threw&&flushes==1,"unmapped source Read bypassed range validation or flush");rejectCommit();
}
}
int main(){auto* handler=AddVectoredExceptionHandler(1,recover);if(!handler)return 2;try{Require(PreservedWriteTrackingMain()==0,"existing actual tracking suite failed");readIntoOwnedBaseline();RemoveVectoredExceptionHandler(handler);std::puts("color snapshot actual Read/WriteChanged PASS pendingflush-beforecopy immutableownedbaseline readonlyRead-accepted readonlyCommit-rejected unmappedReadCommit-rejected existingGPUchangedCPUaliaspadding-stamps-retained");return 0;}catch(const std::exception&e){SetFlushHook(nullptr);RemoveVectoredExceptionHandler(handler);std::fprintf(stderr,"color snapshot Windows FAIL %s\n",e.what());return 1;}}
