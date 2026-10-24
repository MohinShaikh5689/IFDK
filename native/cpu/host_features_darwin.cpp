// SPDX-License-Identifier: GPL-3.0-or-later
// FEXCore host features on iOS. FEX's Common/HostFeatures.cpp reads the ID registers (mrs), which
// Linux emulates for EL0 and Darwin does not, so the features come from sysctl instead. Choices
// follow Madeira's iOS module (willfaust/FEX Source/Windows/Common/CPUFeatures.cpp):
//  - FEAT_AFP stays off: FEX then writes FPCR on every JIT entry and exit, which is expensive on
//    Apple cores; scalar SSE takes FEX's ordinary path instead (GOW3_FEX_AFP=1 turns it on).
//  - AVX is reported and run through FEX's 128-bit emulation (no SVE on Apple); the PS4's Jaguar
//    has AVX, so game code may use it (GOW3_FEX_AVX=0 turns it off).
#include "host_features_darwin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/sysctl.h>

namespace {
bool Has(const char* name) {
    int value = 0;
    size_t len = sizeof(value);
    return sysctlbyname(name, &value, &len, nullptr, 0) == 0 && value != 0;
}
bool EnvIs(const char* name, char want) {
    const char* v = std::getenv(name);
    return v && v[0] == want;
}
// GOW3_FEX_OFF=CSSC,RPRES,...: host features FEX must not use, to find a translation that goes
// wrong on Apple cores (the Snapdragons gow3_android runs on lack several of them).
bool Off(const char* feature) {
    const char* list = std::getenv("GOW3_FEX_OFF");
    if (!list) return false;
    const size_t n = std::strlen(feature);
    for (const char* at = list; (at = std::strstr(at, feature)); at += n) {
        const bool starts = at == list || at[-1] == ',';
        const bool ends = at[n] == '\0' || at[n] == ',';
        if (starts && ends) return true;
    }
    return false;
}
} // namespace

FEXCore::HostFeatures gow3_fex_host_features() {
    FEXCore::HostFeatures f {};
    int line = 0;
    size_t len = sizeof(line);
    if (sysctlbyname("hw.cachelinesize", &line, &len, nullptr, 0) != 0 || line <= 0) line = 128;
    f.DCacheLineSize = static_cast<uint32_t>(line);
    f.ICacheLineSize = static_cast<uint32_t>(line);
    f.SupportsCacheMaintenanceOps = true;
    f.SupportsAES = !Off("AES") && Has("hw.optional.arm.FEAT_AES");
    f.SupportsPMULL_128Bit = !Off("PMULL") && Has("hw.optional.arm.FEAT_PMULL");
    f.SupportsCRC = !Off("CRC") && (Has("hw.optional.armv8_crc32") || Has("hw.optional.arm.FEAT_CRC32"));
    f.SupportsAtomics = !Off("LSE") && (Has("hw.optional.arm.FEAT_LSE") || Has("hw.optional.armv8_1_atomics"));
    f.SupportsRCPC = !Off("RCPC") && Has("hw.optional.arm.FEAT_LRCPC");
    f.SupportsTSOImm9 = !Off("RCPC2") && Has("hw.optional.arm.FEAT_LRCPC2");
    f.SupportsSHA = !Off("SHA") && Has("hw.optional.arm.FEAT_SHA1") && Has("hw.optional.arm.FEAT_SHA256");
    f.SupportsFCMA = !Off("FCMA") && (Has("hw.optional.arm.FEAT_FCMA") || Has("hw.optional.armv8_3_compnum"));
    f.SupportsFlagM = !Off("FlagM") && Has("hw.optional.arm.FEAT_FlagM");
    f.SupportsFlagM2 = !Off("FlagM2") && Has("hw.optional.arm.FEAT_FlagM2");
    f.SupportsFRINTTS = !Off("FRINTTS") && Has("hw.optional.arm.FEAT_FRINTTS");
    f.SupportsRPRES = !Off("RPRES") && Has("hw.optional.arm.FEAT_RPRES");
    f.SupportsCSSC = !Off("CSSC") && Has("hw.optional.arm.FEAT_CSSC");
    f.SupportsECV = Has("hw.optional.arm.FEAT_ECV");
    f.SupportsWFXT = Has("hw.optional.arm.FEAT_WFxT");
    // FEX copies rep movs/stos with MOPS; GOW3_FEX_MOPS=0 uses its plain loops (Android's
    // Snapdragons have no MOPS, so that path is the one the game was tested on).
    f.SupportsMOPS = !EnvIs("GOW3_FEX_MOPS", '0') && Has("hw.optional.arm.FEAT_MOPS");
    f.SupportsAFP = EnvIs("GOW3_FEX_AFP", '1') && Has("hw.optional.arm.FEAT_AFP");
    f.SupportsAVX = !EnvIs("GOW3_FEX_AVX", '0');
    f.SupportsAES256 = f.SupportsAVX && f.SupportsAES;
    f.SupportsSVE128 = false;
    f.SupportsSVE256 = false;
    // TPIDRRO_EL0 is Darwin's thread pointer, not a CPU index.
    f.SupportsCPUIndexInTPIDRRO = false;
    int cpus = 0;
    len = sizeof(cpus);
    if (sysctlbyname("hw.ncpu", &cpus, &len, nullptr, 0) != 0 || cpus <= 0) cpus = 6;
    for (int i = 0; i < cpus; ++i) f.CPUMIDRs.push_back(0u);
    f.HostType = FEXCore::HostFeatures::HostTypeEnum::Linux;
    std::fprintf(stderr, "FEX host features: MOPS %d CSSC %d FlagM %d FlagM2 %d FRINTTS %d RPRES %d AFP %d RCPC %d RCPC2 %d "
                 "LSE %d FCMA %d CRC %d AES %d SHA %d PMULL %d AVX %d\n",
                 f.SupportsMOPS, f.SupportsCSSC, f.SupportsFlagM, f.SupportsFlagM2, f.SupportsFRINTTS,
                 f.SupportsRPRES, f.SupportsAFP, f.SupportsRCPC, f.SupportsTSOImm9, f.SupportsAtomics,
                 f.SupportsFCMA, f.SupportsCRC, f.SupportsAES, f.SupportsSHA, f.SupportsPMULL_128Bit,
                 f.SupportsAVX);
    return f;
}
