#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <utilities/types.h>

#if !defined(__x86_64__) && !defined(__i386__)
#error "cpu.h requires an x86 target"
#endif

enum {
  CPU_EBX_BRAND_INDEX       = 0xFFu << 0,
  CPU_EBX_CLFLUSH_LINE_SIZE = 0xFFu << 8,
  CPU_EBX_APIC_ID_SPACE     = 0xFFu << 16,
  CPU_EBX_INITIAL_APIC_ID   = 0xFFu << 24,

  CPU_ECX_SSE3       = 1u << 0,
  CPU_ECX_PCLMUL     = 1u << 1,
  CPU_ECX_DTES64     = 1u << 2,
  CPU_ECX_MONITOR    = 1u << 3,
  CPU_ECX_DS_CPL     = 1u << 4,
  CPU_ECX_VMX        = 1u << 5,
  CPU_ECX_SMX        = 1u << 6,
  CPU_ECX_EST        = 1u << 7,
  CPU_ECX_TM2        = 1u << 8,
  CPU_ECX_SSSE3      = 1u << 9,
  CPU_ECX_CID        = 1u << 10,
  CPU_ECX_SDBG       = 1u << 11,
  CPU_ECX_FMA        = 1u << 12,
  CPU_ECX_CX16       = 1u << 13,
  CPU_ECX_XTPR       = 1u << 14,
  CPU_ECX_PDCM       = 1u << 15,
  CPU_ECX_PCID       = 1u << 17,
  CPU_ECX_DCA        = 1u << 18,
  CPU_ECX_SSE4_1     = 1u << 19,
  CPU_ECX_SSE4_2     = 1u << 20,
  CPU_ECX_X2APIC     = 1u << 21,
  CPU_ECX_MOVBE      = 1u << 22,
  CPU_ECX_POPCNT     = 1u << 23,
  CPU_ECX_TSC        = 1u << 24,
  CPU_ECX_AES        = 1u << 25,
  CPU_ECX_XSAVE      = 1u << 26,
  CPU_ECX_OSXSAVE    = 1u << 27,
  CPU_ECX_AVX        = 1u << 28,
  CPU_ECX_F16C       = 1u << 29,
  CPU_ECX_RDRAND     = 1u << 30,
  CPU_ECX_HYPERVISOR = 1u << 31,

  CPU_EDX_FPU     = 1u << 0,
  CPU_EDX_VME     = 1u << 1,
  CPU_EDX_DE      = 1u << 2,
  CPU_EDX_PSE     = 1u << 3,
  CPU_EDX_TSC     = 1u << 4,
  CPU_EDX_MSR     = 1u << 5,
  CPU_EDX_PAE     = 1u << 6,
  CPU_EDX_MCE     = 1u << 7,
  CPU_EDX_CX8     = 1u << 8,
  CPU_EDX_APIC    = 1u << 9,
  CPU_EDX_SEP     = 1u << 11,
  CPU_EDX_MTRR    = 1u << 12,
  CPU_EDX_PGE     = 1u << 13,
  CPU_EDX_MCA     = 1u << 14,
  CPU_EDX_CMOV    = 1u << 15,
  CPU_EDX_PAT     = 1u << 16,
  CPU_EDX_PSE36   = 1u << 17,
  CPU_EDX_PSN     = 1u << 18,
  CPU_EDX_CLFLUSH = 1u << 19,
  CPU_EDX_DS      = 1u << 21,
  CPU_EDX_ACPI    = 1u << 22,
  CPU_EDX_MMX     = 1u << 23,
  CPU_EDX_FXSR    = 1u << 24,
  CPU_EDX_SSE     = 1u << 25,
  CPU_EDX_SSE2    = 1u << 26,
  CPU_EDX_SS      = 1u << 27,
  CPU_EDX_HTT     = 1u << 28,
  CPU_EDX_TM      = 1u << 29,
  CPU_EDX_IA64    = 1u << 30,
  CPU_EDX_PBE     = 1u << 31
};

enum {
  CPU_LEAF7_EBX_BMI1 = 1u << 3,
  CPU_LEAF7_EBX_AVX2 = 1u << 5,
  CPU_LEAF7_EBX_BMI2 = 1u << 8,
  CPU_LEAF7_EBX_SHA  = 1u << 29,
};

enum {
  CPU_XCR0_X87 = 1u << 0,
  CPU_XCR0_SSE = 1u << 1,
  CPU_XCR0_AVX = 1u << 2,
};

enum cpu_feature {
  CPU_FEATURE_SSE   = 1ULL << 0,
  CPU_FEATURE_SSE2  = 1ULL << 1,
  CPU_FEATURE_SSE3  = 1ULL << 2,
  CPU_FEATURE_SSSE3 = 1ULL << 3,
  CPU_FEATURE_SSE41 = 1ULL << 4,
  CPU_FEATURE_SSE42 = 1ULL << 5,
  CPU_FEATURE_AVX   = 1ULL << 6,
  CPU_FEATURE_AES   = 1ULL << 7,
  CPU_FEATURE_XSAVE = 1ULL << 8,
  CPU_FEATURE_AVX2  = 1ULL << 9,
  CPU_FEATURE_BMI1  = 1ULL << 10,
  CPU_FEATURE_BMI2  = 1ULL << 11,
  CPU_FEATURE_SHA   = 1ULL << 12,
  CPU_FEATURE_FMA   = 1ULL << 13,
};

extern uint64_t cpu_supported_features;
extern uint64_t cpu_enabled_features;

void cpu_detect_features(void);

static inline bool cpuid_has_bit(uint32_t value, unsigned bit) {
  return bit < 32 && (value & (UINT32_C(1) << bit)) != 0;
}

static inline bool cpu_supports(uint64_t features) {
  return (cpu_supported_features & features) == features;
}

static inline bool cpu_has_feature(uint64_t features) {
  return (cpu_enabled_features & features) == features;
}

static inline bool cpuid_has_feature(uint32_t value, uint32_t mask) {
  return (value & mask) == mask;
}

void cpu_detect_features(void);
void cpu_log_features(void);

SINLINE u64 read_cr3(void) {
  u64 value;

  __asm__ volatile("mov %%cr3, %0" : "=r"(value) : : "memory");

  return value;
}

SINLINE void write_cr3(u64 value) { __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory"); }


#endif /* CPU_H */
