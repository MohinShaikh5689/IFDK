// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 2 self-test: what this iPhone gives us (address map, memory limits, the private
// no-footprint ledger flag, memfd-style aliasing) and whether FEXCore runs x86-64 code here.
#include "gow3core.h"
#include "../cpu/guest_cpu.h"
#include "../jit/jit_pool.h"

#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <mach-o/dyld.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <os/proc.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

#ifndef VM_LEDGER_TAG_DEFAULT
#define VM_LEDGER_TAG_DEFAULT 0
#endif
#ifndef VM_LEDGER_FLAG_NO_FOOTPRINT
#define VM_LEDGER_FLAG_NO_FOOTPRINT (1 << 0)
#endif
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
// Private, exported by libsystem_kernel (Madeira and MeloNX use it for their JIT pools).
extern "C" kern_return_t mach_memory_entry_ownership(mach_port_t mem_entry, mach_port_t owner, int ledger_tag, int ledger_flags);

namespace {
constexpr size_t MiB = 1024 * 1024;

struct Json {
    std::string s = "{";
    void raw(const char* key, const std::string& value) {
        if (s.size() > 1) s += ',';
        s += '"';
        s += key;
        s += "\":";
        s += value;
    }
    void num(const char* key, long long v) { raw(key, std::to_string(v)); }
    void unum(const char* key, unsigned long long v) { raw(key, std::to_string(v)); }
    void hex(const char* key, unsigned long long v) {
        char b[32];
        std::snprintf(b, sizeof(b), "\"0x%llx\"", v);
        raw(key, b);
    }
    void str(const char* key, const char* v) {
        std::string q = "\"";
        for (const char* p = v; *p; ++p) {
            if (*p == '"' || *p == '\\') q += '\\';
            if ((unsigned char)*p >= 0x20) q += *p;
        }
        raw(key, q + "\"");
    }
    void boolean(const char* key, bool v) { raw(key, v ? "true" : "false"); }
    std::string done() { return s + "}"; }
};

size_t Emit(const std::string& json, char* out, size_t capacity) {
    if (out && capacity) {
        const size_t n = json.size() < capacity - 1 ? json.size() : capacity - 1;
        std::memcpy(out, json.data(), n);
        out[n] = 0;
    }
    return json.size();
}

int log_fd = -1;
void Log(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    // While a self-test runs, stderr is the log file, so FEXCore's messages and Fatal() land there too.
    (void)write(STDERR_FILENO, line, n);
    if (log_fd >= 0) fsync(log_fd);
}

// Writes the faulting signal, pc, fault address and their images to the log, then lets the
// crash happen as before. SIGTRAP from the JIT protocol's brk #0xf00d goes to the earlier handler.
struct sigaction previous[32];
constexpr int CrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE, SIGSYS};

void Where(char* out, size_t cap, uint64_t address) {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(address), &info) && info.dli_fname) {
        const char* base = std::strrchr(info.dli_fname, '/');
        std::snprintf(out, cap, "%s+%#llx %s", base ? base + 1 : info.dli_fname,
                      (unsigned long long)(address - reinterpret_cast<uintptr_t>(info.dli_fbase)), info.dli_sname ? info.dli_sname : "");
    } else {
        std::snprintf(out, cap, "?");
    }
}

void Crash(int sig, siginfo_t* info, void* context) {
    auto* uc = static_cast<ucontext_t*>(context);
    const auto& ss = uc->uc_mcontext->__ss;
    if (sig == SIGTRAP && !(ss.__pc & 3) && *reinterpret_cast<const uint32_t*>(ss.__pc) == 0xd43e01a0u && previous[sig].sa_sigaction) {
        previous[sig].sa_sigaction(sig, info, context); // brk #0xf00d
        return;
    }
    if (sig != SIGTRAP && guest_cpu_handle_fault(sig, info, context)) return; // FEXCore's own faults, as the runtime does
    char pc[256], lr[256];
    Where(pc, sizeof(pc), ss.__pc);
    Where(lr, sizeof(lr), ss.__lr);
    Log("CRASH: signal %d code %d addr %#llx far %#llx esr %#x", sig, info->si_code, (unsigned long long)(uintptr_t)info->si_addr,
        (unsigned long long)uc->uc_mcontext->__es.__far, uc->uc_mcontext->__es.__esr);
    Log("CRASH: pc %#llx %s", (unsigned long long)ss.__pc, pc);
    Log("CRASH: lr %#llx %s", (unsigned long long)ss.__lr, lr);
    Log("CRASH: sp %#llx x0 %#llx x1 %#llx x8 %#llx x16 %#llx", (unsigned long long)ss.__sp, (unsigned long long)ss.__x[0],
        (unsigned long long)ss.__x[1], (unsigned long long)ss.__x[8], (unsigned long long)ss.__x[16]);
    if (sig == SIGTRAP || sig == SIGILL) {
        Log("CRASH: instruction %#x", (ss.__pc & 3) ? 0u : *reinterpret_cast<const uint32_t*>(ss.__pc));
    }
    uint64_t fp = ss.__fp;
    for (int i = 0; i < 12 && fp && !(fp & 7); ++i) { // frame-pointer walk
        const uint64_t ret = reinterpret_cast<const uint64_t*>(fp)[1];
        if (!ret) break;
        char where[256];
        Where(where, sizeof(where), ret);
        Log("CRASH: #%d %#llx %s", i, (unsigned long long)ret, where);
        fp = reinterpret_cast<const uint64_t*>(fp)[0];
    }
    signal(sig, SIG_DFL); // returning re-executes the faulting instruction, which now crashes normally
    if (sig == SIGABRT) raise(sig);
}

void InstallCrashLog() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = Crash;
    for (int sig : CrashSignals) sigaction(sig, &sa, &previous[sig]);
}

uint64_t Footprint() {
    task_vm_info_data_t vmi;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmi, &count) == KERN_SUCCESS ? vmi.phys_footprint : 0;
}

std::string Sysctl(const char* name) {
    char buf[128] = {};
    size_t len = sizeof(buf) - 1;
    return sysctlbyname(name, buf, &len, nullptr, 0) == 0 ? std::string(buf) : std::string("?");
}

// "<key>name</key>" followed by <true/> in an entitlements plist.
bool PlistTrue(const std::string& xml, const char* name) {
    size_t at = xml.find(std::string("<key>") + name + "</key>");
    if (at == std::string::npos) return false;
    at = xml.find('<', at + std::strlen(name) + 11);
    return at != std::string::npos && xml.compare(at, 7, "<true/>") == 0;
}

// What the code signature carries (csops CS_OPS_ENTITLEMENTS_BLOB: 8-byte blob header, then XML).
std::string SignedEntitlements() {
    std::string buf(256 * 1024, '\0');
    if (csops(getpid(), 7, buf.data(), buf.size()) != 0) return {};
    return buf.substr(8, buf.find('\0', 8) - 8);
}

// What the provisioning profile grants. A free account's profile lacks the kernel memory keys
// unless GetMoreRam added them to the App ID, and then the signature can't use them either.
std::string ProfileEntitlements() {
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return {};
    std::string dir(path);
    dir.resize(dir.rfind('/'));
    std::ifstream in(dir + "/embedded.mobileprovision", std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t start = data.find("<key>Entitlements</key>"), end = data.find("</dict>", start);
    return start == std::string::npos || end == std::string::npos ? std::string() : data.substr(start, end - start);
}

double Seconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// A named memory entry mapped twice: what replaces memfd_create for the PS4 direct-memory pool.
void TestAliasing(Json& j) {
    mach_port_t task = mach_task_self();
    memory_object_size_t size = 64 * MiB;
    mach_port_t entry = MACH_PORT_NULL;
    kern_return_t kr = mach_make_memory_entry_64(task, &size, 0, MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    j.num("alias_entry_kr", kr);
    if (kr != KERN_SUCCESS) return;
    vm_address_t a = 0, b = 0;
    kern_return_t ka = vm_map(task, &a, size, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    kern_return_t kb = vm_map(task, &b, size, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    j.num("alias_map_kr", ka != KERN_SUCCESS ? ka : kb);
    if (ka == KERN_SUCCESS && kb == KERN_SUCCESS) {
        reinterpret_cast<volatile uint64_t*>(a)[12345] = 0x6f77335f696f73ull;
        j.boolean("alias_ok", reinterpret_cast<volatile uint64_t*>(b)[12345] == 0x6f77335f696f73ull);
        // Discard (the memfd's punched hole): pages read back as zero.
        kern_return_t kd = vm_deallocate(task, a, size);
        vm_address_t again = a;
        kern_return_t kr2 = vm_map(task, &again, size, 0, VM_FLAGS_FIXED, entry, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
        j.boolean("alias_remap_fixed_ok", kd == KERN_SUCCESS && kr2 == KERN_SUCCESS && again == a);
        vm_deallocate(task, again, size);
    }
    if (kb == KERN_SUCCESS) vm_deallocate(task, b, size);
    mach_port_deallocate(task, entry);
}

// Touching `mb` MiB of a named entry, with and without VM_LEDGER_FLAG_NO_FOOTPRINT: does it
// count toward the jetsam footprint? Owner: 0 none (plain), 1 this task, 2 TASK_NULL.
void TestFootprint(Json& j, const char* key, size_t mb, int owner) {
    mach_port_t task = mach_task_self();
    memory_object_size_t size = mb * MiB;
    mach_port_t entry = MACH_PORT_NULL;
    kern_return_t kr = mach_make_memory_entry_64(task, &size, 0, MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    std::string k = key;
    if (kr != KERN_SUCCESS) {
        j.num((k + "_entry_kr").c_str(), kr);
        return;
    }
    if (owner) {
        kern_return_t ko = mach_memory_entry_ownership(entry, owner == 1 ? task : TASK_NULL, VM_LEDGER_TAG_DEFAULT, VM_LEDGER_FLAG_NO_FOOTPRINT);
        j.num((k + "_ownership_kr").c_str(), ko);
    }
    vm_address_t a = 0;
    kr = vm_map(task, &a, size, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    if (kr == KERN_SUCCESS) {
        const uint64_t before = Footprint();
        for (size_t off = 0; off < size; off += 16384) reinterpret_cast<volatile char*>(a)[off] = 1;
        const uint64_t after = Footprint();
        j.num((k + "_footprint_delta_mb").c_str(), (long long)((int64_t)(after - before) / (int64_t)MiB));
        vm_deallocate(task, a, size);
    } else {
        j.num((k + "_map_kr").c_str(), kr);
    }
    mach_port_deallocate(task, entry);
}

// The memfd replacement that may stay out of the jetsam budget: a file in the sandbox mapped
// MAP_SHARED (twice, as the runtime's pool views are). Dirty file-backed pages can be written
// back to the file, so iOS may not charge them to phys_footprint like anonymous memory.
void TestFileBacked(Json& j, size_t mb) {
    const char* tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp ? tmp : "/tmp") + "/gow3-pool-test.bin";
    const size_t size = mb * MiB;
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        j.num("file_open_errno", errno);
        return;
    }
    unlink(path.c_str());
    if (ftruncate(fd, (off_t)size) != 0) {
        j.num("file_truncate_errno", errno);
        close(fd);
        return;
    }
    void* a = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    void* b = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED || b == MAP_FAILED) {
        j.num("file_mmap_errno", errno);
    } else {
        const uint64_t before = Footprint();
        for (size_t off = 0; off < size; off += 16384) reinterpret_cast<volatile char*>(a)[off] = 1;
        const uint64_t after = Footprint();
        j.num("file_512_footprint_delta_mb", (long long)((int64_t)(after - before) / (int64_t)MiB));
        reinterpret_cast<volatile uint64_t*>(a)[777] = 0x6f77335f696f73ull;
        j.boolean("file_alias_ok", reinterpret_cast<volatile uint64_t*>(b)[777] == 0x6f77335f696f73ull);
        j.unum("file_available_after_mb", os_proc_available_memory() / MiB);
    }
    if (a != MAP_FAILED) munmap(a, size);
    if (b != MAP_FAILED) munmap(b, size);
    close(fd);
}

// Where the address space is actually free: walk the regions and report the largest holes.
void TestVaMap(Json& j) {
    task_vm_info_data_t vmi;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmi, &count) != KERN_SUCCESS) return;
    struct Hole { uint64_t start, size; };
    Hole top[6] = {};
    uint64_t cursor = vmi.min_address, total_free = 0;
    unsigned regions = 0;
    std::string big_regions = "[";
    for (;;) {
        vm_address_t address = cursor;
        vm_size_t size = 0;
        natural_t depth = 0;
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
        const bool found = vm_region_recurse_64(mach_task_self(), &address, &size, &depth, (vm_region_recurse_info_t)&info, &info_count) == KERN_SUCCESS;
        const uint64_t next = found ? address : vmi.max_address;
        if (next > cursor) {
            const Hole h {cursor, next - cursor};
            total_free += h.size;
            Hole* smallest = &top[0];
            for (auto& t : top) {
                if (t.size < smallest->size) smallest = &t;
            }
            if (h.size > smallest->size) *smallest = h;
        }
        if (!found) break;
        ++regions;
        if (size >= (1ull << 30) && big_regions.size() < 900) {
            char b[96];
            std::snprintf(b, sizeof(b), "%s\"0x%llx+%lluG tag%u prot%d\"", big_regions.size() > 1 ? "," : "",
                          (unsigned long long)address, (unsigned long long)(size >> 30), info.user_tag, info.protection);
            big_regions += b;
        }
        cursor = address + size;
        if (cursor >= vmi.max_address) break;
    }
    j.unum("va_regions", regions);
    j.unum("va_free_gb", total_free >> 30);
    std::string holes = "[";
    for (const auto& t : top) {
        if (!t.size) continue;
        char b[64];
        std::snprintf(b, sizeof(b), "%s\"0x%llx+%lluM\"", holes.size() > 1 ? "," : "", (unsigned long long)t.start, (unsigned long long)(t.size >> 20));
        holes += b;
    }
    j.raw("va_largest_holes", holes + "]");
    j.raw("va_regions_over_1g", big_regions + "]");
}

// How much can be reserved (not touched) anywhere, the way the PS4 pool and user range would be.
void TestReserveAnywhere(Json& j) {
    std::string out = "{";
    for (uint64_t gb : {4ull, 8ull, 16ull, 32ull, 64ull}) {
        vm_address_t a = 0;
        kern_return_t kr = vm_allocate(mach_task_self(), &a, gb << 30, VM_FLAGS_ANYWHERE);
        char b[64];
        std::snprintf(b, sizeof(b), "%s\"%lluG\":\"%s\"", out.size() > 1 ? "," : "", (unsigned long long)gb,
                      kr == KERN_SUCCESS ? "ok" : (kr == KERN_NO_SPACE ? "no_space" : "fail"));
        out += b;
        if (kr == KERN_SUCCESS) {
            char at[48];
            std::snprintf(at, sizeof(at), "reserve_%llug_at", (unsigned long long)gb);
            j.hex(at, a);
            vm_deallocate(mach_task_self(), a, gb << 30);
        }
    }
    j.raw("reserve_anywhere", out + "}");
}

// iOS fences off [64 GiB, 448 GiB) with a VM_MEMORY_GUARD region (tag 31, no access) for apps
// without the extended-VA entitlement. If a piece of it can be released and remapped, the PS4
// user range fits where the runtime expects it. Tries 1 GiB at the start of the region.
void TestGuard(Json& j) {
    mach_port_t task = mach_task_self();
    vm_address_t at = 0x1000000000ull;
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = vm_region_64(task, &at, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
    if (kr != KERN_SUCCESS || at != 0x1000000000ull) {
        j.str("guard", "no region at 64 GiB");
        return;
    }
    j.hex("guard_size", size);
    j.num("guard_max_prot", info.max_protection);
    j.num("guard_protect_rw_kr", vm_protect(task, at, 16384, FALSE, VM_PROT_READ | VM_PROT_WRITE));
    kr = vm_deallocate(task, at, 1ull << 30);
    j.num("guard_release_kr", kr);
    if (kr != KERN_SUCCESS) return;
    vm_address_t fixed = at;
    kr = vm_allocate(task, &fixed, 1ull << 30, VM_FLAGS_FIXED);
    j.num("guard_fixed_alloc_kr", kr);
    if (kr == KERN_SUCCESS) {
        *reinterpret_cast<volatile uint64_t*>(fixed) = 42; // the pages are really usable
        j.boolean("guard_fixed_write_ok", *reinterpret_cast<volatile uint64_t*>(fixed) == 42);
        vm_deallocate(task, fixed, 1ull << 30);
    }
}

// The address window the gow3 runtime wants (runtime_memory.c): low host map from 32 GiB, PS4
// user range from 64 GiB. Reserve without touching, then release.
void TestWindow(Json& j, const char* key, uint64_t address, uint64_t size) {
    vm_address_t a = address;
    kern_return_t kr = vm_allocate(mach_task_self(), &a, size, VM_FLAGS_FIXED);
    j.num(key, kr);
    if (kr == KERN_SUCCESS) vm_deallocate(mach_task_self(), a, size);
}

void* LowMap(size_t size, int prot) {
    void* p = mmap(nullptr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

uint64_t HostMulAdd(uint64_t a, uint64_t b, uint64_t c) {
    return a * b + c;
}

void TestFex(Json& j) {
    static unsigned char* code;
    if (!code) { // FEXCore is set up once per process; later runs reuse it
        Log("fex: guest_cpu_init");
        guest_cpu_init(LowMap);
        Log("fex: init done, pool used %zu KiB", gow3_jit_pool_used() / 1024);
        code = static_cast<unsigned char*>(LowMap(64 * 1024, PROT_READ | PROT_WRITE));
        if (!code) {
            j.str("fex_error", "cannot map guest code");
            return;
        }
        guest_cpu_code(reinterpret_cast<uintptr_t>(code), 64 * 1024);
    }

    // A: mov eax, 42; ret
    static const unsigned char a[] = {0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3};
    std::memcpy(code, a, sizeof(a));
    Log("fex: test A (mov eax, 42)");
    j.unum("fex_return_42", guest_cpu_call(reinterpret_cast<uintptr_t>(code), 0, nullptr));

    // B: lea rax, [rdi + rsi]; ret, with arguments from the host
    static const unsigned char b[] = {0x48, 0x8d, 0x04, 0x37, 0xc3};
    std::memcpy(code + 0x100, b, sizeof(b));
    const uint64_t args[2] = {40, 2};
    Log("fex: test B (arguments)");
    j.unum("fex_args_42", guest_cpu_call(reinterpret_cast<uintptr_t>(code + 0x100), 2, args));

    // C: the guest calls a host function: 6 * 7 + 0
    const uint64_t stub = guest_cpu_host_function(reinterpret_cast<void*>(HostMulAdd));
    unsigned char c[] = {
        0x48, 0x83, 0xec, 0x08,       // sub rsp, 8
        0xbf, 0x06, 0x00, 0x00, 0x00, // mov edi, 6
        0xbe, 0x07, 0x00, 0x00, 0x00, // mov esi, 7
        0xba, 0x00, 0x00, 0x00, 0x00, // mov edx, 0
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, // movabs rax, stub
        0xff, 0xd0,                   // call rax
        0x48, 0x83, 0xc4, 0x08,       // add rsp, 8
        0xc3,                         // ret
    };
    std::memcpy(c + 21, &stub, sizeof(stub));
    std::memcpy(code + 0x200, c, sizeof(c));
    Log("fex: test C (host call)");
    j.unum("fex_host_call_42", guest_cpu_call(reinterpret_cast<uintptr_t>(code + 0x200), 0, nullptr));

    // D: a hot loop, sum 1..N, to compare translated and native speed.
    constexpr uint32_t N = 200'000'000;
    unsigned char d[] = {
        0x31, 0xc0,                   // xor eax, eax
        0xb9, 0, 0, 0, 0,             // mov ecx, N
        0x48, 0x01, 0xc8,             // loop: add rax, rcx
        0xff, 0xc9,                   // dec ecx
        0x75, 0xf9,                   // jnz loop
        0xc3,                         // ret
    };
    std::memcpy(d + 3, &N, sizeof(N));
    std::memcpy(code + 0x300, d, sizeof(d));
    Log("fex: test D (loop)");
    auto t0 = std::chrono::steady_clock::now();
    const uint64_t sum = guest_cpu_call(reinterpret_cast<uintptr_t>(code + 0x300), 0, nullptr);
    auto t1 = std::chrono::steady_clock::now();
    volatile uint64_t native = 0;
    for (uint64_t i = N; i; --i) native = native + i;
    auto t2 = std::chrono::steady_clock::now();
    j.boolean("fex_loop_ok", sum == (uint64_t)N * (N + 1) / 2);
    j.num("fex_loop_ms", (long long)(Seconds(t0, t1) * 1000));
    j.num("native_loop_ms", (long long)(Seconds(t1, t2) * 1000));
    j.unum("jit_pool_used_kb", gow3_jit_pool_used() / 1024);
    Log("fex: all tests done");
}
} // namespace

extern "C" {
size_t gow3_device_report(char* out, size_t capacity) {
    Json j;
    j.str("machine", Sysctl("hw.machine").c_str());
    j.str("os", Sysctl("kern.osversion").c_str());
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0);
    j.unum("ram_mb", memsize / MiB);
    j.num("page_size", sysconf(_SC_PAGESIZE));
    task_vm_info_data_t vmi;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmi, &count) == KERN_SUCCESS) {
        j.hex("va_min", vmi.min_address);
        j.hex("va_max", vmi.max_address);
        j.unum("va_gb", (vmi.max_address - vmi.min_address) >> 30);
        j.unum("footprint_mb", vmi.phys_footprint / MiB);
        j.unum("footprint_limit_mb", (uint64_t)vmi.limit_bytes_remaining / MiB + vmi.phys_footprint / MiB);
    }
    j.unum("available_mb", os_proc_available_memory() / MiB);
    const std::string signed_ents = SignedEntitlements(), profile_ents = ProfileEntitlements();
    j.boolean("ent_increased_memory", PlistTrue(signed_ents, "com.apple.developer.kernel.increased-memory-limit"));
    j.boolean("ent_extended_va", PlistTrue(signed_ents, "com.apple.developer.kernel.extended-virtual-addressing"));
    j.boolean("profile_increased_memory", PlistTrue(profile_ents, "com.apple.developer.kernel.increased-memory-limit"));
    j.boolean("profile_extended_va", PlistTrue(profile_ents, "com.apple.developer.kernel.extended-virtual-addressing"));
    j.hex("cs_flags", gow3_jit_cs_flags());
    j.boolean("cs_debugged", gow3_jit_cs_debugged());
    j.boolean("debugger_attached", gow3_jit_traced());
    j.boolean("jit_pool", gow3_jit_pool_ready());
    if (gow3_jit_pool_ready()) {
        j.hex("jit_pool_rx", gow3_jit_pool_rx());
        j.unum("jit_pool_mb", gow3_jit_pool_size() / MiB);
    }
    return Emit(j.done(), out, capacity);
}

int gow3_jit_debugged(void) {
    return gow3_jit_cs_debugged();
}

int gow3_jit_debugger_attached(void) {
    return gow3_jit_traced();
}

int gow3_jit_setup(unsigned pool_mb) {
    if (gow3_jit_pool_ready()) return 0;
    const int result = gow3_jit_pool_create((size_t)pool_mb * MiB);
    gow3_jit_detach(); // answered by the debugger, or skipped by the trap fallback
    return result;
}

size_t gow3_selftest(const char* log_path, char* out, size_t capacity) {
    if (log_path) log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    const int saved_stderr = dup(STDERR_FILENO);
    if (log_fd >= 0) dup2(log_fd, STDERR_FILENO);
    gow3_jit_install_trap_fallback();
    InstallCrashLog();
    Json j;
    Log("selftest: start, footprint %" PRIu64 " MiB, available %zu MiB", Footprint() / MiB, os_proc_available_memory() / MiB);
    Log("selftest: memory aliasing");
    TestAliasing(j);
    Log("selftest: footprint ledger");
    TestFootprint(j, "plain_256", 256, 0);
    TestFootprint(j, "nofootprint_256", 256, 1);
    TestFootprint(j, "nofootprint_null_256", 256, 2);
    Log("selftest: file-backed pool");
    TestFileBacked(j, 512);
    Log("selftest: address map");
    TestVaMap(j);
    TestReserveAnywhere(j);
    Log("selftest: guard region");
    TestGuard(j);
    Log("selftest: address window");
    TestWindow(j, "window_low_32g_1g_kr", 0x0800000000ull, 1ull << 30);
    TestWindow(j, "window_user_64g_8g_kr", 0x1000000000ull, 8ull << 30);
    TestWindow(j, "window_user_256g_64g_kr", 0x4000000000ull, 64ull << 30);
    j.boolean("jit_pool", gow3_jit_pool_ready());
    if (gow3_jit_pool_ready()) {
        Log("selftest: native JIT");
        j.num("native_jit_42", gow3_jit_native_test());
        TestFex(j);
    } else {
        j.str("fex", "skipped: enable JIT first");
    }
    Log("selftest: done");
    if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
    return Emit(j.done(), out, capacity);
}
}
