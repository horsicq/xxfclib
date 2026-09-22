/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_global_platform.h
 * @brief Internal platform interface for CPU feature detection.
 *
 * The axis here is instruction set and compiler, not operating system: MSVC
 * and GCC disagree about how to spell CPUID even when both target Windows on
 * x86-64. A windows/posix pair cannot express that, so the split is
 * xx_global_x86.c against xx_global_generic.c, selected by predicate the way
 * src/data/platforms/xx_data_sse2.c is.
 */

#ifndef XX_GLOBAL_PLATFORM_H
#define XX_GLOBAL_PLATFORM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief True when the running processor supports SSE2. */
bool xx_global_platform_has_sse2(void);

/** @brief True when the running processor supports AVX2. */
bool xx_global_platform_has_avx2(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_GLOBAL_PLATFORM_H */
