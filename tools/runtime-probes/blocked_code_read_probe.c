/* Synthetic regression: a blocking read into executable guest memory must not
 * hold FEX's thread/JIT locks, and returned bytes must invalidate cached code.
 * No game data. Compile as a Windows x86-64 console executable. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE ReaderReady, PipeRead, PipeWrite;
static unsigned char *Code;
static volatile LONG ReaderOK, WorkerOK, WriterOK;
static int Plain;
static const unsigned char Replacement[] = {0xb8, 43, 0, 0, 0, 0xc3};

static DWORD WINAPI reader(void *unused) {
    DWORD got = 0;
    SetEvent(ReaderReady);
    ReaderOK = ReadFile(PipeRead, Code, sizeof(Replacement), &got, NULL) && got == sizeof(Replacement);
    return 0;
}
static DWORD WINAPI worker(void *unused) {
    InterlockedExchange(&WorkerOK, Plain || ((int (*)(void))Code)() == 42);
    return 0;
}
static DWORD WINAPI writer(void *unused) {
    DWORD wrote = 0;
    /* Release after the measured worker deadline, even if thread creation blocks. */
    Sleep(2000);
    WriterOK = WriteFile(PipeWrite, Replacement, sizeof(Replacement), &wrote, NULL) && wrote == sizeof(Replacement);
    return 0;
}
int main(int argc, char **argv) {
    Plain = argc == 2 && strcmp(argv[1], "--plain") == 0;
    HANDLE read_thread, write_thread, work_thread;
    DWORD worker_wait;
    ULONGLONG start, elapsed;
    int result, before;
    setbuf(stdout, NULL);
    printf("[blocked-code-read] begin plain=%d\n", Plain);
    Code = VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, Plain ? PAGE_READWRITE : PAGE_EXECUTE_READWRITE);
    ReaderReady = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!Code || !ReaderReady || !CreatePipe(&PipeRead, &PipeWrite, NULL, 0)) return 2;
    memcpy(Code, Replacement, sizeof(Replacement)); Code[1] = 42;
    before = Plain ? 42 : ((int (*)(void))Code)();
    if (before != 42) return 3;
    write_thread = CreateThread(NULL, 0, writer, NULL, 0, NULL);
    read_thread = CreateThread(NULL, 0, reader, NULL, 0, NULL);
    if (!write_thread || !read_thread || WaitForSingleObject(ReaderReady, 5000) != WAIT_OBJECT_0) return 4;
    Sleep(200);
    start = GetTickCount64();
    printf("[blocked-code-read] creating worker while read pending\n");
    work_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    worker_wait = work_thread ? WaitForSingleObject(work_thread, 750) : WAIT_FAILED;
    elapsed = GetTickCount64() - start;
    printf("[blocked-code-read] worker_wait=%lu worker=%ld elapsed_ms=%llu reader=%ld\n",
           worker_wait, WorkerOK, (unsigned long long)elapsed, ReaderOK);
    if (WaitForSingleObject(read_thread, 5000) != WAIT_OBJECT_0 || WaitForSingleObject(write_thread, 5000) != WAIT_OBJECT_0) return 5;
    result = Plain ? (memcmp(Code, Replacement, sizeof(Replacement)) == 0 ? 43 : -1) : ((int (*)(void))Code)();
    printf("[blocked-code-read] before=%d after=%d reader=%ld writer=%ld worker=%ld elapsed_ms=%llu\n",
           before, result, ReaderOK, WriterOK, WorkerOK, (unsigned long long)elapsed);
    CloseHandle(read_thread); CloseHandle(write_thread);
    if (work_thread) CloseHandle(work_thread);
    CloseHandle(PipeRead); CloseHandle(PipeWrite); CloseHandle(ReaderReady);
    VirtualFree(Code, 0, MEM_RELEASE);
    if (!ReaderOK || !WriterOK || !WorkerOK || worker_wait != WAIT_OBJECT_0 || elapsed >= 1000 || result != 43) return 1;
    printf("[blocked-code-read] passed\n");
    return 0;
}
