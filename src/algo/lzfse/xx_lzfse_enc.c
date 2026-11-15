/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT */
#include "xxfclib/algo/lzfse/xx_lzfse.h"
#include "../xx_pack_memory_internal.h"
#include "xx_lzfse_reference.h"
size_t xx_lzfse_compress_bound(size_t size)
{
    return size>=INT32_MAX || size>SIZE_MAX-12U ? 0U : size+12U;
}
bool xx_lzfse_compress_memory(const void *source,size_t size,void *destination,size_t capacity,size_t *written)
{
    static const uint8_t empty=0;
    if(written)*written=0;
    if ((!source && size) || !destination || !xx_lzfse_compress_bound(size)) return false;
    size_t bound=xx_lzfse_compress_bound(size);if(capacity>bound)capacity=bound;
    void *scratch=xx_mem_alloc(lzfse_encode_scratch_size());if(!scratch)return false;
    size_t n=lzfse_encode_buffer(destination,capacity,source ? source : &empty,size,scratch);
    xx_mem_free(scratch);
    if(!n)return false;
    if(written)*written=n;return true;
}
bool xx_lzfse_pack_device(xx_io_device *source,int64_t offset,int64_t size,xx_io_device *destination,xx_pd_struct *pd)
{
    return xx_codec_pack_memory_device(source,offset,size,destination,pd,xx_lzfse_compress_bound,xx_lzfse_compress_memory);
}

/* The reference LZVN encoder (xx_lzvn_encode_base_reference.c); the names are
 * remapped to xx_ref_* by xx_lzfse_reference.h. */
size_t lzvn_encode_scratch_size(void);
size_t lzvn_encode_buffer(void *dst, size_t dst_size, const void *src, size_t src_size, void *work);
size_t xx_lzvn_compress_bound(size_t size)
{
    /* Worst case is n/7 expansion: a 4-byte literal run (5 bytes) followed by
     * a 3-byte match with a new far distance (3 bytes) per 7 input bytes.
     * 64 covers the encoder's emit slack and the 8-byte end-of-stream. */
    if (size >= INT32_MAX) return 0U;
    return size + size / 7U + 64U;
}
bool xx_lzvn_compress_memory(const void *source, size_t size, void *destination, size_t capacity, size_t *written)
{
    static const uint8_t empty = 0;
    size_t bound, n;
    void *scratch;
    if (written) *written = 0;
    if ((!source && size) || !destination || !(bound = xx_lzvn_compress_bound(size))) return false;
    (void)bound; /* the encoder never writes past capacity */
    scratch = xx_mem_alloc(lzvn_encode_scratch_size());
    if (!scratch) return false;
    n = lzvn_encode_buffer(destination, capacity, source ? source : &empty, size, scratch);
    xx_mem_free(scratch);
    if (!n) return false;
    if (written) *written = n;
    return true;
}
