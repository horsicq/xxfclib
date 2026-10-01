/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient SDHCDecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_SDHC_NATIVE_H
#define XX_XPK_SDHC_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef bool (*xpk_sdhc_child_decoder)(const uint8_t *packed,size_t size,
                                        uint8_t *output,size_t wanted,
                                        void *opaque,xx_pd_struct *pd);
static bool xpk_sdhc_byte_delta(uint8_t *data,size_t size,xx_pd_struct *pd) {
    uint8_t accumulator=0U;
    size_t i;
    for(i=0U;i<size;++i) {
        if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
        accumulator=(uint8_t)(accumulator+data[i]);data[i]=accumulator;
    }
    return true;
}
static bool xpk_sdhc_mono_delta(uint8_t *data,size_t size,xx_pd_struct *pd) {
    uint16_t accumulator=0U;
    size_t i;
    for(i=0U;i<size;i+=2U) {
        uint16_t value;
        if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
        value=(uint16_t)(((uint16_t)data[i]<<8U)|data[i+1U]);
        accumulator=(uint16_t)(accumulator+value);
        data[i]=(uint8_t)(accumulator>>8U);data[i+1U]=(uint8_t)accumulator;
    }
    return true;
}
static bool xpk_sdhc_stereo_delta(uint8_t *data,size_t size,xx_pd_struct *pd) {
    uint16_t left=0U,right=0U;
    size_t i;
    for(i=0U;i<size;i+=4U) {
        uint16_t value;
        if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
        value=(uint16_t)(((uint16_t)data[i]<<8U)|data[i+1U]);
        left=(uint16_t)(left+value);
        value=(uint16_t)(((uint16_t)data[i+2U]<<8U)|data[i+3U]);
        right=(uint16_t)(right+value);
        data[i]=(uint8_t)(left>>8U);data[i+1U]=(uint8_t)left;
        data[i+2U]=(uint8_t)(right>>8U);data[i+3U]=(uint8_t)right;
    }
    return true;
}
/* Version 31=SDHC. Nested mode passes a full XPKF child to a bounded native
 * XPK decoder supplied by the outer dispatcher. A cycle/depth guard belongs
 * to that dispatcher. The caller's child function must fill exactly wanted.
 */
static bool xpk_sdhc_native(const uint8_t *packed,size_t size,uint8_t *output,
                            size_t wanted,xpk_sdhc_child_decoder child,
                            void *opaque,xx_pd_struct *pd) {
    unsigned mode;
    size_t length;
    if(!packed || !output || size<2U || xx_pd_is_stopped(pd))return false;
    mode=((unsigned)packed[0]<<8U)|packed[1];
    if(mode&0x8000U) {
        if(!child || size<6U || packed[2]!='X' || packed[3]!='P' ||
           packed[4]!='K' || packed[5]!='F' ||
           !child(packed+2U,size-2U,output,wanted,opaque,pd))return false;
    } else {
        if(size-2U!=wanted)return false;
        memcpy(output,packed+2U,wanted);
    }
    length=wanted&~(size_t)3U;
    switch(mode&15U) {
    case 1U:
        if(!xpk_sdhc_byte_delta(output,length,pd))return false;
        /* fall through */
    case 0U:
        if(!xpk_sdhc_byte_delta(output,length,pd))return false;
        break;
    case 3U:
        if(!xpk_sdhc_mono_delta(output,length,pd))return false;
        /* fall through */
    case 2U:
        if(!xpk_sdhc_mono_delta(output,length,pd))return false;
        break;
    case 11U:
        if(!xpk_sdhc_stereo_delta(output,length,pd))return false;
        /* fall through */
    case 10U:
        if(!xpk_sdhc_stereo_delta(output,length,pd))return false;
        break;
    default:return false;
    }
    return !xx_pd_is_stopped(pd);
}
#endif
