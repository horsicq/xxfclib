/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original ShrinkIt LZW1/LZW2 decoder from published on-disk facts.
 * https://nulib.com/library/FTN.e08002.htm
 * https://github.com/fadden/nulib2/blob/master/nufxlib/Lzw.c
 */
#ifndef XX_NULZW_PRIVATE_H
#define XX_NULZW_PRIVATE_H
typedef struct nl_state {uint16_t parent[4096];uint8_t suffix[4096],stack[4096],rle[4096],chunk[4096];uint32_t next,old,first;bool previous;} nl_state;
static void nl_reset(nl_state *s){s->next=257U;s->previous=false;}
static bool nl_codes(af_work *w,nl_state *s,const uint8_t *p,size_t n,uint32_t wanted,size_t *used) {
    size_t bit=0;uint32_t out=0;
    while(out<wanted){unsigned width=9U,k;uint32_t code=0,walk,depth=0,first,original;
        if(!af_poll(w) || s->next>4095U)return false;
        while(width<12U && (s->next+1U)>=(1U<<width))++width;
        if(bit>n*8U || width>n*8U-bit)return false;
        for(k=0;k<width;++k) {code|=(uint32_t)((p[(bit+k)/8U]>>((bit+k)&7U))&1U)<<k; } bit+=width;
        if(code==256U){nl_reset(s);continue;}
        if(!s->previous){if(code>255U)return false;s->rle[out++]=(uint8_t)code;s->old=code;s->first=code;s->previous=true;continue;}
        original=code;walk=code;
        if(code==s->next){s->stack[depth++]=(uint8_t)s->first;walk=s->old;}
        else if(code>s->next)return false;
        while(walk>=257U){if(walk>=s->next || depth>=4095U || s->parent[walk]>=walk)return false;s->stack[depth++]=s->suffix[walk];walk=s->parent[walk];}
        if(walk>255U || depth>=4096U) {return false; } first=walk;s->stack[depth++]=(uint8_t)first;
        if(depth>wanted-out) {return false; } while(depth)s->rle[out++]=s->stack[--depth];
        s->parent[s->next]=(uint16_t)s->old;s->suffix[s->next]=(uint8_t)first;++s->next;s->old=original;s->first=first;
    }
    *used=(bit+7U)/8U;return true;
}
static bool nl_rle(nl_state *s,const uint8_t *p,uint32_t n,uint8_t marker) {
    uint32_t at=0,out=0;if(n==4096U){xx_rt_memcpy(s->chunk,p,4096);return true;}
    while(at<n){uint8_t value=p[at++];uint32_t count=1;
        if(value==marker){if(n-at<2U)return false;value=p[at++];count=(uint32_t)p[at++]+1U;}
        if(count>4096U-out) {return false; } xx_rt_memset(s->chunk+out,value,count);out+=count;}
    return out==4096U;
}
static bool nl_decode(af_work *w,const uint8_t *p,size_t n,uint8_t *out,uint32_t wanted,unsigned method) {
    nl_state *s;size_t at=method==2U?4U:2U;uint32_t done=0;uint16_t crc=0;bool ok=false;uint8_t marker;
    if(n<at || !(s=(nl_state *)af_alloc(w,sizeof(*s),false)))return false;
    marker=p[at-1U];nl_reset(s);
    while(done<wanted){uint32_t raw;bool lzw;size_t take,used=0,header,declared=0;const uint8_t *rle;uint32_t copy=wanted-done>4096U?4096U:wanted-done;
        if(!af_poll(w) || at>n || n-at<2U) {goto end; } header=at;raw=pm_le16(p+at);at+=2U;
        if(method==2U){if(n-at<1U || p[at]>1U || raw<1U || raw>4096U)goto end;lzw=p[at++]!=0;nl_reset(s);}
        else {lzw=(raw&0x8000U)!=0;if(raw&0x6000U)goto end;raw&=0x1fffU;if(raw<1U || raw>4096U)goto end;
            if(lzw){if(n-at<2U)goto end;declared=pm_le16(p+at);at+=2U;if(declared<5U || declared>n-header){declared=pm_be16(p+at-2U);if(declared<5U || declared>n-header)goto end;}}}
        if(lzw){take=method==3U?declared-4U:n-at;if(!nl_codes(w,s,p+at,take,raw,&used) || (method==3U && used!=take))goto end;rle=s->rle;at+=used;}
        else {take=raw;if(take>n-at)goto end;rle=p+at;at+=take;nl_reset(s);}
        if(!nl_rle(s,rle,raw,marker))goto end;
        if(method==2U)crc=af_crc16(w,s->chunk,4096U,crc);
        xx_rt_memcpy(out+done,s->chunk,copy);done+=copy;
    }
    ok=at<=n && n-at<=1U && (method!=2U || crc==pm_le16(p)) && af_poll(w);
end:af_release(w,s,sizeof(*s));return ok;
}
#endif
