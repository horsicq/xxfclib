/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient SXSCDecompressor.cpp's SASC method
 * and RangeDecoder.cpp, commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834.
 * See LICENSE.ancient. No SHSC decoding is claimed here.
 */
#ifndef XX_XPK_SASC_NATIVE_H
#define XX_XPK_SASC_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct xpk_sasc_coder {
    const uint8_t *packed;
    size_t size,at;
    unsigned bits,slack;
    uint8_t current;
    uint16_t low,high,stream;
    xx_pd_struct *pd;
} xpk_sasc_coder;
typedef struct xpk_sasc_table {
    uint16_t *values;
    unsigned length;
} xpk_sasc_table;
static bool xpk_sasc_byte(xpk_sasc_coder *c,uint8_t *value) {
    if(c->at<c->size) {*value=c->packed[c->at++];return true;}
    if(c->slack>=3U)return false;
    ++c->slack;*value=0U;return true;
}
static bool xpk_sasc_bit(xpk_sasc_coder *c,uint32_t *value) {
    if(!c->bits) {
        if(!xpk_sasc_byte(c,&c->current))return false;
        c->bits=8U;
    }
    --c->bits;
    *value=(c->current>>c->bits)&1U;
    return true;
}
static bool xpk_sasc_decode(xpk_sasc_coder *c,uint16_t length,uint16_t *value) {
    uint32_t span,result;
    if(!length || c->high<c->low)return false;
    span=(uint32_t)c->high-c->low+1U;
    result=(((uint32_t)(uint16_t)(c->stream-c->low)+1U)*length-1U)/span;
    if(result>=length)return false;
    *value=(uint16_t)result;return true;
}
static bool xpk_sasc_scale(xpk_sasc_coder *c,uint16_t first,
                           uint16_t last,uint16_t total) {
    uint32_t span;
    if(!total || first>=last || last>total || c->high<c->low)return false;
    span=(uint32_t)c->high-c->low+1U;
    c->high=(uint16_t)((span*last)/total+c->low-1U);
    c->low=(uint16_t)((span*first)/total+c->low);
    if(c->high<c->low)return false;
    for(;;) {
        uint16_t decr;
        uint32_t bit;
        if(c->high<0x8000U)decr=0U;
        else if(c->low>=0x8000U)decr=0x8000U;
        else if(c->low>=0x4000U && c->high<0xc000U)decr=0x4000U;
        else break;
        c->low=(uint16_t)((c->low-decr)<<1U);
        c->high=(uint16_t)(((c->high-decr)<<1U)|1U);
        if(!xpk_sasc_bit(c,&bit))return false;
        c->stream=(uint16_t)(((c->stream-decr)<<1U)|bit);
    }
    return !xx_pd_is_stopped(c->pd);
}
static void xpk_sasc_init_table(xpk_sasc_table *table,uint16_t initial) {
    unsigned n=table->length,i,j;
    for(i=0U;i<n;++i)table->values[i]=initial;
    for(i=n,j=0U;i<2U*n-1U;++i,j+=2U)
        table->values[i]=(uint16_t)(table->values[j]+table->values[j+1U]);
}
static uint16_t xpk_sasc_total(const xpk_sasc_table *table) {
    return table->values[2U*table->length-2U];
}
static void xpk_sasc_update(xpk_sasc_table *table,uint16_t limit,
                            unsigned index,int16_t change) {
    unsigned n=table->length,i,j;
    if(index>=n)return;
    for(i=index;i<2U*n-1U;i=(i>>1U)+n)
        table->values[i]=(uint16_t)(table->values[i]+change);
    if(xpk_sasc_total(table)>=limit) {
        for(i=0U;i<n;++i)
            if(table->values[i]>1U)table->values[i]>>=1U;
        for(i=n,j=0U;i<2U*n-1U;++i,j+=2U)
            table->values[i]=(uint16_t)(table->values[j]+table->values[j+1U]);
    }
}
static bool xpk_sasc_symbol(const xpk_sasc_table *table,uint16_t *value,
                            uint16_t *symbol) {
    unsigned n=table->length,i=2U*n-4U;
    uint32_t threshold=0U;
    if(!n || *value>=xpk_sasc_total(table))return false;
    while(i>=n) {
        unsigned child=(i-n)<<1U;
        if((uint32_t)*value-threshold>=table->values[i]) {
            threshold+=table->values[i];child+=2U;
        }
        i=child;
    }
    if((uint32_t)*value-threshold>=table->values[i]) {
        threshold+=table->values[i];++i;
    }
    if(i>=n || !table->values[i])return false;
    *value=(uint16_t)threshold;*symbol=(uint16_t)i;return true;
}
static bool xpk_sasc_two_step(xpk_sasc_coder *c,xpk_sasc_table *initial,
                              xpk_sasc_table *dynamic,uint16_t *threshold,
                              uint16_t max,uint16_t step,uint16_t vicinity,
                              uint16_t *result) {
    uint16_t value,symbol,total=xpk_sasc_total(dynamic),initial_total;
    unsigned i,end;
    if((uint32_t)total+*threshold>UINT16_MAX ||
       !xpk_sasc_decode(c,(uint16_t)(total+*threshold),&value))return false;
    if(value<total) {
        if(!xpk_sasc_symbol(dynamic,&value,&symbol) ||
           !xpk_sasc_scale(c,value,(uint16_t)(value+dynamic->values[symbol]),
                           (uint16_t)(total+*threshold)))return false;
    } else {
        if(!xpk_sasc_scale(c,total,(uint16_t)(total+*threshold),
                           (uint16_t)(total+*threshold)))return false;
        initial_total=xpk_sasc_total(initial);
        if(!xpk_sasc_decode(c,initial_total,&value) ||
           !xpk_sasc_symbol(initial,&value,&symbol) ||
           !xpk_sasc_scale(c,value,(uint16_t)(value+initial->values[symbol]),
                           initial_total))return false;
        xpk_sasc_update(initial,65535U,symbol,(int16_t)-initial->values[symbol]);
        if(xpk_sasc_total(initial)) {
            if((uint32_t)*threshold+step>UINT16_MAX)return false;
            *threshold=(uint16_t)(*threshold+step);
        } else *threshold=0U;
        end=(unsigned)symbol+vicinity;
        if(end>=initial->length)end=initial->length-1U;
        for(i=symbol>vicinity?(unsigned)(symbol-vicinity):0U;i<end;++i)
            if(initial->values[i])xpk_sasc_update(initial,max,i,1);
    }
    xpk_sasc_update(dynamic,max,symbol,(int16_t)step);
    if(dynamic->values[symbol]==step*3U)
        *threshold=*threshold>step?(uint16_t)(*threshold-step):1U;
    *result=symbol;return true;
}
/* SASC payload: 1-byte delta mode, 2-byte arithmetic seed, then MSB bits.
 * Exact output length and arithmetic end marker are required. Three virtual
 * zero bytes match Ancient's bounded final arithmetic-code padding.
 */
static bool xpk_sasc_native(const uint8_t *packed,size_t size,uint8_t *output,
                            size_t wanted,xx_pd_struct *pd) {
    xpk_sasc_coder c;
    uint16_t lit_init_values[511],lit_dyn_values[511],dist_values[31];
    uint16_t count_init_values[127],count_dyn_values[127];
    xpk_sasc_table li={lit_init_values,256U},ld={lit_dyn_values,256U};
    xpk_sasc_table dt={dist_values,16U},ci={count_init_values,64U};
    xpk_sasc_table cd={count_dyn_values,64U};
    uint16_t bt1[4]={40U,40U,40U,40U},bt2[4]={40U,40U,40U,40U};
    uint16_t lit_threshold=1U,count_threshold=8U;
    uint32_t bitpos=0U,distance_index=0U;
    size_t produced=0U,i;
    uint8_t mode,*scratch=output;
    bool ok=false;
    if(!packed || !output || size<3U || !wanted || xx_pd_is_stopped(pd))return false;
    mode=packed[0];if(mode>3U)return false;
    if(mode>=2U) {
        scratch=(uint8_t *)malloc(wanted);
        if(!scratch)return false;
    }
    memset(&c,0,sizeof(c));
    c.packed=packed;c.size=size;c.at=3U;c.slack=0U;c.pd=pd;
    c.low=0U;c.high=UINT16_MAX;
    c.stream=(uint16_t)(((uint16_t)packed[1]<<8U)|packed[2]);
    xpk_sasc_init_table(&li,1U);xpk_sasc_init_table(&ld,0U);
    xpk_sasc_init_table(&dt,0U);
    xpk_sasc_init_table(&ci,1U);xpk_sasc_init_table(&cd,0U);
    xpk_sasc_update(&dt,6000U,0U,24);
    for(;;) {
        uint16_t bit_size,bit_value;
        bool literal;
        if(xx_pd_is_stopped(pd))goto done;
        bit_size=(uint16_t)(bt1[bitpos]+bt2[bitpos]);
        if(!xpk_sasc_decode(&c,(uint16_t)(bit_size+1U),&bit_value))goto done;
        if(bit_value==bit_size) {ok=produced==wanted;break;}
        literal=bit_value<bt1[bitpos];
        if(!xpk_sasc_scale(&c,literal?0U:bt1[bitpos],
                           literal?bt1[bitpos]:bit_size,
                           (uint16_t)(bit_size+1U)))goto done;
        if(literal)bt1[bitpos]=(uint16_t)(bt1[bitpos]+40U);
        else bt2[bitpos]=(uint16_t)(bt2[bitpos]+40U);
        if(bit_size>=6000U) {
            if(!(bt1[bitpos]>>=1U))bt1[bitpos]=1U;
            if(!(bt2[bitpos]>>=1U))bt2[bitpos]=1U;
        }
        bitpos=((bitpos<<1U)&2U)|(literal?0U:1U);
        if(literal) {
            uint16_t symbol;
            if(produced>=wanted || !xpk_sasc_two_step(&c,&li,&ld,
                 &lit_threshold,1000U,1U,8U,&symbol))goto done;
            scratch[produced++]=(uint8_t)symbol;
        } else {
            uint16_t value,bits,count_symbol;
            uint32_t distance,count;
            while(produced>((size_t)1U<<distance_index) && distance_index<15U)
                xpk_sasc_update(&dt,6000U,++distance_index,24);
            if(!xpk_sasc_decode(&c,xpk_sasc_total(&dt),&value) ||
               !xpk_sasc_symbol(&dt,&value,&bits) ||
               !xpk_sasc_scale(&c,value,(uint16_t)(value+dt.values[bits]),
                               xpk_sasc_total(&dt)))goto done;
            xpk_sasc_update(&dt,6000U,bits,24);
            distance=bits;
            if(bits>=2U) {
                uint16_t min_range=(uint16_t)(1U<<(bits-1U));
                uint16_t range=distance_index==bits?
                  (uint16_t)((produced<31200U?produced:31200U)-min_range):min_range;
                if(!xpk_sasc_decode(&c,range,&value) ||
                   !xpk_sasc_scale(&c,value,(uint16_t)(value+1U),range))goto done;
                distance=value+min_range;
            }
            ++distance;
            if(!xpk_sasc_two_step(&c,&ci,&cd,&count_threshold,6000U,8U,4U,
                                  &count_symbol))goto done;
            count=count_symbol;
            if(count==15U)count=783U;
            else if(count>=16U) {
                if(!xpk_sasc_decode(&c,16U,&value) ||
                   !xpk_sasc_scale(&c,value,(uint16_t)(value+1U),16U))goto done;
                count=((count-16U)<<4U)+value+15U;
            }
            count+=3U;
            if(!distance || distance>produced || count>wanted-produced)goto done;
            for(i=0U;i<count;++i) {
                if((i&1023U)==0U && xx_pd_is_stopped(pd))goto done;
                scratch[produced+i]=scratch[produced+i-distance];
            }
            produced+=count;
        }
    }
    if(ok) {
        if(mode==1U) {
            uint8_t accumulator=0U;
            for(i=0U;i<wanted;++i) {
                if((i&1023U)==0U && xx_pd_is_stopped(pd)) {ok=false;goto done;}
                accumulator=(uint8_t)(accumulator+output[i]);
                output[i]=accumulator;
            }
        } else if(mode==2U || mode==3U) {
            uint8_t accumulator=0U;
            for(i=0U;i<wanted/2U;++i) {
                if((i&1023U)==0U && xx_pd_is_stopped(pd)) {ok=false;goto done;}
                accumulator=(uint8_t)(accumulator+scratch[i]);
                if(mode==2U) {
                    output[2U*i]=accumulator;
                    output[2U*i+1U]=scratch[wanted/2U+i];
                } else {
                    output[2U*i]=scratch[wanted/2U+i];
                    output[2U*i+1U]=accumulator;
                }
            }
            if(wanted&1U)output[wanted-1U]=scratch[wanted-1U];
        }
    }
done:
    if(scratch!=output)free(scratch);
    return ok && !xx_pd_is_stopped(pd);
}
#endif
