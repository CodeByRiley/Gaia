#include "cpu.h"
#include "utilities/log.h"

uint64_t cpu_supported_features = 0;
uint64_t cpu_enabled_features   = 0;

typedef struct {
  uint64_t flag;
  const char *name;
} cpu_feature_info_t;

typedef struct {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
} cpuid_result_t;

static const cpu_feature_info_t cpu_feature_names[] = {
    {CPU_FEATURE_SSE,   "SSE"},
    {CPU_FEATURE_SSE2,  "SSE2"},
    {CPU_FEATURE_SSE3,  "SSE3"},
    {CPU_FEATURE_SSSE3, "SSSE3"},
    {CPU_FEATURE_SSE41, "SSE4.1"},
    {CPU_FEATURE_SSE42, "SSE4.2"},
    {CPU_FEATURE_AVX,   "AVX"},
    {CPU_FEATURE_AVX2,  "AVX2"},
    {CPU_FEATURE_FMA,   "FMA3"},
    {CPU_FEATURE_AES,   "AES-NI"},
    {CPU_FEATURE_XSAVE, "XSAVE"},
    {CPU_FEATURE_BMI1,  "BMI1"},
    {CPU_FEATURE_BMI2,  "BMI2"},
    {CPU_FEATURE_SHA,   "SHA"},
};

void cpu_log_features(void) {
  log_write_fmt(KERNEL, LOG_INFO,
                "CPU: supported=0x%llx enabled=0x%llx",
                (unsigned long long)cpu_supported_features,
                (unsigned long long)cpu_enabled_features);

  for (unsigned i = 0;
       i < sizeof(cpu_feature_names) / sizeof(cpu_feature_names[0]);
       i++) {
    const cpu_feature_info_t *feature = &cpu_feature_names[i];

    bool supported = (cpu_supported_features & feature->flag) != 0;
    bool enabled   = (cpu_enabled_features & feature->flag) != 0;

    if (!supported)
      continue;

    log_write_fmt(KERNEL, LOG_INFO,
                  "CPU: %-7s supported=yes enabled=%s",
                  feature->name,
                  enabled ? "yes" : "no");
  }
}

static inline cpuid_result_t cpuid(uint32_t leaf, uint32_t subleaf) {
  cpuid_result_t result;

  __asm__ volatile("cpuid"
                   : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
                   : "a"(leaf), "c"(subleaf));

  return result;
}

void cpu_detect_features(void) {
  cpu_supported_features = 0;
  cpu_enabled_features   = 0;

  cpuid_result_t basic = cpuid(0, 0);

  if (basic.eax < 1)
    return;

  cpuid_result_t features = cpuid(1, 0);

  if (cpuid_has_feature(features.edx, CPU_EDX_SSE))
    cpu_supported_features |= CPU_FEATURE_SSE;

  if (cpuid_has_feature(features.edx, CPU_EDX_SSE2))
    cpu_supported_features |= CPU_FEATURE_SSE2;

  if (cpuid_has_feature(features.ecx, CPU_ECX_SSE3))
    cpu_supported_features |= CPU_FEATURE_SSE3;

  if (cpuid_has_feature(features.ecx, CPU_ECX_SSSE3))
    cpu_supported_features |= CPU_FEATURE_SSSE3;

  if (cpuid_has_feature(features.ecx, CPU_ECX_SSE4_1))
    cpu_supported_features |= CPU_FEATURE_SSE41;

  if (cpuid_has_feature(features.ecx, CPU_ECX_SSE4_2))
    cpu_supported_features |= CPU_FEATURE_SSE42;

  if (cpuid_has_feature(features.ecx, CPU_ECX_FMA))
    cpu_supported_features |= CPU_FEATURE_FMA;

  if (cpuid_has_feature(features.ecx, CPU_ECX_AES))
    cpu_supported_features |= CPU_FEATURE_AES;

  if (cpuid_has_feature(features.ecx, CPU_ECX_XSAVE))
    cpu_supported_features |= CPU_FEATURE_XSAVE;

  if (cpuid_has_feature(features.ecx, CPU_ECX_AVX))
    cpu_supported_features |= CPU_FEATURE_AVX;

  if (basic.eax >= 7) {
    cpuid_result_t structured = cpuid(7, 0);

    if (cpuid_has_feature(structured.ebx, CPU_LEAF7_EBX_BMI1))
      cpu_supported_features |= CPU_FEATURE_BMI1;

    if (cpuid_has_feature(structured.ebx, CPU_LEAF7_EBX_AVX2))
      cpu_supported_features |= CPU_FEATURE_AVX2;

    if (cpuid_has_feature(structured.ebx, CPU_LEAF7_EBX_BMI2))
      cpu_supported_features |= CPU_FEATURE_BMI2;

    if (cpuid_has_feature(structured.ebx, CPU_LEAF7_EBX_SHA))
      cpu_supported_features |= CPU_FEATURE_SHA;
  }

  cpu_enabled_features = cpu_supported_features & (
      CPU_FEATURE_SSE   |
      CPU_FEATURE_SSE2  |
      CPU_FEATURE_SSE3  |
      CPU_FEATURE_SSSE3 |
      CPU_FEATURE_SSE41 |
      CPU_FEATURE_SSE42 |
      CPU_FEATURE_AES   |
      CPU_FEATURE_BMI1  |
      CPU_FEATURE_BMI2  |
      CPU_FEATURE_SHA
  );
}
