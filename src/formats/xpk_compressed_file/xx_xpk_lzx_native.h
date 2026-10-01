/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient LZXDecompressor.cpp,
 * HuffmanDecoder.hpp and VariableLengthCodeDecoder.hpp at commit
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_LZX_NATIVE_H
#define XX_XPK_LZX_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XPK_LZX_LITERAL_NODES (768U*16U+1U)
#define XPK_LZX_LENGTH_NODES (20U*16U+1U)
#define XPK_LZX_DISTANCE_NODES (8U*16U+1U)
typedef struct xpk_lzx_node {int32_t child[2],symbol;} xpk_lzx_node;
typedef struct xpk_lzx_bits {
    const uint8_t *data;
    size_t at,end;
    uint16_t word;
    unsigned left;
} xpk_lzx_bits;
static uint32_t xpk_lzx_le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|
           ((uint32_t)p[3]<<24U);
}
static uint32_t xpk_lzx_crc(const uint8_t *data,size_t size,uint32_t previous) {
    return xx_crc32_calc(previous,data,size);
}
static bool xpk_lzx_read(xpk_lzx_bits *bits,unsigned count,uint32_t *value) {
    uint32_t result=0U;
    unsigned i;
    if(count>16U)return false;
    for(i=0U;i<count;++i) {
        if(!bits->left) {
            if(bits->at>bits->end || bits->end-bits->at<2U)return false;
            bits->word=(uint16_t)(((uint16_t)bits->data[bits->at]<<8U)|
                                   bits->data[bits->at+1U]);
            bits->at+=2U;bits->left=16U;
        }
        result|=(uint32_t)(bits->word&1U)<<i;
        bits->word>>=1U;--bits->left;
    }
    *value=result;return true;
}
static void xpk_lzx_node_init(xpk_lzx_node *node) {
    node->child[0]=-1;node->child[1]=-1;node->symbol=-1;
}
static bool xpk_lzx_insert(xpk_lzx_node *nodes,uint32_t *count,uint32_t capacity,
                            unsigned width,uint32_t code,uint32_t symbol) {
    uint32_t at=0U;
    unsigned i;
    if(!width || width>16U || code>=(1U<<width) || symbol>767U)return false;
    for(i=0U;i<width;++i) {
        uint32_t bit=(code>>(width-i-1U))&1U;
        int32_t next;
        if(nodes[at].symbol>=0)return false;
        next=nodes[at].child[bit];
        if(next<0) {
            if(*count>=capacity)return false;
            next=(int32_t)(*count)++;
            nodes[at].child[bit]=next;
            xpk_lzx_node_init(&nodes[next]);
        }
        at=(uint32_t)next;
    }
    if(nodes[at].symbol>=0 || nodes[at].child[0]>=0 || nodes[at].child[1]>=0)return false;
    nodes[at].symbol=(int32_t)symbol;return true;
}
static bool xpk_lzx_build(xpk_lzx_node *nodes,uint32_t *count,uint32_t capacity,
                           const uint8_t *lengths,uint32_t length) {
    uint32_t code=0U,i;
    unsigned min_depth=17U,max_depth=0U,depth;
    *count=1U;xpk_lzx_node_init(nodes);
    for(i=0U;i<length;++i) {
        if(lengths[i]>16U)return false;
        if(lengths[i]) {
            if(lengths[i]<min_depth)min_depth=lengths[i];
            if(lengths[i]>max_depth)max_depth=lengths[i];
        }
    }
    if(!max_depth)return true;
    for(depth=min_depth;depth<=max_depth;++depth) {
        uint32_t step=1U<<(max_depth-depth);
        for(i=0U;i<length;++i)if(lengths[i]==depth) {
            if(code>=(1U<<max_depth) ||
               !xpk_lzx_insert(nodes,count,capacity,depth,
                                code>>(max_depth-depth),i))return false;
            code+=step;
        }
    }
    return true;
}
static bool xpk_lzx_symbol(const xpk_lzx_node *nodes,uint32_t count,
                            xpk_lzx_bits *bits,uint32_t *symbol) {
    uint32_t at=0U;
    unsigned depth=0U;
    if(count<=1U)return false;
    while(nodes[at].symbol<0) {
        uint32_t bit;
        int32_t next;
        if(depth++>=16U || !xpk_lzx_read(bits,1U,&bit))return false;
        next=nodes[at].child[bit];
        if(next<0 || (uint32_t)next>=count)return false;
        at=(uint32_t)next;
    }
    *symbol=(uint32_t)nodes[at].symbol;return true;
}
/* Version 29=ELZX; 30=SLZX (sample delta after inner CRC). */
static bool xpk_lzx_native(const uint8_t *packed,size_t size,uint8_t *output,
                           size_t wanted,bool sampled,xx_pd_struct *pd) {
    static const uint8_t vlc_lengths[32]={
        0U,0U,0U,0U,1U,1U,2U,2U,3U,3U,4U,4U,5U,5U,6U,
        6U,7U,7U,8U,8U,9U,9U,10U,10U,11U,11U,12U,12U,13U,13U,
        14U,14U};
    xpk_lzx_node literal[XPK_LZX_LITERAL_NODES];
    xpk_lzx_node length_tree[XPK_LZX_LENGTH_NODES];
    xpk_lzx_node distance_tree[XPK_LZX_DISTANCE_NODES];
    uint8_t literal_lengths[768];
    uint32_t vlc_offsets[32];
    uint32_t literal_count=1U,length_count=1U,distance_count=1U;
    uint32_t raw_size,packed_size,raw_crc,header_crc,previous_distance=1U;
    uint32_t offset,mode;
    xpk_lzx_bits bits;
    size_t produced=0U,blocks=0U,i;
    if(!packed || !output || !wanted || size<41U || xx_pd_is_stopped(pd))return false;
    if(packed[0]!='L' || packed[1]!='Z' || packed[2]!='X' || packed[3])return false;
    raw_size=xpk_lzx_le32(packed+12U);
    packed_size=xpk_lzx_le32(packed+16U);
    raw_crc=xpk_lzx_le32(packed+32U);
    header_crc=xpk_lzx_le32(packed+36U);
    mode=packed[21U];
    if(raw_size!=wanted || (mode!=0U && mode!=2U))return false;
    offset=41U+(uint32_t)packed[40U]+(uint32_t)packed[24U];
    if(offset>size || packed_size>size-offset)return false;
    {
        static const uint8_t zeros[4]={0U,0U,0U,0U};
        uint32_t crc=xpk_lzx_crc(packed+10U,26U,0U);
        crc=xpk_lzx_crc(zeros,4U,crc);
        crc=xpk_lzx_crc(packed+40U,offset-40U,crc);
        if(crc!=header_crc)return false;
    }
    if(mode==0U) {
        if(packed_size!=wanted)return false;
        memcpy(output,packed+offset,wanted);
        return !xx_pd_is_stopped(pd);
    }
    bits.data=packed;bits.at=offset;bits.end=offset+packed_size;
    bits.word=0U;bits.left=0U;
    memset(literal_lengths,0,sizeof(literal_lengths));
    xpk_lzx_node_init(literal);xpk_lzx_node_init(distance_tree);
    vlc_offsets[0]=0U;
    for(i=1U;i<32U;++i)
        vlc_offsets[i]=vlc_offsets[i-1U]+(1U<<vlc_lengths[i-1U]);
    while(produced<wanted) {
        uint32_t method,block_length,part,symbol;
        if(++blocks>wanted+1U || xx_pd_is_stopped(pd) ||
           !xpk_lzx_read(&bits,3U,&method) || method<1U || method>3U)return false;
        distance_count=1U;xpk_lzx_node_init(distance_tree);
        if(method==3U) {
            uint8_t lengths[8];
            for(i=0U;i<8U;++i) {
                if(!xpk_lzx_read(&bits,3U,&part))return false;
                lengths[i]=(uint8_t)part;
            }
            if(!xpk_lzx_build(distance_tree,&distance_count,XPK_LZX_DISTANCE_NODES,
                               lengths,8U))return false;
        }
        if(!xpk_lzx_read(&bits,8U,&block_length) ||
           !xpk_lzx_read(&bits,8U,&part))return false;
        block_length=(block_length<<16U)|(part<<8U);
        if(!xpk_lzx_read(&bits,8U,&part))return false;
        block_length|=part;
        if(block_length>wanted-produced)return false;
        if(method!=1U) {
            uint32_t position=0U,half;
            literal_count=1U;xpk_lzx_node_init(literal);
            for(half=0U;half<2U;++half) {
                uint32_t adjust=half?0U:1U;
                uint32_t maximum=half?768U:256U;
                uint8_t length_lengths[20];
                for(i=0U;i<20U;++i) {
                    if(!xpk_lzx_read(&bits,4U,&part))return false;
                    length_lengths[i]=(uint8_t)part;
                }
                if(!xpk_lzx_build(length_tree,&length_count,XPK_LZX_LENGTH_NODES,
                                   length_lengths,20U))return false;
                while(position<maximum) {
                    uint32_t repeat=0U,value=0U;
                    if(xx_pd_is_stopped(pd) ||
                       !xpk_lzx_symbol(length_tree,length_count,&bits,&symbol))return false;
                    if(symbol==17U) {
                        if(!xpk_lzx_read(&bits,4U,&part))return false;
                        repeat=part+3U+adjust;
                    } else if(symbol==18U) {
                        if(!xpk_lzx_read(&bits,6U-adjust,&part))return false;
                        repeat=part+19U+adjust;
                    } else if(symbol==19U) {
                        if(!xpk_lzx_read(&bits,1U,&part) ||
                           !xpk_lzx_symbol(length_tree,length_count,&bits,&value))return false;
                        repeat=part+3U+adjust;
                        value=(literal_lengths[position]+17U-value)%17U;
                    } else {
                        if(symbol>16U)return false;
                        literal_lengths[position]=(uint8_t)((literal_lengths[position]+17U-symbol)%17U);
                        ++position;continue;
                    }
                    if(repeat>maximum-position)repeat=maximum-position;
                    memset(literal_lengths+position,(int)value,repeat);
                    position+=repeat;
                }
            }
            if(!xpk_lzx_build(literal,&literal_count,XPK_LZX_LITERAL_NODES,
                               literal_lengths,768U))return false;
        }
        while(block_length) {
            uint32_t count,distance,index,extra;
            size_t j;
            if(xx_pd_is_stopped(pd) ||
               !xpk_lzx_symbol(literal,literal_count,&bits,&symbol))return false;
            if(symbol<256U) {
                output[produced++]=(uint8_t)symbol;
                --block_length;continue;
            }
            symbol-=256U;
            index=symbol&31U;
            if(index>=32U)return false;
            if(index>=8U && method==3U) {
                uint32_t low;
                if(!xpk_lzx_read(&bits,vlc_lengths[index]-3U,&extra) ||
                   !xpk_lzx_symbol(distance_tree,distance_count,&bits,&low))return false;
                distance=vlc_offsets[index]+((extra<<3U)|low);
            } else {
                if(!xpk_lzx_read(&bits,vlc_lengths[index],&extra))return false;
                distance=vlc_offsets[index]+extra;
                if(!distance)distance=previous_distance;
            }
            previous_distance=distance;
            index=symbol>>5U;
            if(index>=32U || !xpk_lzx_read(&bits,vlc_lengths[index],&extra))return false;
            count=vlc_offsets[index]+extra+3U;
            if(count>block_length || distance>produced || !distance)return false;
            for(j=0U;j<count;++j) {
                if((j&1023U)==0U && xx_pd_is_stopped(pd))return false;
                output[produced+j]=output[produced+j-distance];
            }
            produced+=count;block_length-=count;
        }
    }
    if(xpk_lzx_crc(output,wanted,0U)!=raw_crc || xx_pd_is_stopped(pd))return false;
    if(sampled) {
        uint8_t accumulator=0U;
        for(i=0U;i<wanted;++i) {
            if((i&1023U)==0U && xx_pd_is_stopped(pd))return false;
            accumulator=(uint8_t)(accumulator+output[i]);
            output[i]=accumulator;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
