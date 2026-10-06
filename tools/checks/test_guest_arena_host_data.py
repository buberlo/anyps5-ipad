#!/usr/bin/env python3
"""Compile the real Madeira selector with controlled view/host-pool inputs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GuestArenaSelector(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'upstreams/Madeira/build/ntdll-unix/virtual_ios.c').read_text()
        start = source.index('static int ios_anyps5_guest_arena_limits(')
        end = source.index('static inline int mprotect_exec(', start)
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        cls.binary = directory / 'selector'
        harness = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
enum { SEC_IMAGE=1, VPROT_ARM64EC=2, VPROT_SYSTEM=4, VPROT_PLACEHOLDER=8 };
static uintptr_t host_page_mask=0x3fff;
static int arm64ec_view=1, ios_alloc_ec_code;
static void *ios_jit_rx_base_global=(void *)0x100000000ULL;
static void *ios_jit_rw_base_global=(void *)0x7000000000ULL;
static size_t ios_jit_pool_size_global=0x26000000;
struct file_view { void *base; size_t size; unsigned protect; };
static struct file_view views[2] = {
 {(void *)0x7400000000ULL,0x80000000,0},
 {(void *)0x7480000000ULL,0x80000000,0}
};
static const struct file_view *find_view(const void *pointer, size_t unused) {
 (void)unused;
 uintptr_t address=(uintptr_t)pointer;
 for(int i=0;i<2;i++)
  if(address >= (uintptr_t)views[i].base && address-(uintptr_t)views[i].base < views[i].size) return &views[i];
 return NULL;
}
'''
        harness += source[start:end]
        harness += r'''
int main(int argc,char **argv) {
 if(argc!=4)return 64;
 int mode=atoi(argv[1]);
 if(mode==1)ios_alloc_ec_code=1;
 if(mode==2)arm64ec_view=0;
 if(mode>=3 && mode<=6)views[1].protect=1u<<(mode-3);
 if(mode==7)views[1].base=(void *)0x7480004000ULL;
 if(mode==8)ios_jit_rx_base_global=(void *)0x7480000000ULL;
 if(mode==9)ios_jit_rw_base_global=(void *)0x7480000000ULL;
 uintptr_t address=strtoull(argv[2],NULL,0);
 size_t size=strtoull(argv[3],NULL,0);
 errno=EDOM;
 int result=ios_anyps5_guest_arena_is_host_data((void *)address,size);
 if(errno!=EDOM)return 65;
 return result ? 0 : 1;
}
'''
        (directory / 'selector.c').write_text(harness)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                        str(directory / 'selector.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def selector(self, expected, mode=0, address='0x7400000000', size='0x4000', **overrides):
        env = dict(os.environ, APS5_GUEST_ARENA_LAZY='1',
                   APS5_GUEST_ARENA_BASE='0x7400000000', APS5_GUEST_ARENA_SIZE='0x100000000')
        env.update(overrides)
        result = subprocess.run([str(self.binary), str(mode), address, size], env=env)
        self.assertEqual(result.returncode, 0 if expected else 1)

    def test_valid_views_and_cross_view_range(self):
        self.selector(True)
        self.selector(True, address='0x747fffc000', size='0x8000')
        self.selector(True, size='0x100000000')

    def test_range_boundaries_and_overflow(self):
        for address, size in [('0x73ffffc000','0x4000'), ('0x7500000000','0x4000'),
                              ('0x74ffffc000','0x8000'), ('0x7400000000','0'),
                              ('0xffffffffffffc000','0x8000')]:
            with self.subTest(address=address, size=size):
                self.selector(False, address=address, size=size)

    def test_native_code_images_system_views_placeholders_and_holes(self):
        for mode in range(1, 10):
            with self.subTest(mode=mode):
                self.selector(False, mode=mode, address='0x747fffc000', size='0x8000')

    def test_explicit_valid_configuration_required(self):
        cases = [dict(APS5_GUEST_ARENA_LAZY='0'), dict(APS5_GUEST_ARENA_BASE=''),
                 dict(APS5_GUEST_ARENA_BASE='-1'), dict(APS5_GUEST_ARENA_BASE=' 0x7400000000'),
                 dict(APS5_GUEST_ARENA_BASE='0x7400000001'), dict(APS5_GUEST_ARENA_BASE='0x7400000000x'),
                 dict(APS5_GUEST_ARENA_SIZE='0'), dict(APS5_GUEST_ARENA_SIZE='0x4001'),
                 dict(APS5_GUEST_ARENA_SIZE='0xffffffffffffffff'),
                 dict(APS5_GUEST_ARENA_SIZE='18446744073709551616')]
        for case in cases:
            with self.subTest(case=case):
                self.selector(False, **case)


if __name__ == '__main__':
    unittest.main()
