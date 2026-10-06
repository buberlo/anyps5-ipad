#!/usr/bin/env python3
"""Exercise the actual Mach delivery counter: tracked work versus a stuck thread."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=r'''
#include "mach_fault_redelivery.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 struct ios_redelivery_tracker t={0};
 /* The device pattern revisits a store after different faults on the same
    thread. More than 2000 total visits are legitimate, not one refault loop. */
 for (unsigned i=0;i<10000;i++) {
  assert(ios_redelivery_record(&t,1,0x1000,0x2000,0)==1);
  assert(ios_redelivery_record(&t,1,0x3000,0x4000,0)==1);
 }
 /* Traffic on another thread must not conceal a truly repeating fault. */
 for (unsigned i=1;i<=2000;i++) {
  assert(ios_redelivery_record(&t,2,0x1000,0x2000,0)==i);
  ios_redelivery_record(&t,3,0x5000+i,0x6000,0);
 }
 /* Changing either member interrupts the sequence; alignment delivery
    interrupts it too. Full thread identity is retained without XOR folding. */
 assert(ios_redelivery_record(&t,2,0x1001,0x2000,0)==1);
 assert(ios_redelivery_record(&t,2,0x1001,0x2001,0)==1);
 assert(ios_redelivery_record(&t,2,0x1001,0x2001,0)==2);
 assert(ios_redelivery_record(&t,2,0x1001,0x2001,1)==0);
 assert(ios_redelivery_record(&t,2,0x1001,0x2001,0)==1);
 assert(ios_redelivery_record(&t,0x1000000000000002ULL,0x1001,0x2001,0)==1);
 assert(ios_redelivery_record(&t,2,0x1001,0x2001,0)==2);
 /* Bounded table replacement cannot inherit a previous thread's count. */
 for (unsigned i=100;i<300;i++)
  assert(ios_redelivery_record(&t,i,0x1001,0x2001,0)==1);
 /* A protection handler repairs and re-arms the same page every iteration.
    This exact pattern reached the terminal on the real iPad despite 2000
    independently checked completed AVX stores. */
 for (unsigned i=0;i<5000;i++) {
  assert(ios_redelivery_record(&t,400,0x1000,0x21620,0)==1);
  assert(ios_redelivery_repaired(&t,401,0x20000,0x10000)==0);
  assert(ios_redelivery_repaired(&t,400,0x30000,0x10000)==0);
  assert(ios_redelivery_repaired(&t,400,0x20000,0)==0);
  assert(ios_redelivery_repaired(&t,400,0x20000,0x1620)==0);
  assert(ios_redelivery_repaired(&t,400,0x20000,0x10000)==1);
 }
 /* Bad repair ranges must not reset an unrepaired fault. Subtraction
    avoids overflow in a base+size containment check. */
 for (unsigned i=1;i<=2000;i++) {
  assert(ios_redelivery_record(&t,400,0x1000,0x21620,0)==i);
  assert(ios_redelivery_repaired(&t,400,UINT64_MAX-4,16)==0);
 }
 puts("PASS actual Mach redelivery counter: repaired writes, stuck loop, thread isolation, identity, exemptions, capacity and range bounds");
}
'''
with tempfile.TemporaryDirectory(prefix='anyps5-redelivery-') as tmp:
 p=Path(tmp);(p/'check.c').write_text(source)
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'upstreams/Madeira/build/ntdll-unix'),str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
