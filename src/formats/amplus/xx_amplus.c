/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Amiga Plus FORM/APUP nested chunk framing and longword-sum checksum.
 * Wire-format reference: libxad AMPK.c (AmPlusUnpack). Original implementation. */
#include "xxfclib/formats/amplus/xx_amplus.h"
#include "../xx_legacy_archive.h"
static bool amplus_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t at=12;
    if(b->n<12U || xx_rt_memcmp(b->p,"FORM",4) || xx_rt_memcmp(b->p+8,"APUP",4) || pm_be32(b->p+4)!=b->n-8U) return false;
    while(at<b->n) {
        const uint8_t *tag; uint32_t outer,limit,pos,filesize,namesize,size,mode=0,crc=0,sum=0,i; char name[96]; uint8_t *out=NULL;
        if(!ac_poll(b) || !ac_span(b,at,8)) return false;
        tag=b->p+at; outer=pm_be32(tag+4); pos=at+8U;
        if(!ac_span(b,pos,outer) || outer==UINT32_MAX) return false; limit=pos+outer;
        if(!xx_rt_memcmp(tag,"VERS",4) || !xx_rt_memcmp(tag,"DISK",4) || !xx_rt_memcmp(tag,"PREF",4) || !xx_rt_memcmp(tag,"MKDR",4)) { at=limit+(outer&1U); continue; }
        if(xx_rt_memcmp(tag,"HELP",4)) {
            if(xx_rt_memcmp(tag,"PACK",4) && xx_rt_memcmp(tag,"DATA",4)) return false;
            if(outer<10U) return false; mode=pm_be16(b->p+pos); size=pm_be32(b->p+pos+2); crc=pm_be32(b->p+pos+6); pos+=10U;
            if(size>outer) return false;
        }
        if(!ac_span(b,pos,36U) || pos+36U>limit || xx_rt_memcmp(b->p+pos,"FILE",4)) return false;
        filesize=pm_be32(b->p+pos+8); pos+=28U;
        if(xx_rt_memcmp(b->p+pos,"NAME",4)) return false;
        namesize=pm_be32(b->p+pos+4); pos+=8U;
        if(!ac_span(b,pos,namesize) || !ac_name(name,sizeof(name),b->p+pos,namesize)) return false;
        pos+=namesize; /* APUP NAME body already includes its original padding. */
        if(!ac_span(b,pos,8U) || pos+8U>limit) return false;
        size=pm_be32(b->p+pos+4); pos+=8U;
        if(!ac_span(b,pos,size) || pos+size+(size&1U)!=limit) return false;
        out=ac_alloc(b,filesize); if(!out) return false;
        if(!xx_rt_memcmp(tag,"PACK",4)) {
            if(mode!=0U) { ac_release(b,out,filesize); return ac_error(b,"Amiga Plus XPK compression is unsupported"); }
            if(!ac_ampk(b,b->p+pos,size,out,filesize)) { ac_release(b,out,filesize); return false; }
        } else { if(filesize!=size) { ac_release(b,out,filesize); return false; } xx_rt_memcpy(out,b->p+pos,size); }
        for(i=0;i<filesize;++i) { if((i&4095U)==0 && !ac_poll(b)) { ac_release(b,out,filesize); return false; } sum+=(uint32_t)out[i]<<((3U-(i&3U))*8U); }
        if(xx_rt_memcmp(tag,"HELP",4) && sum!=crc) { ac_release(b,out,filesize); return ac_error(b,"Amiga Plus checksum mismatch"); }
        if(!ac_memory(f,s,b,name,out,filesize,size,!xx_rt_memcmp(tag,"PACK",4)?1:0)) return false;
        at=limit+(outer&1U);
    }
    return at==b->n && s->count;
}
AC_PARSE(amplus_parse)
AC_DEFINE(amplus,XX_FILE_TYPE_AMPLUS,"apu")
