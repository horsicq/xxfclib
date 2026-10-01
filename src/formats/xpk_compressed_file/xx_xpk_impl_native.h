/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient IMPDecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_IMPL_NATIVE_H
#define XX_XPK_IMPL_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

typedef struct xpk_impl_bits {
    const uint8_t *data;
    size_t size,at,reference;
    uint8_t word;
    unsigned left;
} xpk_impl_bits;
static uint16_t xpk_impl_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0]<<8U)|p[1]);
}
static uint32_t xpk_impl_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24U)|((uint32_t)p[1]<<16U)|((uint32_t)p[2]<<8U)|p[3];
}
static bool xpk_impl_byte(xpk_impl_bits *bits,uint8_t *value) {
    size_t index;
    if(!bits->at)return false;
    index=--bits->at;
    if(index<12U) {
        if(index<4U)index+=bits->reference+8U;
        else if(index<8U)index+=bits->reference;
        else index+=bits->reference-8U;
    }
    if(index>=bits->size)return false;
    *value=bits->data[index];return true;
}
static bool xpk_impl_read(xpk_impl_bits *bits,unsigned count,uint32_t *value) {
    uint32_t result=0U;
    unsigned i;
    if(count>32U)return false;
    for(i=0U;i<count;++i) {
        if(!bits->left) {
            if(!xpk_impl_byte(bits,&bits->word))return false;
            bits->left=8U;
        }
        --bits->left;
        result=(result<<1U)|((bits->word>>bits->left)&1U);
    }
    *value=result;return true;
}
static bool xpk_impl_lld(xpk_impl_bits *bits,uint32_t *symbol) {
    unsigned i;
    uint32_t bit;
    for(i=0U;i<5U;++i) {
        if(!xpk_impl_read(bits,1U,&bit))return false;
        if(!bit) {*symbol=i;return true;}
    }
    *symbol=5U;return true;
}
static bool xpk_impl_lld2(xpk_impl_bits *bits,uint32_t *symbol) {
    uint32_t bit;
    if(!xpk_impl_read(bits,1U,&bit))return false;
    if(!bit) {*symbol=0U;return true;}
    if(!xpk_impl_read(bits,1U,&bit))return false;
    *symbol=bit?2U:1U;return true;
}
/* Version 27=IMPL. The caller supplies the exact XPK chunk raw size. */
static bool xpk_impl_native(const uint8_t *packed,size_t size,uint8_t *output,
                            size_t wanted,xx_pd_struct *pd) {
    static const uint8_t literal_lengths[4]={6U,10U,10U,18U};
    static const uint8_t literal_bits[3][4]={
        {1U,1U,1U,1U}, {2U,3U,3U,4U}, {4U,5U,7U,14U}};
    uint16_t distance_values[2][4];
    uint8_t distance_bits[3][4];
    xpk_impl_bits bits;
    uint32_t raw_size,end_offset,lit_length;
    size_t pos=wanted;
    unsigned i;
    if(!packed || !output || !wanted || size<0x2eU || xx_pd_is_stopped(pd))return false;
    raw_size=xpk_impl_be32(packed+4U);end_offset=xpk_impl_be32(packed+8U);
    if(raw_size!=wanted || (end_offset&1U) || end_offset<12U ||
       end_offset>size-0x2eU)return false;
    bits.data=packed;bits.size=size;bits.at=end_offset;bits.reference=end_offset;
    bits.word=0U;bits.left=0U;
    if(!(packed[end_offset+16U]&0x80U))--bits.at;
    for(i=0U;i<7U;++i) {
        uint8_t half=packed[end_offset+17U];
        if(half&(1U<<i)) {
            bits.word=(uint8_t)(half>>(i+1U));bits.left=7U-i;
            break;
        }
    }
    for(i=0U;i<8U;++i)
        distance_values[i>>2U][i&3U]=xpk_impl_be16(packed+end_offset+18U+i*2U);
    for(i=0U;i<12U;++i)
        distance_bits[i>>2U][i&3U]=packed[end_offset+34U+i];
    lit_length=xpk_impl_be32(packed+end_offset+12U);
    for(;;) {
        uint32_t i0,i1,i2,selector,count,more;
        uint64_t distance;
        if(xx_pd_is_stopped(pd) || lit_length>pos)return false;
        for(i=0U;i<lit_length;++i) {
            uint8_t value;
            if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
            if(!xpk_impl_byte(&bits,&value))return false;
            output[--pos]=value;
        }
        if(!pos)break;
        if(!xpk_impl_lld(&bits,&i0))return false;
        selector=i0<4U?i0:3U;
        count=i0+2U;
        if(count==6U) {
            if(!xpk_impl_read(&bits,3U,&more))return false;
            count+=more;
        } else if(count==7U) {
            uint8_t tmp;
            if(!xpk_impl_byte(&bits,&tmp) || !tmp)return false;
            count=tmp;
        }
        if(!xpk_impl_lld2(&bits,&i1))return false;
        lit_length=i1+i1;
        if(lit_length==4U)lit_length=literal_lengths[selector];
        if(!xpk_impl_read(&bits,literal_bits[i1][selector],&more))return false;
        lit_length+=more;
        if(!xpk_impl_lld2(&bits,&i2))return false;
        if(distance_bits[i2][selector]>32U ||
           !xpk_impl_read(&bits,distance_bits[i2][selector],&more))return false;
        distance=1ULL+(i2?distance_values[i2-1U][selector]:0U)+more;
        if(count>pos || !distance || distance>wanted-pos)return false;
        for(i=0U;i<count;++i) {
            if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
            --pos;output[pos]=output[pos+distance];
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
