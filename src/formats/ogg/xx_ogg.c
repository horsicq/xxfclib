/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.rfc-editor.org/rfc/rfc3533.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/ogg/xx_ogg.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

typedef struct og_stream { uint32_t serial,sequence; bool continued,ended; } og_stream;
static const xx_crc_model og_crc_model = {
    32U, UINT64_C(0x04c11db7), 0U, false, false, 0U, "Ogg"
};
static bool og_crc(Abstractformat *f,int64_t at,int64_t size,uint32_t expected) {
    size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false; int64_t pos=0; xx_crc_context crc;
    if(!xx_crc_context_init(&crc,&og_crc_model)) return false;
    while(pos<size) {if(!b) { if((uint64_t)(size-pos)<capacity) capacity=(size_t)(size-pos); b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result=false; goto buffer_done; } }  size_t n=(uint64_t)(size-pos)>capacity ? capacity : (size_t)(size-pos); if(!pm_read(f,at+pos,b,n)) { buffer_result = (false); goto buffer_done; }
        if(pos<26 && pos+(int64_t)n>22) { size_t first=pos<22 ? (size_t)(22-pos) : 0U; size_t last=pos+(int64_t)n>26 ? (size_t)(26-pos) : n; xx_mem_zero(b+first,last-first); }
        xx_crc_context_update(&crc,b,n); pos+=(int64_t)n;
    } { buffer_result = ((uint32_t)xx_crc_context_final(&crc)==expected); goto buffer_done; }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    og_stream streams[256]; size_t count=0; int64_t at=0,limit=pm_available(f); uint8_t h[27],lace[255]; bool all_ended=false;
    xx_mem_zero(streams,sizeof(streams));
    while(at<limit) { uint32_t serial,seq,body=0; unsigned flags,n,i; size_t k; int64_t page; char name[64];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,h,27)) return false;
        if(xx_rt_memcmp(h,"OggS",4)) { if(all_ended) break; return false; }
        flags=h[5]; n=h[26]; if(h[4] || (flags&~7U) || !pm_read(f,at+27,lace,n)) return false;
        for(i=0;i<n;++i) { body+=lace[i]; } page=27+(int64_t)n+body;
        if(page>limit-at || !og_crc(f,at,page,xx_data_get_u32(h+22, 4, 0, false))) return false;
        serial=xx_data_get_u32(h+14, 4, 0, false); seq=xx_data_get_u32(h+18, 4, 0, false);
        for(k=0;k<count && streams[k].serial!=serial;++k) {}
        if(k==count) { if(count==256 || !(flags&2) || (flags&1) || seq) return false; streams[count++].serial=serial; }
        else if(streams[k].ended || (flags&2) || seq!=streams[k].sequence+1U || !!(flags&1)!=streams[k].continued) return false;
        streams[k].sequence=seq; streams[k].continued=n ? lace[n-1]==255 : streams[k].continued; streams[k].ended=(flags&4)!=0;
        if(streams[k].ended && streams[k].continued) return false;
        xx_rt_snprintf(name,sizeof(name),"page-%u-%u.lacing",serial,seq); if(!pm_add(f,s,name,at+27,n)) return false;
        xx_rt_snprintf(name,sizeof(name),"page-%u-%u.encoded",serial,seq); if(!pm_add(f,s,name,at+27+n,body)) return false;
        at+=page; all_ended=true; for(k=0;k<count;++k) if(!streams[k].ended) all_ended=false;
    }
    if(!count || !all_ended) { return false; } s->size=at; return true;
}

void xx_ogg_init(xx_ogg *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OGG,"ogg"); } }
xx_ogg *xx_ogg_create(xx_io_device *d,int64_t b) { xx_ogg *r=(xx_ogg *)xx_mem_alloc(sizeof(*r)); if(r) xx_ogg_init(r,d,b); return r; }
void xx_ogg_destroy(xx_ogg *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ogg_free(xx_ogg *r) { if(r) { xx_ogg_destroy(r); xx_mem_free(r); } }
bool xx_ogg_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ogg_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
