#!/usr/bin/env python3
"""Exercise Madeira's real iOS process-name function with expired guest argv."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "upstreams/Madeira/build/ntdll-unix/env_ios.c").read_text()
start = source.index("static void set_process_name( const char *name )")
end = source.index("\n\n\n/***********************************************************************", start)
function = source[start:end]
harness = r'''
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define WINE_IOS 1
#define HAVE_SETPROGNAME 1
''' + function + r'''
static void *guest(void *unused) {
    char stack_path[] = "C:\\game\\stack-title.exe";
    char *heap_path = strdup("C:\\game\\heap-title.exe");
    assert(heap_path);
    set_process_name(stack_path);
    set_process_name(heap_path);
    free(heap_path);
    return NULL;
}
int main(void) {
    const char *host = getprogname();
    char *saved = strdup(host);
    assert(saved);
    for (int i = 0; i < 8; ++i) {
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, guest, NULL));
        assert(!pthread_join(thread, NULL));
        assert(getprogname() == host);
        char *copy = strdup(getprogname());
        assert(copy && !strcmp(copy, saved));
        free(copy);
    }
    free(saved);
    puts("Native process name survives eight expired stack/heap guest argument sets");
}
'''
env = dict(os.environ)
env.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
with tempfile.TemporaryDirectory(prefix="madeira-process-name-") as scratch:
    test = Path(scratch) / "test.c"
    test.write_text(harness)
    binary = Path(scratch) / "test"
    subprocess.run(["xcrun", "clang", "-O1", "-g", "-fsanitize=address,undefined",
                    "-Wall", "-Wextra", "-Wno-unused-parameter", str(test), "-o", str(binary)],
                   env=env, check=True)
    subprocess.run([str(binary)], check=True)
