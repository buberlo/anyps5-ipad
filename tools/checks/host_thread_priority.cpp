#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include "prx/libkernel/KernelErrors.hpp"

using DWORD = std::uint32_t;
using HANDLE = void*;
using BOOL = int;
enum {
 ERROR_SUCCESS = 0, ERROR_ACCESS_DENIED = 5, ERROR_INVALID_HANDLE = 6,
 ERROR_GEN_FAILURE = 31, ERROR_NOT_SUPPORTED = 50, ERROR_INVALID_PARAMETER = 87,
 ERROR_CALL_NOT_IMPLEMENTED = 120, ERROR_THREAD_NOT_IN_PROCESS = 566,
 ERROR_PRIVILEGE_NOT_HELD = 1314,
};
struct PthreadPrivate {
 std::atomic<int> priority{700}; std::atomic<bool> _finished{false}; void* nativeHandle{};
};
using Pthread = PthreadPrivate*;
static constexpr int SCE_OK = 0;
#define APS5_VABI
static std::atomic<unsigned> assertions{0};
static unsigned cases;
static const char* caseName = "startup";
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
 std::cerr << "FAIL " << __LINE__ << ": " << #condition << " [" << caseName << "]\n"; std::exit(1); } } while (false)
static std::atomic<unsigned> winCalls{0}, errorCalls{0}, activeCalls{0};
static DWORD configuredError;
static bool failApi;
static thread_local DWORD lastError;
struct Event { int relative, oldGuest; };
static std::mutex mockLock;
static std::condition_variable mockCondition;
static std::vector<Event> events;
static bool blockFirst, firstEntered, releaseFirst, secondStarting;
static BOOL MockSetThreadPriority(HANDLE handle, int relative)
{
 CHECK(handle != nullptr);
 CHECK(activeCalls.fetch_add(1) == 0);
 ++winCalls;
 auto* owner = static_cast<PthreadPrivate*>(handle);
 std::unique_lock<std::mutex> lock(mockLock);
 events.push_back({relative, owner->priority.load()});
 if (blockFirst && events.size() == 1) {
  firstEntered = true; mockCondition.notify_all();
  mockCondition.wait(lock, [] { return releaseFirst; });
 }
 lastError = configuredError;
 lock.unlock();
 CHECK(activeCalls.fetch_sub(1) == 1);
 return failApi ? 0 : 1;
}
static DWORD MockGetLastError() { ++errorCalls; return lastError; }
#define APS5_PRIORITY_MOCK_WINAPI 1
#define SetThreadPriority MockSetThreadPriority
#define GetLastError MockGetLastError
#include "HostThreadPriority.hpp"
#ifndef TEST_NON_WINDOWS
#define _WIN32 1
#endif
/* The mutex declaration and setter body are extracted verbatim from candidate. */
#include "generated_actual_setter.hpp"

static void beginCase(const char* name)
{
 caseName = name; ++cases;
 winCalls = errorCalls = activeCalls = 0;
 configuredError = ERROR_SUCCESS; failApi = false;
 std::lock_guard<std::mutex> lock(mockLock);
 events.clear(); blockFirst = firstEntered = releaseFirst = secondStarting = false;
}
static void mapping()
{
 beginCase("int64-saturated-mapping-default-and-deadband");
 const struct { int guest, relative; } values[] = {
  {INT_MIN, 2}, {INT_MAX, -2}, {700, 0}, {256, 2}, {260, 2}, {767, 0},
  {573, 0}, {827, 0}, {572, 1}, {828, -1}, {444, 2}, {956, -2},
 };
 for (const auto& value : values) CHECK(HostThreadPriority::Relative(value.guest) == value.relative);
 CHECK(winCalls == 0 && errorCalls == 0);
 CHECK(HostThreadPriority::GuestError(ERROR_SUCCESS) == SCE_OK);
}
[[maybe_unused]] static void gate(bool expected)
{
 beginCase("exact-cached-gate-with-default-off-no-win-calls");
 CHECK(HostThreadPriority::Enabled() == expected);
 CHECK(setenv("APS5_HOST_THREAD_PRIORITY", expected ? "0" : "1", 1) == 0);
 CHECK(HostThreadPriority::Enabled() == expected);
 PthreadPrivate thread; thread.nativeHandle = &thread;
 CHECK(HostThreadPriority::Apply(thread.nativeHandle, 256) == ERROR_SUCCESS);
 CHECK(winCalls == (expected ? 1U : 0U));
 CHECK(errorCalls == 0);
 if (!expected) {
  thread.nativeHandle = nullptr; thread._finished = true;
  CHECK(scePthreadSetprio(&thread, 767) == SCE_OK && thread.priority == 767);
  CHECK(winCalls == 0);
 }
}
#ifndef TEST_NON_WINDOWS
static void enabledCases()
{
 beginCase("successful-native-apply-precedes-guest-commit");
 PthreadPrivate thread; thread.nativeHandle = &thread;
 CHECK(scePthreadSetprio(&thread, 256) == SCE_OK && thread.priority == 256);
 CHECK(events.size() == 1 && events[0].relative == 2 && events[0].oldGuest == 700);
 CHECK(winCalls == 1 && errorCalls == 0);

 beginCase("null-finished-and-handleless-reject-before-native-call");
 CHECK(scePthreadSetprio(nullptr, 256) == SCE_KERNEL_ERROR_EINVAL);
 thread.priority = 700; thread._finished = true;
 CHECK(scePthreadSetprio(&thread, 256) == SCE_KERNEL_ERROR_ESRCH && thread.priority == 700);
 thread._finished = false; thread.nativeHandle = nullptr;
 CHECK(scePthreadSetprio(&thread, 256) == SCE_KERNEL_ERROR_ESRCH && thread.priority == 700);
 CHECK(winCalls == 0 && errorCalls == 0);

 const struct { DWORD native; int guest; } errors[] = {
  {ERROR_ACCESS_DENIED, SCE_KERNEL_ERROR_EPERM}, {ERROR_PRIVILEGE_NOT_HELD, SCE_KERNEL_ERROR_EPERM},
  {ERROR_INVALID_HANDLE, SCE_KERNEL_ERROR_ESRCH}, {ERROR_THREAD_NOT_IN_PROCESS, SCE_KERNEL_ERROR_ESRCH},
  {ERROR_INVALID_PARAMETER, SCE_KERNEL_ERROR_EINVAL}, {ERROR_NOT_SUPPORTED, SCE_KERNEL_ERROR_EOPNOTSUPP},
  {ERROR_CALL_NOT_IMPLEMENTED, SCE_KERNEL_ERROR_EOPNOTSUPP}, {31337, SCE_KERNEL_ERROR_EIO},
  {ERROR_SUCCESS, SCE_KERNEL_ERROR_EIO},
 };
 for (const auto& error : errors) {
  beginCase("native-failure-maps-error-and-keeps-guest-metadata");
  thread.nativeHandle = &thread; thread.priority = 700; configuredError = error.native; failApi = true;
  CHECK(scePthreadSetprio(&thread, 260) == error.guest);
  CHECK(thread.priority == 700 && winCalls == 1 && errorCalls == 1);
  CHECK(events.size() == 1 && events[0].oldGuest == 700 && events[0].relative == 2);
 }

 beginCase("two-setters-serialize-native-apply-and-guest-commit");
 thread.priority = 700; thread.nativeHandle = &thread; blockFirst = true;
 int firstStatus = -1, secondStatus = -1;
 std::thread first([&] { firstStatus = scePthreadSetprio(&thread, 256); });
 {
  std::unique_lock<std::mutex> lock(mockLock);
  CHECK(mockCondition.wait_for(lock, std::chrono::seconds(5), [] { return firstEntered; }));
  CHECK(thread.priority == 700 && events.size() == 1);
 }
 /* Deterministic witness that the actual setter holds its file-local mutex. */
 bool unexpectedlyUnlocked = hostPriorityLock.try_lock();
 if (unexpectedlyUnlocked) hostPriorityLock.unlock();
 CHECK(!unexpectedlyUnlocked);
 std::thread second([&] {
  { std::lock_guard<std::mutex> lock(mockLock); secondStarting = true; mockCondition.notify_all(); }
  secondStatus = scePthreadSetprio(&thread, 767);
 });
 {
  std::unique_lock<std::mutex> lock(mockLock);
  CHECK(mockCondition.wait_for(lock, std::chrono::seconds(5), [] { return secondStarting; }));
  CHECK(thread.priority == 700 && events.size() == 1);
  releaseFirst = true; mockCondition.notify_all();
 }
 first.join(); second.join();
 CHECK(firstStatus == SCE_OK && secondStatus == SCE_OK && thread.priority == 767);
 CHECK(events.size() == 2 && events[0].oldGuest == 700 && events[1].oldGuest == 256);
 CHECK(events[0].relative == 2 && events[1].relative == 0 && winCalls == 2 && activeCalls == 0);
}
#endif
int main(int argc, char** argv)
{
#ifdef TEST_NON_WINDOWS
 (void)argc; (void)argv;
 beginCase("non-windows-actual-setter-keeps-metadata-only-path");
 PthreadPrivate thread;
 thread._finished = true;
 CHECK(scePthreadSetprio(&thread, 256) == SCE_OK && thread.priority == 256);
 CHECK(scePthreadSetprio(nullptr, 256) == SCE_KERNEL_ERROR_EINVAL);
 CHECK(winCalls == 0 && errorCalls == 0);
 mapping();
#else
 if (argc == 3 && !std::strcmp(argv[1], "--gate")) gate(!std::strcmp(argv[2], "1"));
 else { CHECK(argc == 1); CHECK(HostThreadPriority::Enabled()); mapping(); enabledCases(); }
#endif
 std::cout << "{\"passed\":true,\"cases\":" << cases << ",\"assertions\":" << assertions.load()
           << ",\"realWinCalls\":false,\"realSchedulingCalls\":false}\n";
 return 0;
}
