// SPDX-License-Identifier: GPL-2.0-or-later
// guest_cpu.h through FEXCore on iOS. Ported from wyatt-poole/gow3_android cpu/guest_cpu_fex.cpp @ 5c2219d;
// the logic is the same, the host specifics are Darwin's:
//  - signal state is uc_mcontext->__ss (x0-x28, fp, lr, sp, pc; x0-x30 contiguous) and __ns (v0-v31);
//  - protection faults arrive as SIGBUS, unmapped addresses as SIGSEGV, so both are accepted;
//  - executable code buffers come from the dual-mapped JIT pool (native/jit), written through
//    FEXCore::DualMap::WriteOffset; guest x86 memory is never mapped executable;
//  - host pages are 16 KiB (FEX_PAGE_SIZE stays 4 KiB), so guards here are 16 KiB.
// Host functions are reached from the guest through stubs that end in `syscall` (dispatched by
// HandleSyscall below); guest functions are called with Context::HandleCallback.
#include "guest_cpu.h"
#include "host_features_darwin.h"
#include "../jit/jit_pool.h"

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/CodeCache.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/ArchHelpers/Arm64.h>
#include <FEXCore/Utils/DualMap.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/LongJump.h>
#include <FEXCore/Utils/SignalScopeGuards.h>
#include <FEXCore/Utils/TypeDefines.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <mach/mach.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

// cpu/host_call_arm64.S: calls `fn` with AAPCS64 arguments from `args`, stores x0, x1, v0, v1.
struct HostArgs {
    uint64_t x[8];
    uint64_t stack_count;
    const uint64_t* stack;
    __uint128_t v[8];
};
struct HostResult {
    uint64_t x[2];
    __uint128_t v[2];
};
extern "C" void gow3cpu_host_call(void* fn, const HostArgs* args, HostResult* result);
static_assert(offsetof(HostArgs, stack_count) == 64 && offsetof(HostArgs, stack) == 72 && offsetof(HostArgs, v) == 80);
static_assert(offsetof(HostResult, v) == 16);

namespace {
using FEXCore::Core::CPUState;
using FEXCore::Core::InternalThreadState;

constexpr size_t HostPage = 16384;

[[noreturn]] void Fatal(const char* message, uint64_t a = 0, uint64_t b = 0) {
    std::fprintf(stderr, "STOP: guest CPU: %s (%#llx, %#llx)\n", message, (unsigned long long)a, (unsigned long long)b);
    std::fflush(nullptr);
    _exit(21);
}

// Progress of guest_cpu_init on stderr, so a crash there shows how far it got.
void Step(const char* what) {
    std::fprintf(stderr, "%s\n", what);
}

FEXCore::Context::Context* ctx;
void* (*low_map)(size_t, int);
bool wide_xmm;
unsigned tsc_shift;
FEXCore::ArchHelpers::Arm64::UnalignedHandlerType unaligned_type;

// Stubs: 16 bytes each in one region; the first holds CALLBACKRET (0f 3e), the end of guest code
// called by HandleCallback. A stub is `mov r10, rcx; syscall; ret`: syscall replaces RCX (the
// fourth argument) with the return RIP before HandleSyscall sees the registers.
constexpr size_t StubSize = 16;
constexpr size_t StubBytes = 1 << 20;
constexpr size_t SyscallOffset = 3;
unsigned char* stubs;
uintptr_t callback_ret;
void* stub_functions[StubBytes / StubSize];
std::atomic<size_t> stub_count;
std::mutex stub_lock;
std::unordered_map<void*, uintptr_t> stub_of;

struct Hook {
    uintptr_t address;
    GuestHook hook;
};
std::array<Hook, 64> hooks;
std::atomic<size_t> hook_count;

std::shared_mutex code_lock;
std::vector<std::pair<uintptr_t, size_t>> code_ranges;

constexpr size_t CallRetSize = (InternalThreadState::CALLRET_STACK_SIZE + HostPage - 1) & ~(HostPage - 1);
constexpr size_t CallRetAlloc = CallRetSize + 2 * HostPage;
constexpr size_t DefaultStack = 1024 * 1024;
// Above the stack top for calls not nested in guest code: host calls read a few stack argument
// slots beyond the caller's frame.
constexpr size_t StackHeadroom = 256;
// Stack argument slots copied for host functions: x6, x7 and then AAPCS64 stack slots.
constexpr unsigned HostStackSlots = 10;

struct GuestThread {
    InternalThreadState* thread;
    uintptr_t callret_alloc;
    uintptr_t stack_top;
    unsigned depth;
};
thread_local GuestThread* self;
std::mutex threads_lock;
std::vector<GuestThread*> threads;

uint64_t* Xmm(CPUState& s, unsigned i) {
    return wide_xmm ? s.xmm.avx.data[i] : s.xmm.sse.data[i];
}
uintptr_t DefaultCallRet(const GuestThread* t) {
    return reinterpret_cast<uintptr_t>(t->thread->CallRetStackBase) + InternalThreadState::CALLRET_STACK_SIZE / 4;
}

// x0-x28, fp (x29) and lr (x30) are consecutive in __darwin_arm_thread_state64.
uint64_t* Gprs(ucontext_t* uc) {
    return reinterpret_cast<uint64_t*>(uc->uc_mcontext->__ss.__x);
}
const uint64_t* Gprs(const ucontext_t* uc) {
    return reinterpret_cast<const uint64_t*>(uc->uc_mcontext->__ss.__x);
}

GuestThread* Current() {
    if (self) {
        return self;
    }
    FEXCore::Allocator::InitializeThread();
    auto* t = new GuestThread {};
    t->thread = ctx->CreateThread(0, 0); // RIP and RSP are set by each guest_cpu_call
    auto* frame = t->thread->CurrentFrame;
    void* alloc = mmap(nullptr, CallRetAlloc, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (alloc == MAP_FAILED) {
        Fatal("cannot allocate the call-return stack");
    }
    t->callret_alloc = reinterpret_cast<uintptr_t>(alloc);
    t->thread->CallRetStackBase = static_cast<char*>(alloc) + HostPage;
    mprotect(t->thread->CallRetStackBase, CallRetSize, PROT_READ | PROT_WRITE);
    auto& s = frame->State;
    s.callret_sp = DefaultCallRet(t);
    // A flat 64-bit code segment, as the Linux kernel sets up.
    s.segment_arrays[CPUState::SEGMENT_ARRAY_INDEX_GDT] = s.private_gdt;
    s.segment_arrays[CPUState::SEGMENT_ARRAY_INDEX_LDT] = s.private_gdt;
    s.cs_idx = CPUState::DEFAULT_USER_CS << 3;
    auto* code = CPUState::GetSegmentFromIndex(s, s.cs_idx);
    CPUState::SetGDTBase(code, 0);
    CPUState::SetGDTLimit(code, 0xF'FFFFU);
    code->L = 1;
    s.cs_cached = 0;
    frame->Pointers.ThunkCallbackRet = callback_ret;
    {
        std::lock_guard lock(threads_lock);
        threads.push_back(t);
    }
    self = t;
    return t;
}

void InvalidateCode(uint64_t start, uint64_t length) {
    std::lock_guard lock(threads_lock);
    auto guard = FEXCore::GuardSignalDeferringSectionWithFallback(ctx->GetCodeInvalidationMutex(), self ? self->thread : nullptr);
    ctx->InvalidateCodeBuffersCodeRange(start, length);
    for (auto* t : threads) {
        ctx->InvalidateThreadCachedCodeRange(t->thread, start, length);
    }
}

// A guest call of a host function: SysV registers in, AAPCS64 call, SysV registers out.
// Darwin note: stack arguments are passed at natural alignment on Apple arm64, not in 8-byte
// slots, so host functions with more than 8 integer arguments must take 64-bit ones there.
void CallHost(CPUState& s, void* fn) {
    using namespace FEXCore::X86State;
    HostArgs args;
    args.x[0] = s.gregs[REG_RDI];
    args.x[1] = s.gregs[REG_RSI];
    args.x[2] = s.gregs[REG_RDX];
    args.x[3] = s.gregs[REG_R10];
    args.x[4] = s.gregs[REG_R8];
    args.x[5] = s.gregs[REG_R9];
    const auto* stack = reinterpret_cast<const uint64_t*>(s.gregs[REG_RSP] + 8);
    args.x[6] = stack[0];
    args.x[7] = stack[1];
    args.stack_count = HostStackSlots - 2;
    args.stack = stack + 2;
    for (unsigned i = 0; i < 8; ++i) {
        std::memcpy(&args.v[i], Xmm(s, i), sizeof(args.v[i]));
    }
    HostResult result;
    gow3cpu_host_call(fn, &args, &result);
    s.gregs[REG_RAX] = result.x[0];
    s.gregs[REG_RDX] = result.x[1];
    std::memcpy(Xmm(s, 0), &result.v[0], sizeof(result.v[0]));
    std::memcpy(Xmm(s, 1), &result.v[1], sizeof(result.v[1]));
}

// OS_GENERIC: the JIT spills every guest register to the frame before calling HandleSyscall and
// fills them afterwards, leaving RAX alone, so the handler edits the state directly. On entry
// s.rip is the address of the `syscall` instruction. Outside _WIN32 this FEX does not end the
// block at `syscall` (X86Tables.h DEFAULT_SYSCALL_FLAGS), so execution continues after it whatever
// s.rip says: right for host stubs (`ret` follows), but a guest_cpu_hook that moves RIP needs
// FLAGS_BLOCK_END there first (see docs/AUDIT.md).
class Handler final : public FEXCore::HLE::SyscallHandler {
public:
    Handler() {
        OSABI = FEXCore::HLE::SyscallOSABI::OS_GENERIC;
    }
    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame* frame, FEXCore::HLE::SyscallArguments*) override {
        auto& s = frame->State;
        const uint64_t rip = s.rip;
        const uint64_t offset = rip - reinterpret_cast<uintptr_t>(stubs);
        if (offset < StubBytes && offset % StubSize == SyscallOffset) {
            void* fn = stub_functions[offset / StubSize];
            if (!fn) {
                Fatal("call of an unregistered host stub", rip);
            }
            CallHost(s, fn);
            s.rip = rip + 2;
            return 0;
        }
        for (size_t i = 0, n = hook_count.load(std::memory_order_acquire); i < n; ++i) {
            if (hooks[i].address != rip) {
                continue;
            }
            GuestRegs regs;
            std::memcpy(regs.gpr, s.gregs, sizeof(regs.gpr));
            regs.rip = rip;
            hooks[i].hook(&regs);
            std::memcpy(s.gregs, regs.gpr, sizeof(regs.gpr));
            s.rip = regs.rip;
            return 0;
        }
        Fatal("the game executed a syscall instruction (rip, rax)", rip, s.gregs[FEXCore::X86State::REG_RAX]);
    }
    FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(InternalThreadState*, uint64_t address) override {
        std::shared_lock lock(code_lock);
        for (const auto& [base, size] : code_ranges) {
            if (address - base < size) {
                return {base, size, false};
            }
        }
        return {0, 0, false};
    }
    std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(InternalThreadState*, uint64_t) override {
        return std::nullopt;
    }
    void InvalidateGuestCodeRange(InternalThreadState*, uint64_t start, uint64_t length) override {
        InvalidateCode(start, length);
    }
};

class Delegator final : public FEXCore::SignalDelegator {
public:
    uintptr_t GetThunkCallbackRET() const override {
        return callback_ret;
    }
};

Handler handler;
Delegator delegator;

void LogMessage(LogMan::DebugLevels level, const char* message) {
    static const bool verbose = std::getenv("GOW3_FEX_LOG") != nullptr;
    if (level <= LogMan::ERROR || verbose) {
        std::fprintf(stderr, "FEX %s: %s\n", LogMan::DebugLevelStr(level), message);
    }
}
void LogAssert(const char* message) {
    Fatal(message);
}

uint64_t ReadGuest(uintptr_t address) {
    uint64_t value = 0;
    vm_size_t read = 0;
    const kern_return_t kr = vm_read_overwrite(mach_task_self(), address, sizeof(value), reinterpret_cast<vm_address_t>(&value), &read);
    return kr == KERN_SUCCESS && read == sizeof(value) ? value : 0;
}

bool IsFaultSignal(int sig) {
    return sig == SIGSEGV || sig == SIGBUS;
}
} // namespace

extern "C" {
void guest_cpu_init(void* (*map)(size_t size, int prot)) {
    low_map = map;
    if (!gow3_jit_pool_ready()) {
        Fatal("the JIT pool is not ready (enable JIT first)");
    }
    // Every executable buffer FEXCore asks for comes from the pool, and its emitters write at
    // RX + WriteOffset. Both must be set before the context and its dispatcher exist.
    FEXCore::DualMap::WriteOffset = gow3_jit_write_offset();
    FEXCore::Allocator::mmap = gow3_jit_mmap_hook;
    FEXCore::Allocator::munmap = gow3_jit_munmap_hook;
    LogMan::Throw::InstallHandler(LogAssert);
    LogMan::Msg::InstallHandler(LogMessage);
    Step("fex: allocator");
    FEXCore::Allocator::free(FEXCore::Allocator::malloc(64));
    // FEX's file and environment config layers live in its Common library, which FEX does not
    // build for Apple; the defaults plus the options set here are all the port uses.
    Step("fex: config");
    FEXCore::Config::Initialize();
    FEXCore::Config::Load();
    FEXCore::Config::ReloadMetaLayer();
    FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "1");
    Step("fex: host features");
    const auto features = gow3_fex_host_features();
    wide_xmm = features.SupportsAVX && features.SupportsSVE256;
    Step("fex: create context");
    ctx = FEXCore::Context::Context::CreateNewContext(features).release();
    ctx->SetSignalDelegator(&delegator);
    ctx->SetSyscallHandler(&handler);
    Step("fex: init core");
    if (!ctx->InitCore()) {
        Fatal("FEXCore initialization failed");
    }
    unaligned_type = FEXCore::Config::Get_HALFBARRIERTSOENABLED()() ? FEXCore::ArchHelpers::Arm64::UnalignedHandlerType::HalfBarrier :
                                                                       FEXCore::ArchHelpers::Arm64::UnalignedHandlerType::NonAtomic;
    uint64_t frequency;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (FEXCore::Config::Get_SMALLTSCSCALE()()) {
        // As FEXCore's ContextImpl: the counter scaled up to at least 1 GHz.
        while (frequency && (frequency << tsc_shift) < 1'000'000'000) {
            ++tsc_shift;
        }
    }
    Step("fex: host stubs");
    stubs = static_cast<unsigned char*>(map(StubBytes, PROT_READ | PROT_WRITE));
    if (!stubs) {
        Fatal("cannot allocate host function stubs");
    }
    std::memset(stubs, 0xcc, StubBytes);
    stubs[0] = 0x0f;
    stubs[1] = 0x3e;
    callback_ret = reinterpret_cast<uintptr_t>(stubs);
    stub_count = 1;
    guest_cpu_code(reinterpret_cast<uintptr_t>(stubs), StubBytes);
    std::fprintf(stderr, "Guest CPU: FEXCore JIT (x86-64 guest on iOS), TSC x%u\n", 1u << tsc_shift);
}

void guest_cpu_code(uintptr_t base, size_t size) {
    std::unique_lock lock(code_lock);
    code_ranges.emplace_back(base, size);
}

uintptr_t guest_cpu_host_function(void* fn) {
    std::lock_guard lock(stub_lock);
    if (auto found = stub_of.find(fn); found != stub_of.end()) {
        return found->second;
    }
    const size_t index = stub_count.load(std::memory_order_relaxed);
    if (index >= StubBytes / StubSize) {
        Fatal("too many host functions");
    }
    unsigned char* stub = stubs + index * StubSize;
    static constexpr unsigned char code[] = {0x49, 0x89, 0xca, 0x0f, 0x05, 0xc3}; // mov r10, rcx; syscall; ret
    stub_functions[index] = fn;
    std::memcpy(stub, code, sizeof(code));
    stub_count.store(index + 1, std::memory_order_release);
    const auto address = reinterpret_cast<uintptr_t>(stub);
    stub_of.emplace(fn, address);
    return address;
}

uint64_t guest_cpu_call(uintptr_t fn, unsigned count, const uint64_t* args) {
    using namespace FEXCore::X86State;
    static constexpr int arg_regs[6] = {REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9};
    GuestThread* t = Current();
    auto& s = t->thread->CurrentFrame->State;
    uint64_t saved[16];
    std::memcpy(saved, s.gregs, sizeof(saved));
    const uint64_t saved_rip = s.rip;
    uintptr_t sp;
    if (t->depth) {
        sp = s.gregs[REG_RSP] - 128; // below the red zone of the guest code that called the host
    } else {
        if (!t->stack_top) {
            auto* stack = static_cast<unsigned char*>(low_map(DefaultStack, PROT_READ | PROT_WRITE));
            if (!stack) {
                Fatal("cannot allocate a guest stack");
            }
            t->stack_top = reinterpret_cast<uintptr_t>(stack) + DefaultStack;
        }
        sp = t->stack_top - StackHeadroom;
    }
    const unsigned stack_args = count > 6 ? count - 6 : 0;
    sp = (sp - stack_args * 8) & ~uintptr_t(15);
    for (unsigned i = 0; i < stack_args; ++i) {
        reinterpret_cast<uint64_t*>(sp)[i] = args[6 + i];
    }
    // HandleCallback pushes its return trampoline at RSP - 16: the callee finds its return
    // address at sp - 8 and its stack arguments from sp, as after a call.
    s.gregs[REG_RSP] = sp + 8;
    for (unsigned i = 0; i < count && i < 6; ++i) {
        s.gregs[arg_regs[i]] = args[i];
    }
    s.gregs[REG_RAX] = 0;
    ++t->depth;
    ctx->HandleCallback(t->thread, fn);
    --t->depth;
    const uint64_t result = s.gregs[REG_RAX];
    std::memcpy(s.gregs, saved, sizeof(saved));
    s.rip = saved_rip;
    return result;
}

void guest_cpu_thread_stack(void* top, size_t size) {
    (void)size;
    Current()->stack_top = reinterpret_cast<uintptr_t>(top) & ~uintptr_t(15);
}

void guest_cpu_set_gs(void* base) {
    auto& s = Current()->thread->CurrentFrame->State;
    s.gs_cached = reinterpret_cast<uint64_t>(base);
    s.fs_cached = reinterpret_cast<uint64_t>(base);
}

void guest_cpu_abandon(void) {
    if (self) {
        self->depth = 0;
    }
}

void guest_cpu_thread_end(void) {
    GuestThread* t = self;
    if (!t) {
        return;
    }
    {
        std::lock_guard lock(threads_lock);
        std::erase(threads, t);
    }
    self = nullptr;
    ctx->DestroyThread(t->thread);
    munmap(reinterpret_cast<void*>(t->callret_alloc), CallRetAlloc);
    delete t;
}

uintptr_t guest_cpu_return_address(uintptr_t native) {
    (void)native;
    if (!self || !self->depth) {
        return 0;
    }
    return ReadGuest(self->thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSP]);
}

uintptr_t guest_cpu_stack_pointer(void) {
    return self && self->depth ? self->thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSP] : 0;
}

int guest_cpu_signal_regs(const void* ucontext, GuestRegs* regs) {
    const auto* uc = static_cast<const ucontext_t*>(ucontext);
    const uint64_t pc = uc->uc_mcontext->__ss.__pc;
    GuestThread* t = self;
    if (t) {
        const auto& s = t->thread->CurrentFrame->State;
        if (ctx->IsAddressInCodeBuffer(t->thread, pc)) {
            std::memcpy(regs->gpr, s.gregs, sizeof(regs->gpr));
            const auto& config = delegator.GetConfig();
            for (unsigned i = 0; i < config.SRAGPRCount; ++i) {
                regs->gpr[i] = Gprs(uc)[config.SRAGPRMapping[i]];
            }
            regs->rip = ctx->RestoreRIPFromHostPC(t->thread, pc);
            return GUEST_CPU_IN_GUEST;
        }
        // A fault the guest code raised itself (ud2, int3, hlt): the JIT flushed the registers and
        // RIP to the state, then left its code buffer for the signal stub.
        if (t->thread->CurrentFrame->SynchronousFaultData.FaultToTopAndGeneratedException) {
            std::memcpy(regs->gpr, s.gregs, sizeof(regs->gpr));
            regs->rip = s.rip;
            return GUEST_CPU_IN_GUEST;
        }
        if (t->depth) {
            std::memcpy(regs->gpr, s.gregs, sizeof(regs->gpr));
            regs->rip = ReadGuest(s.gregs[FEXCore::X86State::REG_RSP]);
            return GUEST_CPU_IN_HOST_CALL;
        }
    }
    std::memset(regs, 0, sizeof(*regs));
    regs->rip = pc;
    regs->gpr[GUEST_RSP] = uc->uc_mcontext->__ss.__sp;
    return 0;
}

uintptr_t guest_cpu_host_pc(const void* ucontext) {
    return static_cast<const ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
}

int guest_cpu_handle_fault(int sig, siginfo_t* info, void* ucontext) {
    GuestThread* t = self;
    if (!t) {
        return 0;
    }
    auto* uc = static_cast<ucontext_t*>(ucontext);
    auto& ss = uc->uc_mcontext->__ss;
    const auto address = reinterpret_cast<uintptr_t>(info->si_addr);
    InternalThreadState* thread = t->thread;
    if (IsFaultSignal(sig)) {
        // Call-return stack overflow or underflow: the stack is only a prediction, start it again.
        if (address - t->callret_alloc < CallRetAlloc) {
            ss.__x[25] = DefaultCallRet(t);
            return 1;
        }
        // The JIT's temporary code buffer filled up while compiling: FEXCore restarts the compilation.
        if (thread->JITGuardPage && address - thread->JITGuardPage < HostPage) {
            FEXCore::UncheckedLongJump::ManuallyLoadJumpBuf(thread->RestartJump, thread->JITGuardOverflowArgument, Gprs(uc),
                                                            reinterpret_cast<__uint128_t*>(uc->uc_mcontext->__ns.__v),
                                                            reinterpret_cast<uint64_t*>(&ss.__pc));
            return 1;
        }
    }
    // Unaligned atomics of guest code (x86 allows them): FEXCore rewrites the access.
    if (sig == SIGBUS && info->si_code == BUS_ADRALN && ctx->IsAddressInCodeBuffer(thread, ss.__pc)) {
        if (const auto skip = FEXCore::ArchHelpers::Arm64::HandleUnalignedAccess(thread, unaligned_type, ss.__pc, Gprs(uc))) {
            ss.__pc += *skip;
            return 1;
        }
    }
    return 0;
}

int guest_cpu_hook(uintptr_t address, size_t size, GuestHook hook) {
    if (size < 2) {
        return 0;
    }
    const size_t index = hook_count.load(std::memory_order_relaxed);
    if (index >= hooks.size()) {
        return 0;
    }
    // Guest code is only read by FEXCore, never executed by the host: it needs no execute access.
    auto* page = reinterpret_cast<void*>(address & ~uintptr_t(HostPage - 1));
    const size_t span = address + 2 - reinterpret_cast<uintptr_t>(page);
    if (mprotect(page, span, PROT_READ | PROT_WRITE) != 0) {
        return 0;
    }
    hooks[index] = {address, hook};
    hook_count.store(index + 1, std::memory_order_release);
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0x0f; // syscall
    code[1] = 0x05;
    mprotect(page, span, PROT_READ);
    InvalidateCode(address, size);
    return 1;
}

uint64_t guest_cpu_tsc(void) {
    uint64_t value;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value << tsc_shift;
}

uint64_t guest_cpu_tsc_frequency(void) {
    uint64_t frequency;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency << tsc_shift;
}
} // extern "C"
