/* Independent standard transforms. SPDX-License-Identifier: MIT. */
#ifndef DGCA_TRANSFORM_H
#define DGCA_TRANSFORM_H
#include <stddef.h>
#include <stdint.h>
#ifdef DG_TRANSFORM_SHARED
#define DG_TRANSFORM_API __declspec(dllexport)
#else
#define DG_TRANSFORM_API
#endif
/* Return 1 on success, 0 on invalid input, -1 on cancellation. Buffers must
 * not overlap. BWT workspace has at least count uint32_t entries. */
DG_TRANSFORM_API int dg_inverse_bwt(const unsigned char *, unsigned char *, size_t,
                                 uint32_t, uint32_t *, int (*)(void *), void *);
/* Concatenated planes contain original bytes at i, i+stride, i+2*stride...
 * Plane lengths differ by at most one; count need not be divisible by stride. */
DG_TRANSFORM_API int dg_deinterleave(const unsigned char *, unsigned char *, size_t,
                                  uint32_t, int (*)(void *), void *);
#endif
