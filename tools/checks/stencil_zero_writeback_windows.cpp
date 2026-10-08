// Actual local Windows tracking, with synthetic fence/CPU interleaving; no GPU claim.
#define main OriginalWriteTrackingMain
#include "prx/libSceAgcDriver/tests/WriteTracking.cpp"
#undef main
#include <atomic>
#include <cstdio>
namespace {
unsigned flushes=0, ownFaults=0;
std::uint64_t expectedAddress=0;
std::size_t expectedBytes=0;
void flush(std::uint64_t address,std::size_t bytes) {Require(address==expectedAddress && bytes==expectedBytes,"identity commit flush range changed");++flushes;}
LONG CALLBACK recover(EXCEPTION_POINTERS* exception) {
    const auto& r=*exception->ExceptionRecord;
    if(r.ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || r.NumberParameters<2 || r.ExceptionInformation[0]!=1 || !GuestArena::GuestArenaHandleWrite_nid_postfix(r.ExceptionInformation[1]))return EXCEPTION_CONTINUE_SEARCH;
    ++ownFaults;return EXCEPTION_CONTINUE_EXECUTION;
}
void identitySharedSnapshot() {
    constexpr std::size_t length=2*Block;
    auto* memory=GuestArena::GuestArenaAllocate_nid_postfix(2*length,Block);
    auto* alias=static_cast<std::byte*>(memory)+length;
    HANDLE section=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_EXECUTE_READWRITE,0,length,nullptr);
    Require(section!=nullptr,"cannot create zero-stencil section");
    GuestArena::GuestArenaMap_nid_postfix(memory,length,section,0,PAGE_READWRITE);
    GuestArena::GuestArenaMap_nid_postfix(alias,length,section,0,PAGE_READWRITE);CloseHandle(section);
    {GuestAllocations::Mutation mutation;mutation.Add(memory,length,true,true);mutation.Add(alias,length,true,true);}
    struct Release {void* memory;void* alias;~Release() {{GuestAllocations::Mutation mutation;mutation.Remove(memory);mutation.Remove(alias);}GuestArena::GuestArenaReset_nid_postfix(memory,2*length);GuestArena::GuestArenaRelease_nid_postfix(memory,2*length);}} release{memory,alias};
    const auto base=reinterpret_cast<std::uint64_t>(memory),peer=reinterpret_cast<std::uint64_t>(alias);
    auto collectPeer=[&] {std::array<void*,128> pages{};auto count=pages.size();Require(GuestArena::GuestArenaCollectWrites_nid_postfix(peer,length,pages.data(),&count,true),"zero-stencil peer collect failed");return count;};
    std::memset(memory,0,length);CollectWritesUncached(base,length);collectPeer();
    std::vector<std::byte> original(length);Read(base,original,1);
    const auto beforeCpu=CollectWritesUncached(base,length);
    // Synthetic CPU work during the existing GPU fence: one logical byte and
    // one padding byte, deliberately in distinct shared tracker blocks.
    reinterpret_cast<volatile std::uint8_t*>(alias)[19]=0x69;
    reinterpret_cast<volatile std::uint8_t*>(alias)[Block+123]=0x77;
    Require(!UnchangedSinceCollected(base,length,beforeCpu),"alias CPU stores were hidden before identity writeback");
    collectPeer();const auto beforeCommit=CollectWritesUncached(base,length);const auto globalBeforeCommit=TrackerGeneration();const auto beforeFaults=ownFaults;
    expectedAddress=base;expectedBytes=length;flushes=0;SetFlushHook(flush);WriteChanged(base,original,original);SetFlushHook(nullptr);
    Require(flushes==1,"identity writeback omitted pending GPU flush");
    Require(ownFaults==beforeFaults,"identity writeback performed an actual host store");
    std::printf("identity generation before=%llu range=%llu after=%llu\n",static_cast<unsigned long long>(globalBeforeCommit),static_cast<unsigned long long>(beforeCommit),static_cast<unsigned long long>(TrackerGeneration()));
    // walkWrites increments its collect epoch even on a clean page. The two
    // ordered walks are required; written stamps, not that epoch, classify stores.
    Require(TrackerGeneration()==globalBeforeCommit+2 && UnchangedSince(base,length,beforeCommit) &&
        !WrittenSince(base,length,beforeCommit) && !StoredOver(base,length,beforeCommit),"identity writeback added a written stamp");
    Require(!UnchangedSinceCollected(base,length,beforeCpu),"identity writeback hid previous CPU classification");
    Require(collectPeer()==0,"identity writeback dirtied the alias");
    for(std::size_t at=0;at<length;++at) {const auto value=at==19?std::byte{0x69}:at==Block+123?std::byte{0x77}:std::byte{0};Require(static_cast<std::byte*>(memory)[at]==value && alias[at]==value,"identity commit overwrote CPU alias logical/padding bytes");}
    const auto secondBefore=CollectWritesUncached(base,length);
    expectedAddress=base;expectedBytes=length;flushes=0;SetFlushHook(flush);WriteChanged(base,original,original);SetFlushHook(nullptr);
    Require(flushes==1 && UnchangedSince(base,length,secondBefore) && !WrittenSince(base,length,secondBefore) && collectPeer()==0,"second identity commit dirtied or lost flush");
}
void invalidDestinations() {
    constexpr std::size_t length=4096;std::vector<std::byte> zero(length);
    auto* readonly=VirtualAlloc(nullptr,length,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Require(readonly!=nullptr,"readonly fixture alloc failed");std::memset(readonly,0,length);
    DWORD previous=0;Require(VirtualProtect(readonly,length,PAGE_READONLY,&previous)!=0,"readonly fixture protection failed");
    Require(!Accessible(readonly,length,true) && Accessible(readonly,length,false),"readonly fastpath admission did not reject");
    auto attempt=[&](void* pointer) {expectedAddress=reinterpret_cast<std::uint64_t>(pointer);expectedBytes=length;flushes=0;SetFlushHook(flush);bool threw=false;try {WriteChanged(expectedAddress,zero,zero);}catch(const std::runtime_error&) {threw=true;}SetFlushHook(nullptr);Require(threw && flushes==1,"identity commit skipped writable validation or flush");};
    attempt(readonly);Require(VirtualFree(readonly,0,MEM_RELEASE)!=0,"readonly fixture release failed");
    Require(!Accessible(readonly,length,true),"unmapped fastpath admission did not reject");attempt(readonly);
}
}
int main() {
    auto* handler=AddVectoredExceptionHandler(1,recover);if(!handler)return 2;
    try {Require(WriteWatched(),"actual Windows arena not watched");Require(OriginalWriteTrackingMain()==0,"existing production tracking fixture failed");identitySharedSnapshot();invalidDestinations();RemoveVectoredExceptionHandler(handler);std::puts("zero stencil identity WriteChanged: shared logical/padding CPU bytes, no written stamp/no alias dirtying, flush ordering, read-only/unmapped rejection PASS");return 0;}
    catch(const std::exception& error) {SetFlushHook(nullptr);RemoveVectoredExceptionHandler(handler);std::fprintf(stderr,"zero stencil identity failure: %s\n",error.what());return 1;}
}
