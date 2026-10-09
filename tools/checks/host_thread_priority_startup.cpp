#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include "prx/libkernel/KernelErrors.hpp"
using DWORD = std::uint32_t;
using HANDLE = void*;
using ULONG_PTR = std::uintptr_t;
using BOOL = int;
enum { ERROR_SUCCESS = 0, ERROR_ACCESS_DENIED = 5, ERROR_INVALID_HANDLE = 6,
 ERROR_GEN_FAILURE = 31, ERROR_NOT_SUPPORTED = 50, ERROR_INVALID_PARAMETER = 87,
 ERROR_CALL_NOT_IMPLEMENTED = 120, ERROR_THREAD_NOT_IN_PROCESS = 566, ERROR_PRIVILEGE_NOT_HELD = 1314,
 MEM_COMMIT = 0x1000, PAGE_READWRITE = 4 };
static constexpr DWORD INFINITE = UINT32_MAX, WAIT_OBJECT_0 = 0;
static constexpr int SCE_OK = 0, DETACH_DETACHED = 1;
static constexpr std::size_t DEFAULT_STACK_SIZE = 1U << 20;
static std::atomic<unsigned> assertions{0}, destroyed{0}, guestEntries{0}, releaseCalls{0}, policyCalls{0};
static unsigned cases, waitCalls, closeCalls;
static bool failPriority, invalidStack;
static const char* caseName = "startup";
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
 std::cerr << "FAIL startup " << __LINE__ << ": " << #condition << " [" << caseName << "]\n"; std::exit(1); } } while (false)
struct PthreadPrivate {
 bool _detached{}; std::size_t stackSize{}; void* stackAddress{}; void* nativeHandle{};
 std::atomic<int> priority{700}; std::atomic<std::uint64_t> affinity{0};
 std::string name; std::thread::id threadId;
 ~PthreadPrivate() { ++destroyed; }
};
struct PthreadAttrPrivate { int _detachstate{}, _schedpriority{700}; std::size_t _stacksize{DEFAULT_STACK_SIZE}; std::uint64_t _affinity{}; };
using Pthread = PthreadPrivate*;
using PthreadAttr = PthreadAttrPrivate*;
using PthreadEntry = void* (*)(void*);
static thread_local PthreadPrivate* currentThread;
static std::mutex hostPriorityLock;
static Pthread* observedOutput;
static HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(std::uintptr_t{1}); }
static BOOL MockSetThreadPriority(HANDLE handle, int relative)
{
 CHECK(handle == GetCurrentThread() && currentThread != nullptr && relative == 0);
 ++policyCalls;
 return failPriority ? 0 : 1;
}
static DWORD MockGetLastError() { return ERROR_ACCESS_DENIED; }
#define APS5_PRIORITY_MOCK_WINAPI 1
#define SetThreadPriority MockSetThreadPriority
#define GetLastError MockGetLastError
#include "HostThreadPriority.hpp"
#define _WIN32 1
#define __stdcall
#define APS5_VABI
struct SYSTEM_INFO { DWORD dwPageSize, dwAllocationGranularity; };
struct MEMORY_BASIC_INFORMATION { void* BaseAddress; std::size_t RegionSize; DWORD State, Protect; };
static void GetSystemInfo(SYSTEM_INFO* info) { info->dwPageSize = 4096; info->dwAllocationGranularity = 65536; }
static void GetCurrentThreadStackLimits(ULONG_PTR* low, ULONG_PTR* high)
{ *low = 0x10000000; *high = invalidStack ? *low : 0x10200000; }
static std::size_t VirtualQuery(void* address, MEMORY_BASIC_INFORMATION* info, std::size_t size)
{
 CHECK(size == sizeof(*info));
 info->BaseAddress = address; info->RegionSize = 0x10200000 - reinterpret_cast<std::uintptr_t>(address);
 info->State = MEM_COMMIT; info->Protect = PAGE_READWRITE;
 return size;
}
static void SetThreadDescription(HANDLE, const wchar_t*) {}
static void ApplyJobAffinity(PthreadPrivate&, void*) {}
struct MockHandle { std::thread worker; unsigned exitCode{}; };
static std::uintptr_t _beginthreadex(void*, unsigned stack, unsigned (*entry)(void*), void* arg, unsigned flags, unsigned*)
{
 CHECK(stack >= DEFAULT_STACK_SIZE && flags == 0);
 auto* handle = new MockHandle;
 handle->worker = std::thread([handle, entry, arg] { handle->exitCode = entry(arg); });
 return reinterpret_cast<std::uintptr_t>(handle);
}
static DWORD WaitForSingleObject(HANDLE value, DWORD timeout)
{
 CHECK(value != nullptr && timeout == INFINITE); ++waitCalls;
 auto* handle = static_cast<MockHandle*>(value);
 CHECK(handle->worker.joinable()); handle->worker.join();
 CHECK(handle->exitCode == 0);
 return WAIT_OBJECT_0;
}
static BOOL CloseHandle(HANDLE value)
{
 CHECK(value != nullptr); ++closeCalls;
 auto* handle = static_cast<MockHandle*>(value);
 CHECK(!handle->worker.joinable()); delete handle; return 1;
}
struct ThreadArgs;
static void RunThread(std::unique_ptr<ThreadArgs>);
static void ReleaseThread(PthreadPrivate*) { ++releaseCalls; }
/* Verbatim ThreadArgs/NativeThreadArgs/StartNativeThread/scePthreadCreate. */
#include "generated_actual_startup.hpp"
static void RunThread(std::unique_ptr<ThreadArgs> args)
{
 CHECK(observedOutput && *observedOutput == args->self);
 (void)args->entry(args->arg);
}
static void* guest(void*) { ++guestEntries; return nullptr; }
static void beginCase(const char* name)
{
 ++cases; caseName = name;
 destroyed = guestEntries = releaseCalls = policyCalls = 0;
 waitCalls = closeCalls = 0; failPriority = invalidStack = false;
}
static void successCase(bool enabled)
{
 beginCase(enabled ? "actual-startup-success-publishes-before-guest" : "default-off-actual-startup-ignores-native-priority-failure");
 failPriority = !enabled;
 Pthread output = reinterpret_cast<Pthread>(std::uintptr_t{0x1234}); observedOutput = &output;
 CHECK(scePthreadCreate(&output, nullptr, guest, nullptr, nullptr) == SCE_OK);
 CHECK(output != reinterpret_cast<Pthread>(std::uintptr_t{0x1234}) && output != nullptr);
 CHECK(waitCalls == 0 && closeCalls == 0 && destroyed == 0);
 CHECK(WaitForSingleObject(output->nativeHandle, INFINITE) == WAIT_OBJECT_0);
 CHECK(guestEntries == 1 && releaseCalls == 1 && policyCalls == (enabled ? 1U : 0U));
 CHECK(CloseHandle(output->nativeHandle) != 0); delete output;
 CHECK(waitCalls == 1 && closeCalls == 1 && destroyed == 1);
}
int main(int argc, char** argv)
{
 if (argc == 2 && !std::strcmp(argv[1], "--off")) {
  CHECK(!HostThreadPriority::Enabled()); successCase(false);
 } else {
  CHECK(argc == 1 && HostThreadPriority::Enabled());
  beginCase("actual-startup-native-failure-maps-status-no-publication-no-entry-cleanup-once");
  failPriority = true;
  Pthread output = reinterpret_cast<Pthread>(std::uintptr_t{0x1234}); observedOutput = &output;
  CHECK(scePthreadCreate(&output, nullptr, guest, nullptr, nullptr) == SCE_KERNEL_ERROR_EPERM);
  CHECK(output == reinterpret_cast<Pthread>(std::uintptr_t{0x1234}));
  CHECK(policyCalls == 1 && guestEntries == 0 && releaseCalls == 0);
  CHECK(waitCalls == 1 && closeCalls == 1 && destroyed == 1);
  beginCase("existing-startup-exception-preserves-output-and-cleans-once");
  invalidStack = true; bool caught = false;
  try { (void)scePthreadCreate(&output, nullptr, guest, nullptr, nullptr); }
  catch (const std::runtime_error&) { caught = true; }
  CHECK(caught && output == reinterpret_cast<Pthread>(std::uintptr_t{0x1234}));
  CHECK(policyCalls == 0 && guestEntries == 0 && releaseCalls == 0);
  CHECK(waitCalls == 1 && closeCalls == 1 && destroyed == 1);
  successCase(true);
 }
 std::cout << "{\"passed\":true,\"cases\":" << cases << ",\"assertions\":" << assertions.load()
           << ",\"actualStartupFunctions\":true,\"realWinCalls\":false,\"deviceExecuted\":false}\n";
 return 0;
}
