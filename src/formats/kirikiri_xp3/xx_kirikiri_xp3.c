/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/krkrz/krkrz/blob/master/base/XP3Archive.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/kirikiri_xp3/xx_kirikiri_xp3.h"
#include "xxfclib/global/xx_global.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static bool xp_adler(Abstractformat *f,int64_t at,int64_t bytes,uint32_t expected,xx_pd_struct *pd) {
    size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false; uint32_t a=1,c=0; while(bytes) {if(!b) { if((uint64_t)(bytes)<capacity) capacity=(size_t)(bytes); b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result=false; goto buffer_done; } }  size_t n=(uint64_t)(bytes)>capacity ? capacity : (size_t)bytes,i; if(wg_stop(pd) || !pm_read(f,at,b,n)) { buffer_result = (false); goto buffer_done; }
        for(i=0;i<n;++i) { a=(a+b[i])%65521U; c=(c+a)%65521U; } at+=n; bytes-=n; } { buffer_result = ((c<<16|a)==expected); goto buffer_done; }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const uint8_t magic[11]={0x58,0x50,0x33,0x0d,0x0a,0x20,0x0a,0x1a,0x8b,0x67,1}; uint8_t h[32]; uint64_t index,n; int64_t at,end,extent,limit=pm_available(f);
    if(!pm_read(f,0,h,19) || xx_rt_memcmp(h,magic,11) || (index=wg64(h+11))<19 || !wg_range(limit,index,9) || !pm_read(f,(int64_t)index,h,9) || h[0] || !(n=wg64(h+1)) || n>16777216 || !wg_range(limit,index+9,n)) return false;
    at=(int64_t)index+9; end=at+(int64_t)n; extent=end;
    while(at<end) { int64_t fileend,p; uint64_t size,raw=0,packed=0,offset=0; uint32_t check=0; unsigned mask=0; char label[48];
        if(wg_stop(pd) || end-at<12 || !pm_read(f,at,h,12) || xx_rt_memcmp(h,"File",4) || (size=wg64(h+4))>(uint64_t)(end-at-12)) { return false; } p=at+12; fileend=p+(int64_t)size;
        while(p<fileend) { uint64_t bytes; int64_t body; if(fileend-p<12 || !pm_read(f,p,h,12) || (bytes=wg64(h+4))>(uint64_t)(fileend-p-12)) return false; body=p+12;
            if(!xx_rt_memcmp(h,"info",4)) { uint16_t names; if(mask&1 || bytes<22 || !pm_read(f,body,h,22) || pm_le32(h)) return false; raw=wg64(h+4); packed=wg64(h+12); names=pm_le16(h+20); if(!names || names>4096 || bytes!=22U+2U*names) return false; mask|=1; }
            else if(!xx_rt_memcmp(h,"segm",4)) { if(mask&2 || bytes!=28 || !pm_read(f,body,h,28) || pm_le32(h)) return false; offset=wg64(h+4); if(wg64(h+12)!=wg64(h+20)) return false; n=wg64(h+12); mask|=2; }
            else if(!xx_rt_memcmp(h,"adlr",4)) { if(mask&4 || bytes!=4 || !pm_read(f,body,h,4)) return false; check=pm_le32(h); mask|=4; }
            else { return false; } p=body+(int64_t)bytes;
        }
        if(mask!=7 || raw!=packed || raw!=n || offset<19 || !wg_range(limit,offset,n) || (offset<(uint64_t)end && offset+n>index) || !xp_adler(f,(int64_t)offset,(int64_t)n,check,pd)) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",(unsigned)s->count); if(!pm_add(f,s,label,(int64_t)offset,(int64_t)n)) return false; if((int64_t)(offset+n)>extent) extent=(int64_t)(offset+n); at=fileend;
    } s->size=extent; return s->count>0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_kirikiri_xp3_init(xx_kirikiri_xp3 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_KIRIKIRI_XP3,"xp3"); } }
xx_kirikiri_xp3 *xx_kirikiri_xp3_create(xx_io_device *d,int64_t b) { xx_kirikiri_xp3 *r=(xx_kirikiri_xp3 *)xx_mem_alloc(sizeof(*r)); if(r) xx_kirikiri_xp3_init(r,d,b); return r; }
void xx_kirikiri_xp3_destroy(xx_kirikiri_xp3 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_kirikiri_xp3_free(xx_kirikiri_xp3 *r) { if(r) { xx_kirikiri_xp3_destroy(r); xx_mem_free(r); } }
bool xx_kirikiri_xp3_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_kirikiri_xp3_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
