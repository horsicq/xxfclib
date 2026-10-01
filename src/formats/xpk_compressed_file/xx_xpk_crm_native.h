/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient CRMDecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_CRM_NATIVE_H
#define XX_XPK_CRM_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

#define XPK_CRM_NODES 8193U
typedef struct xpk_crm_node {int32_t child[2],symbol;} xpk_crm_node;
typedef struct xpk_crm_tree {xpk_crm_node node[XPK_CRM_NODES];uint32_t count;} xpk_crm_tree;
typedef struct xpk_crm_bits {
    const uint8_t *data;
    size_t at;
    uint32_t word;
    unsigned left;
} xpk_crm_bits;
static uint16_t xpk_crm_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0]<<8U)|p[1]);
}
static uint32_t xpk_crm_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24U)|((uint32_t)p[1]<<16U)|((uint32_t)p[2]<<8U)|p[3];
}
static bool xpk_crm_read(xpk_crm_bits *bits,unsigned count,uint32_t *value) {
    uint32_t result=0U;
    unsigned i;
    if(count>24U)return false;
    for(i=0U;i<count;++i) {
        uint32_t bit;
        if(!bits->left) {
            if(bits->at<=14U)return false;
            bits->word=bits->data[--bits->at];bits->left=8U;
        }
        bit=bits->word&1U;bits->word>>=1U;--bits->left;
        result|=bit<<i;
    }
    *value=result;return true;
}
static void xpk_crm_tree_init(xpk_crm_tree *tree) {
    tree->count=1U;
    tree->node[0].child[0]=-1;tree->node[0].child[1]=-1;tree->node[0].symbol=-1;
}
static bool xpk_crm_insert(xpk_crm_tree *tree,unsigned width,uint32_t code,uint32_t symbol) {
    uint32_t at=0U;
    unsigned i;
    if(!width || width>15U || code>=(1U<<width) || symbol>511U)return false;
    for(i=0U;i<width;++i) {
        uint32_t bit=(code>>(width-i-1U))&1U;
        int32_t next;
        if(tree->node[at].symbol>=0)return false;
        next=tree->node[at].child[bit];
        if(next<0) {
            if(tree->count>=XPK_CRM_NODES)return false;
            next=(int32_t)tree->count++;
            tree->node[at].child[bit]=next;
            tree->node[next].child[0]=-1;tree->node[next].child[1]=-1;
            tree->node[next].symbol=-1;
        }
        at=(uint32_t)next;
    }
    if(tree->node[at].symbol>=0 || tree->node[at].child[0]>=0 ||
       tree->node[at].child[1]>=0)return false;
    tree->node[at].symbol=(int32_t)symbol;return true;
}
static bool xpk_crm_table(xpk_crm_tree *tree,xpk_crm_bits *bits,unsigned symbol_bits) {
    uint32_t max_depth,counts[15],code=0U;
    unsigned depth,i;
    xpk_crm_tree_init(tree);
    if(!xpk_crm_read(bits,4U,&max_depth) || !max_depth)return false;
    for(depth=1U;depth<=max_depth;++depth) {
        unsigned width=depth<symbol_bits?depth:symbol_bits;
        if(!xpk_crm_read(bits,width,&counts[depth-1U]))return false;
    }
    for(depth=1U;depth<=max_depth;++depth) {
        uint32_t step=1U<<(max_depth-depth);
        for(i=0U;i<counts[depth-1U];++i) {
            uint32_t symbol;
            if(code>=(1U<<max_depth) ||
               !xpk_crm_read(bits,symbol_bits,&symbol) ||
               !xpk_crm_insert(tree,depth,code>>(max_depth-depth),symbol))return false;
            code+=step;
        }
    }
    return tree->count>1U;
}
static bool xpk_crm_symbol(const xpk_crm_tree *tree,xpk_crm_bits *bits,uint32_t *value) {
    uint32_t at=0U;
    unsigned depth=0U;
    while(tree->node[at].symbol<0) {
        uint32_t bit;
        int32_t next;
        if(depth++>=15U || !xpk_crm_read(bits,1U,&bit))return false;
        next=tree->node[at].child[bit];
        if(next<0)return false;
        at=(uint32_t)next;
    }
    *value=(uint32_t)tree->node[at].symbol;return true;
}
/* Version 24=CRM2; 25=CRMS. The internal CrM2/Crm2 header selects sample delta. */
static bool xpk_crm_native(const uint8_t *packed,size_t size,uint8_t *output,
                           size_t wanted,xx_pd_struct *pd) {
    xpk_crm_tree length_tree,distance_tree;
    xpk_crm_bits bits;
    uint32_t raw_size,packed_size,shift;
    size_t pos=wanted,at,rounds=0U;
    uint8_t accumulated=0U;
    bool sampled;
    if(!packed || !output || !wanted || size<20U || xx_pd_is_stopped(pd))return false;
    if(packed[0]!='C' || packed[1]!='r' ||
       (packed[2]!='M' && packed[2]!='m') || packed[3]!='2')return false;
    sampled=packed[2]=='m';
    raw_size=xpk_crm_be32(packed+6U);packed_size=xpk_crm_be32(packed+10U);
    if(raw_size!=wanted || packed_size<6U || packed_size>size-14U)return false;
    at=(size_t)packed_size+8U;
    shift=xpk_crm_be16(packed+at+4U);
    if(shift>16U)return false;
    bits.data=packed;bits.at=at;
    bits.word=xpk_crm_be32(packed+at)>>(16U-shift);
    bits.left=shift+16U;
    for(;;) {
        uint32_t items,index,again;
        if(++rounds>wanted+1U || xx_pd_is_stopped(pd) ||
           !xpk_crm_table(&length_tree,&bits,9U) ||
           !xpk_crm_table(&distance_tree,&bits,4U) ||
           !xpk_crm_read(&bits,16U,&items))return false;
        ++items;
        for(index=0U;index<items;++index) {
            uint32_t count,distance_bits,distance,i;
            if(xx_pd_is_stopped(pd) ||
               !xpk_crm_symbol(&length_tree,&bits,&count))return false;
            if(count&0x100U) {
                if(!pos)return false;
                output[--pos]=(uint8_t)count;
                continue;
            }
            count+=3U;
            if(count>pos || !xpk_crm_symbol(&distance_tree,&bits,&distance_bits) ||
               distance_bits>15U)return false;
            if(!distance_bits) {
                if(!xpk_crm_read(&bits,1U,&distance))return false;
                ++distance;
            } else {
                if(!xpk_crm_read(&bits,distance_bits,&distance))return false;
                distance=(distance|(1U<<distance_bits))+1U;
            }
            if(!distance || distance>wanted-pos)return false;
            for(i=0U;i<count;++i) {
                if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
                --pos;output[pos]=output[pos+distance];
            }
        }
        if(!xpk_crm_read(&bits,1U,&again))return false;
        if(!again)break;
    }
    if(pos || xx_pd_is_stopped(pd))return false;
    if(sampled) {
        for(at=0U;at<wanted;++at) {
            if((at&1023U)==0U && xx_pd_is_stopped(pd))return false;
            accumulated=(uint8_t)(accumulated+output[at]);
            output[at]=accumulated;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
