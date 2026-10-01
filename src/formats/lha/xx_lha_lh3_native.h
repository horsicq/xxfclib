/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * Native LHarc -lh3- block-static Huffman decoder. The wire fields follow
 * the original LHa shuf.c specification and are implemented independently.
 */
#ifndef XX_LHA_LH3_NATIVE_H
#define XX_LHA_LH3_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct xx_lha_lh3_bits_s {
    const uint8_t *data;
    size_t size, position;
} xx_lha_lh3_bits;

typedef struct xx_lha_lh3_tree_s {
    uint16_t symbols[286];
    unsigned count[17], first[17], offset[17];
    unsigned max_length, single_symbol;
    bool single;
} xx_lha_lh3_tree;

static bool xx_lha_lh3_get(xx_lha_lh3_bits *bits,unsigned n,unsigned *value) {
    unsigned result=0U,i;
    if (!bits || !value || n>16U || bits->size>SIZE_MAX/8U ||
        bits->position>bits->size*8U ||
        n>bits->size*8U-bits->position) return false;
    for(i=0U;i<n;++i){
        size_t p=bits->position++;
        result=(result<<1U)|
               ((bits->data[p/8U]>>(7U-(unsigned)(p&7U)))&1U);
    }
    *value=result;return true;
}

static bool xx_lha_lh3_tree_build(xx_lha_lh3_tree *tree,
                                   const uint8_t *lengths,unsigned alphabet) {
    unsigned symbol,length,code=0U,used=0U,max=0U;
    if (!tree || !lengths || alphabet>286U) return false;
    memset(tree,0,sizeof(*tree));
    for(symbol=0U;symbol<alphabet;++symbol){
        length=lengths[symbol];
        if(length>16U)return false;
        if(length){++tree->count[length];if(length>max)max=length;}
    }
    if(!max)return false;
    tree->max_length=max;
    for(length=1U;length<=max;++length){
        code=(code+tree->count[length-1U])<<1U;
        if(code+tree->count[length]>(1U<<length))return false;
        tree->first[length]=code;
        tree->offset[length]=used;
        for(symbol=0U;symbol<alphabet;++symbol)
            if(lengths[symbol]==length)tree->symbols[used++]=(uint16_t)symbol;
    }
    return code+tree->count[max]==(1U<<max);
}

static bool xx_lha_lh3_symbol(xx_lha_lh3_bits *bits,
                               const xx_lha_lh3_tree *tree,unsigned *symbol) {
    unsigned length,code=0U,bit;
    if(tree->single){*symbol=tree->single_symbol;return true;}
    for(length=1U;length<=tree->max_length;++length){
        if(!xx_lha_lh3_get(bits,1U,&bit))return false;
        code=(code<<1U)|bit;
        if(code>=tree->first[length] &&
           code-tree->first[length]<tree->count[length]){
            *symbol=tree->symbols[tree->offset[length]+
                                  code-tree->first[length]];
            return true;
        }
    }
    return false;
}

static bool xx_lha_lh3_code_tree(xx_lha_lh3_bits *bits,
                                  xx_lha_lh3_tree *tree) {
    uint8_t lengths[286];unsigned i,present,value;
    memset(lengths,0,sizeof(lengths));
    for(i=0U;i<286U;++i){
        if(!xx_lha_lh3_get(bits,1U,&present))return false;
        if(present){
            if(!xx_lha_lh3_get(bits,4U,&value))return false;
            lengths[i]=(uint8_t)(value+1U);
        }
        if(i==2U && lengths[0]==1U && lengths[1]==1U &&
           lengths[2]==1U){
            if(!xx_lha_lh3_get(bits,9U,&value) || value>=286U)return false;
            memset(tree,0,sizeof(*tree));tree->single=true;
            tree->single_symbol=value;return true;
        }
    }
    return xx_lha_lh3_tree_build(tree,lengths,286U);
}

static bool xx_lha_lh3_position_tree(xx_lha_lh3_bits *bits,
                                      xx_lha_lh3_tree *tree,bool encoded) {
    uint8_t lengths[128];unsigned i,value,step=0U,length=2U;
    static const unsigned steps[7]={1U,1U,3U,6U,13U,31U,78U};
    if(encoded){
        for(i=0U;i<128U;++i){
            if(!xx_lha_lh3_get(bits,4U,&value))return false;
            lengths[i]=(uint8_t)value;
            if(i==2U && lengths[0]==1U && lengths[1]==1U &&
               lengths[2]==1U){
                if(!xx_lha_lh3_get(bits,7U,&value) || value>=128U)return false;
                memset(tree,0,sizeof(*tree));tree->single=true;
                tree->single_symbol=value;return true;
            }
        }
    }else{
        for(i=0U;i<128U;++i){
            while(step<7U && steps[step]==i){++length;++step;}
            lengths[i]=(uint8_t)length;
        }
    }
    return xx_lha_lh3_tree_build(tree,lengths,128U);
}

static bool xx_lha_lh3_decode_native(const uint8_t *input,size_t input_size,
                                     uint8_t *output,size_t output_size,
                                     xx_pd_struct *pd) {
    xx_lha_lh3_bits bits;
    xx_lha_lh3_tree codes,positions;
    size_t written=0U;
    unsigned block_left=0U;
    if(!input || !input_size || !output || !output_size ||
       input_size>SIZE_MAX/8U || xx_pd_is_stopped(pd))return false;
    bits.data=input;bits.size=input_size;bits.position=0U;
    while(written<output_size){
        unsigned symbol,group,low,encoded;
        size_t distance,length,i;
        if(xx_pd_is_stopped(pd))return false;
        if(!block_left){
            if(!xx_lha_lh3_get(&bits,16U,&block_left) || !block_left ||
               !xx_lha_lh3_code_tree(&bits,&codes) ||
               !xx_lha_lh3_get(&bits,1U,&encoded) ||
               !xx_lha_lh3_position_tree(&bits,&positions,
                                           encoded!=0U))return false;
        }
        --block_left;
        if(!xx_lha_lh3_symbol(&bits,&codes,&symbol))return false;
        if(symbol==285U){
            unsigned extra;
            if(!xx_lha_lh3_get(&bits,8U,&extra))return false;
            symbol+=extra;
        }
        if(symbol<256U){output[written++]=(uint8_t)symbol;continue;}
        length=(size_t)symbol-253U;
        if(length>output_size-written ||
           !xx_lha_lh3_symbol(&bits,&positions,&group) ||
           group>=128U || !xx_lha_lh3_get(&bits,6U,&low))return false;
        distance=((size_t)group<<6U)+(size_t)low+1U;
        if(distance>8192U)return false;
        for(i=0U;i<length;++i){
            output[written]=distance>written ? (uint8_t)' ' :
                            output[written-distance];
            ++written;
        }
    }
    if((bits.position+7U)/8U!=input_size)return false;
    return !xx_pd_is_stopped(pd);
}
#endif
