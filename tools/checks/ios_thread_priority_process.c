#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#define WINE_IOS 1
#ifndef __APPLE__
#define __APPLE__ 1
#endif
#define min(a,b) ((a) < (b) ? (a) : (b))
#define STATUS_SUCCESS UINT32_C(0)
#define STATUS_NOT_SUPPORTED UINT32_C(0xc00000bb)
#define STATUS_INVALID_PARAMETER UINT32_C(0xc000000d)
#define STATUS_ACCESS_DENIED UINT32_C(0xc0000022)
enum {
 LOW_PRIORITY = 0, HIGH_PRIORITY = 31, LOW_REALTIME_PRIORITY = 16,
 PROCESS_PRIOCLASS_IDLE = 1, PROCESS_PRIOCLASS_NORMAL = 2,
 PROCESS_PRIOCLASS_HIGH = 3, PROCESS_PRIOCLASS_REALTIME = 4,
 PROCESS_PRIOCLASS_BELOW_NORMAL = 5, PROCESS_PRIOCLASS_ABOVE_NORMAL = 6,
 RUNNING = 0, TERMINATED = 1,
};
struct thread;
struct list { struct thread *head; };
struct process { int priority, base_priority, disable_boost, running_threads; struct list thread_list; struct thread *first; };
struct thread {
 struct process *process; struct thread *next;
 int base_priority, disable_boost, state, unix_tid;
 unsigned int id, ios_priority_port;
 unsigned long long ios_priority_identity;
};
#define LIST_FOR_EACH_ENTRY(cursor,list,type,field) for ((cursor) = (list)->head; (cursor); (cursor) = (cursor)->next)
static unsigned int assertions, cases, error_status, base_calls, boost_calls;
static int process_gate = 1;
static const char *case_name;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
 fprintf(stderr, "FAIL %s:%d: %s [%s]\n", __FILE__, __LINE__, #condition, case_name); exit(1); } } while (0)
static int thread_uses_ios_precedence(void) { return process_gate; }
static void set_error(unsigned int status) { error_status = status; }
static unsigned int set_thread_base_priority(struct thread *thread, int priority)
{ ++base_calls; thread->base_priority = priority; return STATUS_SUCCESS; }
static void set_thread_disable_boost(struct thread *thread, int boost)
{ ++boost_calls; thread->disable_boost = boost; }
static struct thread *get_process_first_thread(struct process *process) { return process->first; }
#include "generated_process_functions.h"

static unsigned int log_count;
static char logs[32][512];
static int capture_fprintf(FILE *destination, const char *format, ...)
{
 CHECK(destination == stderr && log_count < 32);
 va_list args;
 va_start(args, format);
 int length = vsnprintf(logs[log_count++], sizeof(logs[0]), format, args);
 va_end(args);
 CHECK(length > 0 && (size_t)length < sizeof(logs[0]));
 return length;
}
/* These are verbatim actual enabled/importance/trace functions. */
#define fprintf capture_fprintf
#include "generated_trace_functions.h"
#undef fprintf

static struct process process;
static struct thread threads[2];
static void begin_case(const char *name)
{
 ++cases; case_name = name;
 error_status = base_calls = boost_calls = 0; process_gate = 1;
 memset(&process, 0, sizeof(process)); memset(threads, 0, sizeof(threads));
 process.priority = PROCESS_PRIOCLASS_NORMAL; process.base_priority = 8;
 process.disable_boost = 1; process.running_threads = 2;
 process.thread_list.head = &threads[0];
 threads[0].next = &threads[1];
 threads[0].base_priority = 1; threads[1].base_priority = -2;
 threads[0].disable_boost = 0; threads[1].disable_boost = 1;
 for (unsigned int i = 0; i < 2; ++i) {
  threads[i].process = &process; threads[i].state = RUNNING; threads[i].unix_tid = 99;
  threads[i].id = 0x77U + i; threads[i].ios_priority_port = 77; threads[i].ios_priority_identity = 123;
 }
}
static void untouched(void)
{
 CHECK(process.priority == PROCESS_PRIOCLASS_NORMAL && process.base_priority == 8 && process.disable_boost == 1);
 CHECK(threads[0].base_priority == 1 && threads[1].base_priority == -2);
 CHECK(threads[0].disable_boost == 0 && threads[1].disable_boost == 1);
 CHECK(base_calls == 0 && boost_calls == 0);
}
static void process_cases(void)
{
 begin_case("live-process-class-change-and-inconsistent-noop-reject");
 set_process_priority(&process, PROCESS_PRIOCLASS_HIGH);
 CHECK(error_status == STATUS_NOT_SUPPORTED); untouched();
 process.base_priority = 9; error_status = 0;
 set_process_priority(&process, PROCESS_PRIOCLASS_NORMAL);
 CHECK(error_status == STATUS_NOT_SUPPORTED && process.priority == PROCESS_PRIOCLASS_NORMAL && process.base_priority == 9);
 CHECK(base_calls == 0 && boost_calls == 0);

 begin_case("live-process-base-change-reject-before-mutation-fanout");
 set_process_base_priority(&process, 10);
 CHECK(error_status == STATUS_NOT_SUPPORTED); untouched();

 begin_case("live-process-boost-change-reject-before-mutation-fanout");
 set_process_disable_boost(&process, 0);
 CHECK(error_status == STATUS_NOT_SUPPORTED); untouched();

 begin_case("true-process-noops-preserve-individual-thread-settings");
 set_process_priority(&process, PROCESS_PRIOCLASS_NORMAL);
 set_process_base_priority(&process, 8);
 set_process_disable_boost(&process, 1);
 CHECK(error_status == STATUS_SUCCESS); untouched();

 begin_case("no-running-process-initialization-works");
 process.running_threads = 0; process.thread_list.head = NULL;
 set_process_priority(&process, PROCESS_PRIOCLASS_BELOW_NORMAL);
 CHECK(error_status == STATUS_SUCCESS && process.priority == PROCESS_PRIOCLASS_BELOW_NORMAL && process.base_priority == 6);
 set_process_base_priority(&process, 12);
 set_process_disable_boost(&process, 0);
 CHECK(error_status == STATUS_SUCCESS && process.base_priority == 12 && process.disable_boost == 0);
 CHECK(base_calls == 0 && boost_calls == 0);

 begin_case("gate-off-retains-live-process-fanout-including-noops");
 process_gate = 0;
 set_process_priority(&process, PROCESS_PRIOCLASS_HIGH);
 CHECK(error_status == STATUS_SUCCESS && process.priority == PROCESS_PRIOCLASS_HIGH && process.base_priority == 13 && base_calls == 2);
 set_process_base_priority(&process, 13);
 CHECK(error_status == STATUS_SUCCESS && base_calls == 4);
 set_process_disable_boost(&process, 1);
 CHECK(error_status == STATUS_SUCCESS && boost_calls == 2 && threads[0].disable_boost == 1 && threads[1].disable_boost == 1);

 begin_case("invalid-process-base-never-mutates-or-fans-out");
 set_process_base_priority(&process, 0);
 CHECK(error_status == STATUS_INVALID_PARAMETER); untouched();
 set_process_base_priority(&process, 32);
 CHECK(error_status == STATUS_INVALID_PARAMETER); untouched();
}

static void trace_call(struct thread *thread, int requested, long long nt_priority, unsigned int status)
{ ios_trace_explicit_priority(thread, "priority", requested, nt_priority, status); }
static void trace_cases(int expected)
{
 begin_case("actual-trace-gates-cache-readback-predicates-extremes-cap32");
 struct thread *thread = &threads[0];
 trace_call(thread, 8, 8, STATUS_SUCCESS);
 CHECK(log_count == (expected ? 1U : 0U));
 /* Changing either environment value does not change already cached decisions. */
 CHECK(setenv("MADEIRA_IOS_THREAD_PRECEDENCE_TRACE", expected ? "0" : "1", 1) == 0);
 CHECK(setenv("MADEIRA_IOS_THREAD_PRECEDENCE", "1", 1) == 0);
 thread->unix_tid = -1; trace_call(thread, 8, 8, 0); thread->unix_tid = 99;
 thread->state = TERMINATED; trace_call(thread, 8, 8, 0); thread->state = RUNNING;
 thread->ios_priority_port = 0; trace_call(thread, 8, 8, 0); thread->ios_priority_port = 77;
 thread->ios_priority_identity = 0; trace_call(thread, 8, 8, 0); thread->ios_priority_identity = 123;
 trace_call(thread, 8, 8, STATUS_ACCESS_DENIED);
 trace_call(thread, INT_MIN, LLONG_MIN, STATUS_INVALID_PARAMETER);
 trace_call(thread, INT_MAX, LLONG_MAX, STATUS_INVALID_PARAMETER);
 process.first = thread; trace_call(thread, 8, 8, 0); process.first = NULL;
 if (expected) {
  CHECK(log_count == 9);
  CHECK(strstr(logs[0], "effective=8 importance=0 importance-valid=1 status=00000000 precedence-readback-match=1 queued-before-bootstrap=0") != NULL);
  CHECK(strstr(logs[1], "precedence-readback-match=0 queued-before-bootstrap=1") != NULL);
  for (unsigned int i = 2; i < 6; ++i) CHECK(strstr(logs[i], "precedence-readback-match=0 queued-before-bootstrap=0") != NULL);
  CHECK(strstr(logs[6], "effective=-9223372036854775808 importance=0 importance-valid=0") != NULL);
  CHECK(strstr(logs[7], "effective=9223372036854775807 importance=0 importance-valid=0") != NULL);
  CHECK(strstr(logs[8], "effective=9 importance=5 importance-valid=1") != NULL);
 }
 for (unsigned int i = 0; i < 96; ++i) trace_call(thread, 8, 8, STATUS_SUCCESS);
 CHECK(log_count == (expected ? 32U : 0U));
}

int main(int argc, char **argv)
{
 if (argc == 3 && !strcmp(argv[1], "--trace")) trace_cases(!strcmp(argv[2], "1"));
 else { CHECK(argc == 1); process_cases(); }
 printf("{\"passed\":true,\"cases\":%u,\"assertions\":%u,\"traceLines\":%u,\"actualExtractedFunctions\":true,\"realMachCalls\":false}\n", cases, assertions, log_count);
 return 0;
}
