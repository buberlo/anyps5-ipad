#!/usr/bin/env python3
"""Compile production diagnostic policy/handlers and check quiet/error behavior."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / 'upstreams/wine/include'


def function(text, name):
    start = text.index(name)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class DiagnosticsTests(unittest.TestCase):
    def build_run(self, source, env=None, cpp=False):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            code = directory / ('test.cpp' if cpp else 'test.c')
            code.write_text(source)
            compiler = shutil.which('clang++' if cpp else 'clang')
            self.assertIsNotNone(compiler, 'clang required for production policy test')
            subprocess.run([compiler, '-std=c++17' if cpp else '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                            '-Wall', '-Wextra', '-Werror', '-I', str(INCLUDE), str(code),
                            '-o', str(directory / 'test')], check=True, capture_output=True, text=True)
            clean = {k: v for k, v in os.environ.items() if not k.startswith('MADEIRA_')}
            clean.update(env or {})
            subprocess.run([str(directory / 'test')], env=clean, check=True, timeout=5)

    def test_only_explicit_opt_in_and_bounded_duration(self):
        self.build_run(r'''
#include <assert.h>
#include "wine/madeira_diagnostics.h"
int main(void) {
 assert(!madeira_diagnostics_value(NULL));
 const char *off[] = {"", "0", "true", "yes", "01", "1 ", "10"};
 for (size_t i=0; i<sizeof(off)/sizeof(off[0]); ++i) assert(!madeira_diagnostics_value(off[i]));
 assert(madeira_diagnostics_value("1"));
 assert(!madeira_diagnostics_enabled("MADEIRA_THREAD_SAMPLER"));
 setenv("MADEIRA_THREAD_SAMPLER", "0", 1);
 assert(!madeira_diagnostics_enabled("MADEIRA_THREAD_SAMPLER"));
 assert(madeira_diagnostics_seconds()==30);
 setenv("MADEIRA_DIAGNOSTICS_SECONDS", "bad", 1); assert(madeira_diagnostics_seconds()==30);
 setenv("MADEIRA_DIAGNOSTICS_SECONDS", "-1", 1); assert(madeira_diagnostics_seconds()==30);
 setenv("MADEIRA_DIAGNOSTICS_SECONDS", "0", 1); assert(madeira_diagnostics_seconds()==30);
 setenv("MADEIRA_DIAGNOSTICS_SECONDS", "999999999999999999999", 1); assert(madeira_diagnostics_seconds()==60);
 setenv("MADEIRA_DIAGNOSTICS_SECONDS", "1", 1); assert(madeira_diagnostics_seconds()==1);
 setenv("MADEIRA_THREAD_SAMPLER", "1", 1);
 assert(madeira_diagnostics_enabled("MADEIRA_THREAD_SAMPLER"));
 assert(!madeira_diagnostics_enabled("MADEIRA_WPROF"));
 setenv("MADEIRA_DIAGNOSTICS", "1", 1); assert(madeira_diagnostics_enabled("MADEIRA_WPROF"));
 struct timespec delay = {1, 100000000}; nanosleep(&delay, NULL);
 assert(!madeira_diagnostics_enabled("MADEIRA_THREAD_SAMPLER"));
 assert(!madeira_diagnostics_enabled("MADEIRA_WPROF"));
 return 0;
}
''')

    def test_errors_asserts_unknown_and_partial_records_are_never_filtered(self):
        self.build_run(r'''
#include <assert.h>
#include "wine/madeira_diagnostics.h"
int main(void) {
 const char *verbose[] = {"D 24 compile block\n", "I A8 profile info\n"};
 for (size_t i=0; i<sizeof(verbose)/sizeof(verbose[0]); ++i) {
   size_t n=strlen(verbose[i]); assert(madeira_fex_verbose_record(verbose[i],n));
   for (size_t cut=0; cut<n; ++cut) assert(!madeira_fex_verbose_record(verbose[i],cut));
 }
 const char *keep[] = {"E 24 failed allocation\n", "A assertion\n", "? 24 unknown\n",
 "D not-a-tid text\n", "D 24 info\nE 24 failure\n", "wine:err: fatal\n",
 "E 84 [iOS-xrem] via=aligned trace marked ERROR\n", "D 24 partial", "I  info\n", ""};
 for (size_t i=0; i<sizeof(keep)/sizeof(keep[0]); ++i)
   assert(!madeira_fex_verbose_record(keep[i],strlen(keep[i])));
 return 0;
}
''')

    def test_real_host_fex_handlers_preserve_errors_and_throws(self):
        text = (ROOT / 'upstreams/Madeira/app/Madeira/FEXBridge.mm').read_text()
        handlers = function(text, 'static void FEXLogHandler(') + '\n' + function(text, 'static void FEXThrowHandler(')
        self.build_run(r'''
#include <cassert>
#include "wine/madeira_diagnostics.h"
namespace LogMan {
 enum DebugLevels {NONE=0, ASSERT=1, ERROR=2, DEBUG=3, INFO=4};
 static const char *DebugLevelStr(DebugLevels) { return "level"; }
}
static int emitted;
static void fex_log(const char *, ...) { ++emitted; }
''' + handlers + r'''
int main() {
 FEXLogHandler(LogMan::DEBUG,"debug"); FEXLogHandler(LogMan::INFO,"info"); assert(emitted==0);
 FEXLogHandler(LogMan::ERROR,"failure"); FEXLogHandler(LogMan::ASSERT,"assert");
 FEXThrowHandler("throw"); assert(emitted==3);
 FEXLogHandler(static_cast<LogMan::DebugLevels>(99),"unknown"); assert(emitted==4);
 setenv("MADEIRA_FEX_LOG","1",1);
 FEXLogHandler(LogMan::DEBUG,"debug"); FEXLogHandler(LogMan::INFO,"info"); assert(emitted==6);
 return 0;
}
''', cpp=True)

    def test_exception_trace_caps_do_not_gate_context_reset(self):
        text = (ROOT / 'upstreams/wine/dlls/ntdll/signal_arm64ec.c').read_text()
        gate = function(text, 'static BOOL madeira_exception_diagnostics_enabled(')
        body = function(text, 'static void * __attribute__((used)) prepare_exception_arm64ec(')
        body = body[:body.index('    /* call x64 dispatcher')]
        body = body[body.index('{'):] + '}\n'
        # Replace only the optional platform memory census; exercise the real
        # enclosing opt-in/count gates and the actual reset call placement.
        census = function(body, 'if (rec->ExceptionCode == STATUS_ACCESS_VIOLATION)')
        body = body.replace(census, 'if (rec->ExceptionCode == STATUS_ACCESS_VIOLATION) ++census_calls;')
        self.build_run(r'''
#include <assert.h>
#include <string.h>
typedef int LONG, BOOL;
typedef unsigned ULONG;
typedef struct { unsigned ExceptionCode; } EXCEPTION_RECORD;
typedef struct { int AMD64_Context; } ARM64EC_NT_CONTEXT;
typedef int ARM64_NT_CONTEXT;
enum { STATUS_ACCESS_VIOLATION=1, STATUS_EMULATION_SYSCALL=2 };
static int madeira_exception_diagnostics, census_calls, resets, conversions, pre, post;
static ULONG madeira_exception_start_ms=1000, madeira_exception_duration_ms=30000;
static struct { struct { ULONG LowPart; } TickCount; } shared={{1000}}, *user_shared_data=&shared;
''' + gate + r'''

#define InterlockedIncrement(p) (++*(p))
#define ERR(format, ...) do { if (strstr(format,"[rtcs] pre")) ++pre; else ++post; } while (0)
static void dispatch_syscall(ARM64_NT_CONTEXT *a) { (void)a; }
static void context_arm_to_x64(ARM64EC_NT_CONTEXT *c, ARM64_NT_CONTEXT *a) { (void)c; (void)a; ++conversions; }
static void reset(EXCEPTION_RECORD *r, int *c, ARM64_NT_CONTEXT *a) { (void)r; (void)c; (void)a; ++resets; }
static void (*pResetToConsistentState)(EXCEPTION_RECORD *, int *, ARM64_NT_CONTEXT *) = reset;
static void probe(EXCEPTION_RECORD *rec, ARM64EC_NT_CONTEXT *context, ARM64_NT_CONTEXT *arm_ctx)
''' + body + r'''
int main(void) {
 EXCEPTION_RECORD rec={STATUS_ACCESS_VIOLATION}; ARM64EC_NT_CONTEXT context={0}; ARM64_NT_CONTEXT arm=0;
 for (int i=0; i<120; ++i) probe(&rec,&context,&arm);
 assert(!pre && !post && !census_calls && resets==120 && conversions==120);
 madeira_exception_diagnostics=1;
 for (int i=0; i<100; ++i) probe(&rec,&context,&arm);
 assert(pre==40 && post==40 && census_calls==40 && resets==220 && conversions==220);
 madeira_exception_diagnostics=0; probe(&rec,&context,&arm);
 assert(pre==40 && post==40 && resets==221 && conversions==221);
 madeira_exception_diagnostics=1; shared.TickCount.LowPart=31000;
 assert(!madeira_exception_diagnostics_enabled()); probe(&rec,&context,&arm);
 assert(resets==222 && conversions==222);
 madeira_exception_start_ms=0xfffffff0u; shared.TickCount.LowPart=20;
 assert(madeira_exception_diagnostics_enabled());
 return 0;
}
''')

    def test_real_sampler_dispatch_is_disabled_until_opt_in(self):
        text = (ROOT / 'upstreams/Madeira/build/ntdll-unix/server_ios.c').read_text()
        start = text.index('        if ((madeira_diagnostics_enabled("MADEIRA_THREAD_SAMPLER")')
        end = text.index('        if (madeira_diagnostics_enabled("MADEIRA_LEGACY_PROFILER"))', start)
        dispatch = text[start:end]
        # Preserve the production conditions/CAS and replace only platform scheduling.
        dispatch = re.sub(r'dispatch_async\(dispatch_get_global_queue\(QOS_CLASS_UTILITY, 0\), \^\{ (\w+)\(\); \}\);', r'\1();', dispatch)
        self.build_run(r'''
#include <assert.h>
#include "wine/madeira_diagnostics.h"
static int ios_ts_armed, threads, xp, wp;
static void ios_thread_sampler_main(void) { ++threads; }
static void ios_xprobe_main(void) { ++xp; }
static void ios_wprof_main(void) { ++wp; }
static void start(void) {
''' + dispatch + r'''
}
int main(void) {
 start(); assert(threads==0 && xp==0 && wp==0 && ios_ts_armed==0);
 setenv("MADEIRA_XPROBE","1",1); start(); start(); assert(threads==0 && xp==1 && wp==0);
 ios_ts_armed=0; unsetenv("MADEIRA_XPROBE"); setenv("MADEIRA_DIAGNOSTICS","1",1);
 start(); assert(threads==1 && xp==2 && wp==1);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
