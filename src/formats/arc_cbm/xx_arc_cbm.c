/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CBM ARC v1/v2 254-byte records, RLE, explicit Huffman and variable LZW.
 * Wire-format facts: libxad AMPK.c ArcCBM and CBM decoder descriptions. */
#include "xxfclib/formats/arc_cbm/xx_arc_cbm.h"
#include "../xx_legacy_archive.h"
typedef struct cbm_sink { uint8_t *p,marker,last,count,state; uint32_t n,at; bool old,rle; ac_blob *b; } cbm_sink;
static bool cbm_put(cbm_sink *s,uint8_t c) {
    uint32_t n=1;
    if(!ac_poll(s->b)) return false;
    if(s->rle) {
        if(s->state==1U) { s->count=c; s->state=2; return true; }
        if(s->state==2U) { n=s->count?s->count:(s->old?255U:256U); s->state=0; }
        else if(c==s->marker) { s->state=1; return true; }
    }
    if(n>s->n-s->at) { return false; } while(n--) s->p[s->at++]=c; return true;
}
static uint32_t cbm_reverse(uint32_t x,unsigned n) { uint32_t v=0; while(n--) { v=(v<<1U)|(x&1U); x>>=1; } return v; }
static bool cbm_decode(ac_blob *b,const uint8_t *in,uint32_t packed,uint8_t *out,uint32_t n,unsigned version,unsigned method) {
    cbm_sink sink; ac_bits bits; uint32_t i;
    xx_mem_zero(&sink,sizeof(sink)); sink.p=out; sink.n=n; sink.b=b; sink.old=version==1U; sink.rle=method==1U || method==3U || method==4U; sink.marker=0xFE;
    if(method==1U) { if(!packed) return false; sink.marker=*in++; --packed; }
    bits.p=in; bits.n=packed; bits.bit=0; bits.failed=false; bits.lsb=true;
    if(method<=1U) {
        for(i=0;i<packed && sink.at<n;++i) if(!cbm_put(&sink,in[i])) return false;
    } else if(method==2U || method==4U) {
        uint32_t codes[256]; uint8_t lengths[256];
        for(i=0;i<256U;++i) { lengths[i]=(uint8_t)ac_bits_get(&bits,5);
            if(lengths[i]>24U) { return false; } codes[i]=ac_bits_get(&bits,lengths[i]); }
        while(sink.at<n) { uint32_t code=0; unsigned width; bool found=false;
            for(width=1U;width<=24U && !found;++width) {
                code|=ac_bits_get(&bits,1)<<(width-1U); if(bits.failed) return false;
                for(i=0;i<256U;++i) if(lengths[i]==width && codes[i]==code) { if(!cbm_put(&sink,(uint8_t)i)) return false; found=true; break; }
            }
            if(!found) return false;
        }
    } else if(method==3U) {
        uint16_t prefix[4096]; uint8_t suffix[4096],stack[4096],first=0; uint32_t next=258,previous,code,token,top,width=9,before_bump=253,interval=256;
        previous=cbm_reverse(ac_bits_get(&bits,width),width);
        if(previous==256U) { return n==0; } if(previous>255U || bits.failed || !cbm_put(&sink,(uint8_t)previous)) return false; first=(uint8_t)previous;
        for(;;) {
            token=code=cbm_reverse(ac_bits_get(&bits,width),width); if(bits.failed) return false;
            if(width<12U && --before_bump==0U) { ++width; interval*=2U; before_bump=interval; }
            if(code==256U) { break; } if(code==257U || code>next || code>=4096U) return false; top=0;
            if(code==next) { stack[top++]=first; code=previous; }
            while(code>255U) { if(code<258U || code>=next || top>=4095U || prefix[code]>=code) return false; stack[top++]=suffix[code]; code=prefix[code]; }
            first=(uint8_t)code; stack[top++]=first;
            while(top) if(!cbm_put(&sink,stack[--top])) return false;
            if(next<4096U) { prefix[next]=(uint16_t)previous; suffix[next++]=first; }
            previous=token;
        }
    } else return ac_error(b,"CBM ARC one-pass mode 5 is unsupported");
    return !bits.failed && sink.at==n && !sink.state && ac_poll(b);
}
static bool cbm_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t at=0;
    /* BASIC SYS wrapper: the line number determines the exact payload block. */
    if(b->n>265U && b->p[6]==0x9EU && b->p[7]=='(') {
        uint32_t line=pm_le16(b->p+4); if(line<7U || line>4096U) return false;
        at=(line-6U)*254U; if(line==15U && b->p[8]=='7') --at;
    }
    while(at<b->n) {
        uint32_t n,blocks,record,header,name_n,i,sum=0; uint16_t expected; unsigned ver,method; uint8_t *out; char name[96];
        if(!ac_poll(b) || !ac_span(b,at,11U)) return false;
        ver=b->p[at]; method=b->p[at+1]; expected=pm_le16(b->p+at+2);
        n=(uint32_t)b->p[at+4]|(uint32_t)b->p[at+5]<<8U|(uint32_t)b->p[at+6]<<16U;
        blocks=pm_le16(b->p+at+7); name_n=b->p[at+10]; header=11U+name_n+(ver==2U?3U:0U);
        if((ver!=1U && ver!=2U) || method>(ver==1U?2U:5U) || !name_n || name_n>16U || !blocks ||
            (b->p[at+9]!='P' && b->p[at+9]!='S' && b->p[at+9]!='U' && !(ver==2U && b->p[at+9]=='R')) ||
            !ac_span(b,at,header) || !ac_name(name,sizeof(name),b->p+at+11,name_n)) return false;
        record=blocks*254U; if(record<header || !ac_span(b,at,record)) return false;
        if(!method && (n>record-header || (n+253U)/254U!=blocks)) return false;
        out=ac_alloc(b,n); if(!out) return false;
        if(!cbm_decode(b,b->p+at+header,method?record-header:n,out,n,ver,method)) { ac_release(b,out,n); return false; }
        for(i=0;i<n;++i) sum+=ver==1U?out[i]:(out[i]^(uint8_t)(i+1U));
        if((uint16_t)sum!=expected) { ac_release(b,out,n); return ac_error(b,"CBM ARC member checksum mismatch"); }
        if(!ac_memory(f,s,b,name,out,n,method?record-header:n,(uint16_t)method)) return false;
        at+=record;
    }
    return at==b->n && s->count;
}
AC_PARSE(cbm_parse)
AC_DEFINE(arc_cbm,XX_FILE_TYPE_ARC_CBM,"arc")
