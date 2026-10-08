// Verify our Madeira bridge against the actual pinned FEX headers.
// This file is project-owned; it does not modify or compile FEX source files.
#include <FEXCore/Core/CoreState.h>
#include "aps5_async_flags.h"
#include <cstdio>

using State = FEXCore::Core::CPUState;
using Prefix = aps5_fex_flags_prefix;
#define CHECK_FIELD(field) static_assert(offsetof(State, field) == offsetof(Prefix, field))
CHECK_FIELD(pf_raw);
CHECK_FIELD(af_raw);
CHECK_FIELD(rip);
CHECK_FIELD(gregs);
CHECK_FIELD(flags);
#undef CHECK_FIELD
static_assert(sizeof(State::flags) == sizeof(Prefix::flags));
static_assert(sizeof(State::gregs) == sizeof(Prefix::gregs));
static_assert(sizeof(State) >= sizeof(Prefix));
static_assert(offsetof(FEXCore::Core::CpuStateFrame, State) == 0);
static_assert(FEXCore::X86State::REG_RSP == 4);
static_assert(FEXCore::X86State::RFLAG_DF_RAW_LOC == 10);
static_assert(FEXCore::X86State::RFLAG_NZCV_LOC == 24);
static_assert(sizeof(aps5_async_flags_info) == 24);
int main() { std::puts("Pinned FEX async flag bridge layout verified."); }
