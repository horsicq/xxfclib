/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_entropy_log2.c
 * @brief YARA-X-form Shannon entropy: -sum p*log2(p) in byte order.
 *
 * The ILP kernels in xx_entropy.c optimize a mathematically equivalent
 * (ln N - sum c*ln c / N)/ln 2 form whose floating-point result can differ
 * in the last bits from YARA-X's math::entropy, which sums -p*log2(p) over
 * the 256 counts in byte order. Consumers that must match YARA-X bit for
 * bit use the functions implemented here instead.
 */

#include "xxfclib/algo/entropy/xx_entropy.h"
#include "xxfclib/algo/bytestats/xx_bytestats.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

double xx_entropy_log2_counts(const uint64_t counts[256], uint64_t total)
{
    double entropy = 0.0;
    if (!counts || !total) return 0.0;
    for (size_t i = 0; i < 256; ++i) {
        if (counts[i] != 0) {
            double probability = (double)counts[i] / (double)total;
            entropy -= probability * log2(probability);
        }
    }
    return entropy;
}

double xx_entropy_log2(const void *data, size_t size)
{
    uint64_t counts[256];
    if (size && !data) return 0.0;
    xx_bytestats_histogram(data, size, counts);
    return xx_entropy_log2_counts(counts, (uint64_t)size);
}
