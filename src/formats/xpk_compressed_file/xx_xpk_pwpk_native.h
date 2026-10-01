/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient PPDecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_PWPK_NATIVE_H
#define XX_XPK_PWPK_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

typedef struct xpk_pwpk_bits {
    const uint8_t *data;
    size_t at;
    uint32_t word;
    unsigned left;
} xpk_pwpk_bits;
static bool xpk_pwpk_read(xpk_pwpk_bits *bits,unsigned count,uint32_t *value) {
    uint32_t result=0U;
    unsigned i;
    if(count>32U)return false;
    for(i=0U;i<count;++i) {
        uint32_t bit;
        if(!bits->left) {
            if(bits->at<4U)return false;
            bits->word=((uint32_t)bits->data[bits->at-4U]<<24U)|
                       ((uint32_t)bits->data[bits->at-3U]<<16U)|
                       ((uint32_t)bits->data[bits->at-2U]<<8U)|
                       bits->data[bits->at-1U];
            bits->at-=4U;bits->left=32U;
        }
        bit=bits->word&1U;bits->word>>=1U;--bits->left;
        result=(result<<1U)|bit;
    }
    *value=result;return true;
}
static uint32_t xpk_pwpk_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24U)|((uint32_t)p[1]<<16U)|((uint32_t)p[2]<<8U)|p[3];
}
/* Version 22=PWPK. Initialize *mode to UINT32_MAX before the first chunk.
 * The first compressed chunk carries the mode after its size footer; later
 * chunks reuse the cached mode. Only commit mode on complete success.
 */
static bool xpk_pwpk_native(const uint8_t *packed,size_t size,uint8_t *output,
                            size_t wanted,uint32_t *mode,xx_pd_struct *pd) {
    static const uint8_t table[5][4]={
        {9U,9U,9U,9U}, {9U,10U,10U,10U}, {9U,10U,11U,11U},
        {9U,10U,12U,12U}, {9U,10U,12U,13U}};
    xpk_pwpk_bits bits;
    uint32_t candidate,footer,discard;
    size_t pos,limit;
    if(!packed || !mode || !output || !wanted || size<4U || xx_pd_is_stopped(pd))return false;
    candidate=*mode;
    limit=size-4U;
    if(candidate==UINT32_MAX) {
        if(size<8U)return false;
        candidate=xpk_pwpk_be32(packed+limit);
        if(candidate>4U)return false;
        limit-=4U;
    } else if(candidate>4U)return false;
    footer=xpk_pwpk_be32(packed+limit);
    if((footer>>8U)!=wanted || (footer&255U)>=32U)return false;
    bits.data=packed;bits.at=limit;bits.word=0U;bits.left=0U;
    if(!xpk_pwpk_read(&bits,footer&255U,&discard))return false;
    pos=wanted;
    while(pos) {
        uint32_t flag,mode_index,count,distance,tmp;
        size_t i;
        if(xx_pd_is_stopped(pd) || !xpk_pwpk_read(&bits,1U,&flag))return false;
        if(!flag) {
            count=1U;
            do {
                if(!xpk_pwpk_read(&bits,2U,&tmp))return false;
                if(count>pos || tmp>pos-count)return false;
                count+=tmp;
            } while(tmp==3U);
            if(count>pos)return false;
            for(i=0U;i<count;++i) {
                if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
                if(!xpk_pwpk_read(&bits,8U,&tmp))return false;
                output[--pos]=(uint8_t)tmp;
            }
        }
        if(!pos)break;
        if(!xpk_pwpk_read(&bits,2U,&mode_index))return false;
        if(mode_index==3U) {
            if(!xpk_pwpk_read(&bits,1U,&tmp))return false;
            if(!xpk_pwpk_read(&bits,tmp?table[candidate][3]:7U,&distance))return false;
            ++distance;count=5U;
            do {
                if(!xpk_pwpk_read(&bits,3U,&tmp))return false;
                if(count>pos || tmp>pos-count)return false;
                count+=tmp;
            } while(tmp==7U);
        } else {
            count=mode_index+2U;
            if(!xpk_pwpk_read(&bits,table[candidate][mode_index],&distance))return false;
            ++distance;
        }
        if(count>pos || distance>wanted-pos)return false;
        for(i=0U;i<count;++i) {
            if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
            --pos;output[pos]=output[pos+distance];
        }
    }
    if(xx_pd_is_stopped(pd))return false;
    *mode=candidate;return true;
}
#endif
