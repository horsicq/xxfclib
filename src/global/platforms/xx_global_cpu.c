/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_global_cpu.c
 * @brief CPU feature detection.
 *
 * The axis here is instruction set and compiler, not operating system: MSVC
 * and GCC disagree about how to spell CPUID even when both target Windows on
 * x86-64, so a windows/posix pair cannot express it. The file is selected
 * unconditionally and forks internally, the way
 * src/data/platforms/xx_data_avx2.c does, with a stub for architectures that
 * have neither SSE2 nor AVX2.
 *
 * The compiler intrinsics below have to stay at their use site, which is why
 * this file has conditionals and xx_global.c now has none.
 */

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)

#include "xx_global_platform.h"

/* ========================================================================= */
/* --- CPU Feature Detection & Configuration (SSE2 / AVX2)               --- */
/* ========================================================================= */

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#  include <intrin.h>
#  include <immintrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
#  include <cpuid.h>
#  include <immintrin.h>
#endif

static bool detect_sse2_hardware(void) {
#if defined(_M_X64) || defined(__x86_64__)
    /* All x86-64 processors support SSE2 by architectural specification */
    return true;
#elif defined(_MSC_VER) && defined(_M_IX86)
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    if (cpu_info[0] < 1) {
        return false;
    }
    __cpuid(cpu_info, 1);
    return (cpu_info[3] & (1 << 26)) != 0; /* EDX bit 26 = SSE2 */
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__i386__)
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return false;
    }
    return (edx & (1 << 26)) != 0;
#else
    return false;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
#  if defined(__i386__) || defined(__x86_64__)
#    define XX_GLOBAL_DETECT_AVX2 __attribute__((target("avx2")))
#  else
#    define XX_GLOBAL_DETECT_AVX2
#  endif
#else
#  define XX_GLOBAL_DETECT_AVX2
#endif

XX_GLOBAL_DETECT_AVX2
static bool detect_avx2_hardware(void) {
#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
    int cpu_info[4] = {0};

    /* 1. Check max supported CPUID level */
#if defined(_MSC_VER)
    __cpuid(cpu_info, 0);
#else
    __cpuid_count(0, 0, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif
    if (cpu_info[0] < 7) {
        return false;
    }

    /* 2. Check OSXSAVE (ECX bit 27) and AVX (ECX bit 28) in leaf 1 */
#if defined(_MSC_VER)
    __cpuid(cpu_info, 1);
#else
    __cpuid_count(1, 0, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif
    bool osxsave = (cpu_info[2] & (1 << 27)) != 0;
    bool avx = (cpu_info[2] & (1 << 28)) != 0;
    if (!osxsave || !avx) {
        return false;
    }

    /* 3. Check XCR0 (OS enabled saving XMM state bit 1 and YMM state bit 2) */
    unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) {
        return false;
    }

    /* 4. Check AVX2 support in leaf 7, subleaf 0 (EBX bit 5) */
#if defined(_MSC_VER)
    __cpuidex(cpu_info, 7, 0);
#else
    __cpuid_count(7, 0, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif
    return (cpu_info[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}

bool xx_global_platform_has_sse2(void) {
    return detect_sse2_hardware();
}

bool xx_global_platform_has_avx2(void) {
    return detect_avx2_hardware();
}

#else /* not x86: the SIMD paths these gate do not exist here */

#include "xx_global_platform.h"

bool xx_global_platform_has_sse2(void) {
    return false;
}

bool xx_global_platform_has_avx2(void) {
    return false;
}

#endif
