/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * LhF big-endian directory and block-coded canonical Huffman/LZ payload.
 * Reference: libxad LhF.c header/bitstream facts. Original implementation. */
#include "xxfclib/formats/lhf/xx_lhf.h"
#include "../xx_legacy_archive.h"
#include "../xx_legacy_prefix.h"
static bool lhf_tree(ac_bits *bits,ac_prefix *tree,unsigned count_bits,unsigned max) {
    unsigned count=ac_bits_get(bits,count_bits),i; uint8_t lengths[20]={0};
    if(bits->failed) return false;
    if(!count) { unsigned symbol=ac_bits_get(bits,count_bits); if(bits->failed || symbol>=max) return false; ac_prefix_single(tree,symbol); return true; }
    if(count>=max) return false;
    for(i=0;i<=count;++i) {
        unsigned len=ac_bits_get(bits,3U);
        if(len==7U) { while(ac_bits_get(bits,1U)) { if(++len>16U || bits->failed) return false; } }
        lengths[i]=(uint8_t)len;
    }
    return !bits->failed && ac_prefix_build(tree,lengths,max,16U);
}
static bool lhf_decode(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n) {
    ac_bits bits; uint32_t at=0; bool more;
    bits.p=in; bits.n=packed; bits.bit=0; bits.failed=false; bits.lsb=false;
    do {
        unsigned count=ac_bits_get(&bits,16U),lengthcount=ac_bits_get(&bits,9U),highest=0; ac_prefix chars,positions,pre;
        if(!count || bits.failed) return false;
        if(lengthcount) {
            uint8_t lengths[512]={0}; unsigned i=0,previous=0;
            if(!lhf_tree(&bits,&pre,5U,20U)) return false;
            while(i<=lengthcount) {
                unsigned symbol,len,run=1;
                if(!ac_prefix_read(&pre,&bits,&symbol)) return false;
                if(symbol>=3U) len=symbol-3U;
                else if(!symbol) { if(!i || !previous) return false; len=previous; run=ac_bits_get(&bits,3U)+3U; }
                else { len=0; run=ac_bits_get(&bits,symbol==1U?2U:7U)+(symbol==1U?4U:8U); }
                if(bits.failed || run>512U-i || run>lengthcount+1U-i) return false;
                while(run--) { lengths[i++]=(uint8_t)len; } previous=len;
            }
            highest=lengthcount; while(highest && !lengths[highest]) --highest;
            if(!ac_prefix_build(&chars,lengths,512U,16U)) return false;
        } else {
            unsigned singleton=ac_bits_get(&bits,9U); if(bits.failed) return false;
            highest=singleton; ac_prefix_single(&chars,singleton);
        }
        if(highest>=256U && !lhf_tree(&bits,&positions,4U,16U)) return false;
        while(count--) {
            unsigned token; uint32_t length=1,distance=0;
            if(!ac_poll(b) || !ac_prefix_read(&chars,&bits,&token)) return false;
            if(token>=256U) {
                unsigned pos; if(!ac_prefix_read(&positions,&bits,&pos)) return false;
                distance=pos>1U?ac_bits_get(&bits,pos-1U)+(1U<<(pos-1U)):pos; ++distance;
                length=token-256U+3U;
            }
            if(bits.failed || length>n-at || distance>32768U) return false;
            while(length--) { uint8_t c=token<256U?(uint8_t)token:(distance>at?0:out[at-distance]); out[at++]=c; }
        }
        more=ac_bits_get(&bits,1U)!=0; if(bits.failed) return false;
    } while(more);
    return at==n && ac_poll(b);
}
static bool lhf_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t at=8,end;
    if(b->n<8U || xx_rt_memcmp(b->p,"LhF",4) || (end=pm_be32(b->p+4))!=b->n) return false;
    while(at<end) {
        uint32_t packed,n,name_n,pos; char name[96];
        if(!ac_poll(b) || !ac_span(b,at,16U)) return false;
        name_n=pm_be16(b->p+at+2); packed=pm_be32(b->p+at+4); n=pm_be32(b->p+at+8); pos=at+16U;
        if(!ac_span(b,pos,name_n) || !ac_name(name,sizeof(name),b->p+pos,name_n)) { return false; } pos+=name_n;
        if(!ac_span(b,pos,packed)) return false;
        if(n==UINT32_MAX) { if(packed) return false; }
        else if(!n) { if(!ac_emit(f,s,b,name,pos,packed)) return false; }
        else { uint8_t *out=ac_alloc(b,n); if(!out) return false;
            if(!lhf_decode(b,b->p+pos,packed,out,n)) { ac_release(b,out,n); return ac_error(b,"LhF member decode failed"); }
            if(!ac_memory(f,s,b,name,out,n,packed,1)) return false;
        }
        at=pos+packed;
    }
    ((xx_lhf *)f)->note="LhF stored/compressed file payloads; directory-only records do not have payloads; format has no member CRC";
    return at==end && s->count;
}
AC_PARSE(lhf_parse)
AC_DEFINE(lhf,XX_FILE_TYPE_LHF,"lhf")
