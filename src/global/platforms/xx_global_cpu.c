/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_global_cpu.c
 * @brief CPU feature detection.
 *
 * The axis here is instruction set and compiler, not operating system. x86
 * targets use the compiler's CPUID interface, Windows Arm targets use
 * IsProcessorFeaturePresent, and other Arm targets use their platform feature
 * mechanism with compiler feature macros as a fallback. The file is selected
 * unconditionally and forks internally, the way
 * src/data/platforms/xx_data_avx2.c does.
 *
 * The compiler intrinsics below have to stay at their use site, which is why
 * this file has conditionals and xx_global.c now has none.
 */

#if defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC)) || defined(__i386__) || (defined(__x86_64__) && !defined(_M_ARM64EC))

#include "xx_global_platform.h"
#include "../../algo/sha/platforms/xx_sha_platform.h"
#include "../../algo/crc/platforms/xx_crc64_platform.h"

/* ========================================================================= */
/* --- CPU Feature Detection & Configuration (SSE2 / SSE4.2 / AVX2 / AES / SHA / VPCLMULQDQ) --- */
/* ========================================================================= */

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#include <immintrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#include <immintrin.h>
#endif

static bool detect_sse2_hardware(void)
{
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

static bool detect_sse42_hardware(void)
{
#if defined(_MSC_VER)
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    if (cpu_info[0] < 1) return false;
    __cpuid(cpu_info, 1);
    return (cpu_info[2] & (1 << 20)) != 0; /* ECX bit 20 = SSE4.2 */
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
    return (ecx & (1U << 20)) != 0;
#else
    return false;
#endif
}

static bool detect_aes_hardware(void)
{
#if defined(_MSC_VER)
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    if (cpu_info[0] < 1) return false;
    __cpuid(cpu_info, 1);
    return (cpu_info[2] & (1 << 25)) != 0; /* ECX bit 25 = AES-NI */
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
    return (ecx & (1U << 25)) != 0;
#else
    return false;
#endif
}

static bool detect_sha_hardware(void)
{
#ifdef XX_SHA_HAVE_SHA_NI
#if defined(_MSC_VER)
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    if (cpu_info[0] < 7) return false;
    __cpuidex(cpu_info, 7, 0);
    return (cpu_info[1] & (1 << 29)) != 0; /* Leaf 7, EBX bit 29 = SHA-NI */
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_max(0, NULL) < 7) return false;
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    return (ebx & (1U << 29)) != 0;
#else
    return false;
#endif
#else
    return false;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
#if defined(__i386__) || defined(__x86_64__)
/* _xgetbv requires XSAVE compiler support; detection must not itself use
 * AVX instructions before CPU and OS capability checks have completed. */
#define XX_GLOBAL_DETECT_AVX2 __attribute__((target("xsave")))
#else
#define XX_GLOBAL_DETECT_AVX2
#endif
#else
#define XX_GLOBAL_DETECT_AVX2
#endif

XX_GLOBAL_DETECT_AVX2
static bool detect_avx2_hardware(void)
{
#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
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

static bool detect_vpclmulqdq_hardware(void)
{
#ifdef XX_CRC64_HAVE_VPCLMUL
    /* The CRC backend uses AVX2 byte transforms and 128-bit PCLMUL for its
     * final reduction. detect_avx2_hardware also verifies OS XMM/YMM state. */
    if (!detect_avx2_hardware()) return false;
#if defined(_MSC_VER)
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 1);
    if ((cpu_info[2] & (1 << 1)) == 0) return false;
    __cpuidex(cpu_info, 7, 0);
    return (cpu_info[2] & (1 << 10)) != 0; /* Leaf 7, ECX bit 10 */
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    __cpuid_count(1, 0, eax, ebx, ecx, edx);
    if ((ecx & (1U << 1)) == 0) return false;
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    return (ecx & (1U << 10)) != 0;
#endif
#endif
    return false;
}

bool xx_global_platform_has_sse2(void)
{
    return detect_sse2_hardware();
}

bool xx_global_platform_has_sse42(void)
{
    return detect_sse42_hardware();
}

bool xx_global_platform_has_avx2(void)
{
    return detect_avx2_hardware();
}

bool xx_global_platform_has_aes(void)
{
    return detect_aes_hardware();
}

bool xx_global_platform_has_vpclmulqdq(void)
{
    return detect_vpclmulqdq_hardware();
}

bool xx_global_platform_has_sha(void)
{
    return detect_sha_hardware();
}

bool xx_global_platform_has_neon(void)
{
    return false;
}

bool xx_global_platform_has_arm_crc32(void)
{
    return false;
}

bool xx_global_platform_has_arm_crypto(void)
{
    return false;
}

bool xx_global_platform_has_arm_dotprod(void)
{
    return false;
}

bool xx_global_platform_has_arm_pmull(void)
{
    return false;
}

#else /* non-x86: legacy x86 features are false; Arm features are detected below */

#include "xx_global_platform.h"

#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(_WIN32) && !defined(_M_ARM64EC) && (defined(_M_ARM64) || defined(__aarch64__))
#include <windows.h>

enum {
    XX_PF_ARM_NEON_INSTRUCTIONS_AVAILABLE = 19,
    XX_PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE = 30,
    XX_PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE = 31,
    XX_PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE = 43
};

static bool xx_global_windows_arm_feature(unsigned int feature)
{
    return IsProcessorFeaturePresent(feature) != 0;
}
#endif

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/auxv.h>
#if defined(__has_include)
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#define XX_GLOBAL_HAVE_ARM_HWCAP 1
#endif
#endif
#endif

bool xx_global_platform_has_sse2(void)
{
    return false;
}

bool xx_global_platform_has_sse42(void)
{
    return false;
}

bool xx_global_platform_has_avx2(void)
{
    return false;
}

bool xx_global_platform_has_aes(void)
{
    return false;
}

bool xx_global_platform_has_vpclmulqdq(void)
{
    return false;
}

bool xx_global_platform_has_sha(void)
{
    return false;
}

bool xx_global_platform_has_neon(void)
{
#if defined(_M_ARM64EC)
    return false;
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return xx_global_windows_arm_feature(XX_PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);
#elif defined(__aarch64__)
    /* Advanced SIMD is part of the AArch64 architecture baseline. */
    return true;
#else
    return false;
#endif
}

bool xx_global_platform_has_arm_crc32(void)
{
#if defined(_M_ARM64EC)
    return false;
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return xx_global_windows_arm_feature(XX_PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE);
#elif defined(XX_GLOBAL_HAVE_ARM_HWCAP) && defined(HWCAP_CRC32)
    return (getauxval(AT_HWCAP) & HWCAP_CRC32) != 0;
#elif defined(__ARM_FEATURE_CRC32)
    return true;
#else
    return false;
#endif
}

bool xx_global_platform_has_arm_crypto(void)
{
#if defined(_M_ARM64EC)
    return false;
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return xx_global_windows_arm_feature(XX_PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
#elif defined(XX_GLOBAL_HAVE_ARM_HWCAP) && defined(HWCAP_AES) && defined(HWCAP_SHA1) && defined(HWCAP_SHA2)
    {
        unsigned long capabilities = getauxval(AT_HWCAP);
        return (capabilities & (HWCAP_AES | HWCAP_SHA1 | HWCAP_SHA2)) == (HWCAP_AES | HWCAP_SHA1 | HWCAP_SHA2);
    }
#elif defined(__ARM_FEATURE_AES) && defined(__ARM_FEATURE_SHA2)
    return true;
#else
    return false;
#endif
}

bool xx_global_platform_has_arm_dotprod(void)
{
#if defined(_M_ARM64EC)
    return false;
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return xx_global_windows_arm_feature(XX_PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE);
#elif defined(XX_GLOBAL_HAVE_ARM_HWCAP) && defined(HWCAP_ASIMDDP)
    return (getauxval(AT_HWCAP) & HWCAP_ASIMDDP) != 0;
#elif defined(__ARM_FEATURE_DOTPROD)
    return true;
#else
    return false;
#endif
}

bool xx_global_platform_has_arm_pmull(void)
{
#if defined(_M_ARM64EC)
    return false;
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return xx_global_windows_arm_feature(XX_PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
#elif defined(__APPLE__) && defined(__aarch64__)
    int supported = 0;
    size_t length = sizeof(supported);
    return sysctlbyname("hw.optional.arm.FEAT_PMULL", &supported, &length, NULL, 0) == 0 && supported != 0;
#elif defined(XX_GLOBAL_HAVE_ARM_HWCAP) && defined(HWCAP_PMULL)
    return (getauxval(AT_HWCAP) & HWCAP_PMULL) != 0;
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRYPTO)
    return true;
#else
    return false;
#endif
}

#endif
