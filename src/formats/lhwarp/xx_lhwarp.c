/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * LhWarp revision 0..3, bounded explicit cylinder/sector presence.
 * Reference: libxad AMPK.c LhWarp framing, bitmap and checksum conventions. */
#include "xxfclib/formats/lhwarp/xx_lhwarp.h"
#include "../xx_legacy_archive.h"
#include "../xx_legacy_huffman.h"
static bool lhwarp_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t rev,low,high,at,comment,packed,c,block,total,labeltotal; uint8_t *image,*labels=NULL;
    if(b->n<16U || b->p[0]!=1U || b->p[1]>3U || b->p[2]>9U || (!b->p[1] && !b->p[2]) || b->p[3]) return false;
    rev=b->p[1]; low=pm_be16(b->p+4); high=pm_be16(b->p+6);
    comment=pm_be32(b->p+8); packed=pm_be32(b->p+12); at=rev<3U?16U:20U;
    if(low>high || high>=80U || comment>65535U || packed>comment || !ac_span(b,at,packed)) return false;
    if(comment) { uint8_t *text=ac_alloc(b,comment); uint32_t written=0;
        if(!text) return false;
        if(!ac_adaptive(b,b->p+at,packed,text,comment,&written,NULL,4U) || written!=comment) { ac_release(b,text,comment); return false; }
        if(!ac_memory(f,s,b,"comment.txt",text,comment,packed,1)) return false;
    } else if(packed) return false;
    at+=packed; total=(high-low+1U)*11264U; labeltotal=(high-low+1U)*352U;
    image=ac_alloc(b,total); if(!image) return false;
    if(rev) { labels=ac_alloc(b,labeltotal); if(!labels) { ac_release(b,image,total); return false; } xx_mem_zero(labels,labeltotal); }
    for(c=low;c<=high;++c) {
        uint32_t bitmap,n,stored,crc,i,j=0,sum=0; uint16_t method; uint8_t *plain;
        uint32_t header=rev?20U:16U;
        if(!ac_poll(b) || !ac_span(b,at,header)) goto fail;
        method=rev?b->p[at]:0U;
        bitmap=rev?b->p[at+4]|(uint32_t)b->p[at+5]<<8U|(uint32_t)b->p[at+6]<<16U:0x3FFFFFU;
        if(b->p[at+2]!=c || (rev && (b->p[at+1]!=0U && b->p[at+1]!=2U)) || (bitmap&~0x3FFFFFU)) goto fail;
        n=pm_be32(b->p+at+(rev?8U:4U)); stored=pm_be32(b->p+at+(rev?12U:8U)); crc=pm_be32(b->p+at+(rev?16U:12U));
        block=rev?528U:512U;
        for(i=0;i<22U;++i) if(bitmap&(1U<<i)) j+=block;
        if(n!=j || !ac_span(b,at+header,stored)) goto fail;
        at+=header; plain=ac_alloc(b,n); if(!plain) goto fail;
        if(!n) { if(stored || crc) { ac_release(b,plain,n); goto fail; } }
        else if(method==1U) { if(stored!=n) { ac_release(b,plain,n); goto fail; } xx_rt_memcpy(plain,b->p+at,n); }
        else if(method==0U) { uint32_t written=0; if(!ac_adaptive(b,b->p+at,stored,plain,n,&written,NULL,rev?4U:5U) || written!=n) { ac_release(b,plain,n); goto fail; } }
        else if(method==7U) { if(!ac_unix(b,b->p+at,stored,plain,n,14U)) { ac_release(b,plain,n); goto fail; } }
        else if(method==10U) { if(!ac_warp_decode(b,b->p+at,stored,plain,n,2U)) { ac_release(b,plain,n); goto fail; } }
        else { ac_release(b,plain,n); ac_error(b,"unsupported LhWarp compression method"); goto fail; }
        if(!rev) { for(i=0;i<n;++i) sum+=plain[i]; } else sum=ac_crc32(plain,n,0U);
        if(sum!=crc) { ac_release(b,plain,n); ac_error(b,"LhWarp cylinder checksum mismatch"); goto fail; }
        j=0;
        for(i=0;i<22U;++i) {
            uint8_t *dest=image+(c-low)*11264U+i*512U;
            if(bitmap&(1U<<i)) { xx_rt_memcpy(dest,plain+j,512U); if(labels) xx_rt_memcpy(labels+(c-low)*352U+i*16U,plain+j+512U,16U); j+=block; }
            else xx_mem_zero(dest,512U); /* explicit absence bitmap, not missing input */
        }
        ac_release(b,plain,n); at+=stored;
    }
    if(at!=b->n) goto fail;
    ((xx_lhwarp *)f)->incomplete=low!=0U || high!=79U;
    ((xx_lhwarp *)f)->note="declared cylinder range decoded; zero sectors only where explicitly absent in bitmap; labels preserved";
    if(!ac_memory(f,s,b,"cylinders.adf",image,total,b->n,1)) { if(labels) ac_release(b,labels,labeltotal); return false; }
    return !labels || ac_memory(f,s,b,"sector-labels.bin",labels,labeltotal,0,0);
fail: ac_release(b,image,total); if(labels) ac_release(b,labels,labeltotal); return false;
}
AC_PARSE(lhwarp_parse)
AC_DEFINE(lhwarp,XX_FILE_TYPE_LHWARP,"lhw")
