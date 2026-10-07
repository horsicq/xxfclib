/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * FORM/CDAF Shrink FILE/NOTE/BODY chunk reader, from published framing facts. */
#include "xxfclib/formats/shrink_cdaf/xx_shrink_cdaf.h"
#include "../xx_legacy_archive.h"
#include "../xx_shrink_cdaf_codec.h"
static bool cdaf_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t at=26;
    if(b->n<26U || xx_rt_memcmp(b->p,"FORM",4) || xx_data_get_u32(b->p+4, 4, 0, true)!=b->n-8U || xx_rt_memcmp(b->p+8,"CDAFNAME",8) || xx_data_get_u32(b->p+16, 4, 0, true)!=6U || xx_rt_memcmp(b->p+20,"shrink",6)) return false;
    while(at<b->n) {
        uint32_t header,namesize,n,packed,pos; unsigned method; uint16_t crc; uint8_t *out; char name[96];
        if(!ac_poll(b) || !ac_span(b,at,28U) || xx_rt_memcmp(b->p+at,"FILE",4)) return false;
        header=xx_data_get_u32(b->p+at+4, 4, 0, true); if(header<20U || header>65535U) return false;
        namesize=header-20U; n=xx_data_get_u32(b->p+at+14, 4, 0, true); method=b->p[at+9]; crc=xx_data_get_u16(b->p+at+24, 2, 0, true);
        pos=at+28U; if(!ac_span(b,pos,namesize) || !ac_name(name,sizeof(name),b->p+pos,namesize)) return false;
        pos+=namesize+(namesize&1U);
        if(!ac_span(b,pos,8U)) return false;
        if(!xx_rt_memcmp(b->p+pos,"NOTE",4)) { uint32_t note=xx_data_get_u32(b->p+pos+4, 4, 0, true);
            if(note==UINT32_MAX || !ac_span(b,pos+8U,note)) { return false; } pos+=8U+note+(note&1U); }
        if(!ac_span(b,pos,8U) || xx_rt_memcmp(b->p+pos,"BODY",4)) return false;
        packed=xx_data_get_u32(b->p+pos+4, 4, 0, true); pos+=8U; if(!ac_span(b,pos,packed) || packed==UINT32_MAX) return false;
        out=ac_alloc(b,n); if(!out) return false;
        if(!method) { if(packed!=n) { ac_release(b,out,n); return false; } xx_rt_memcpy(out,b->p+pos,n); }
        else if(packed<4U || !sc_decode(b,b->p+pos+4U,packed-4U,out,n,method)) { ac_release(b,out,n); return false; }
        if(crc && ac_crc16(out,n)!=crc) { ac_release(b,out,n); return ac_error(b,"Shrink member CRC mismatch"); }
        if(!ac_memory(f,s,b,name,out,n,packed,(uint16_t)method)) return false;
        at=pos+packed+(packed&1U);
    }
    return at==b->n && s->count;
}
AC_PARSE(cdaf_parse)
AC_DEFINE(shrink_cdaf,XX_FILE_TYPE_SHRINK_CDAF,"shr")
