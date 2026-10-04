/* Independent DGCA range mathematics. SPDX-License-Identifier: MIT. */
#ifndef DGCA_RANGE_H
#define DGCA_RANGE_H
#include <stddef.h>
#include <stdint.h>
typedef struct dg_range {
    const unsigned char *data;
    size_t size, position;
    uint32_t code, width;
    unsigned char previous;
} dg_range;
int dg_range_init(dg_range *,const unsigned char *,size_t);
int dg_range_bit(dg_range *,uint32_t probability_zero,unsigned *bit);
int dg_range_uniform(dg_range *,unsigned width,uint32_t *value);
int dg_range_integer(dg_range *,uint32_t probabilities[32],unsigned shift,uint32_t *value);
#endif
