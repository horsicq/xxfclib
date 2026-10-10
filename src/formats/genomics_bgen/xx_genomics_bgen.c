/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.chg.ox.ac.uk/~gav/bgen_format/spec/v1.2.html */
#include "xxfclib/formats/genomics_bgen/xx_genomics_bgen.h"
#include "../common/xx_memory_blob.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24];memory_blob b={0};bool ok=false;uint32_t offset,header,variants,samples,flags,i;uint64_t at;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h+16,"bgen",4)) return false;
    offset=xx_data_get_u32(h, 4, 0, false);header=xx_data_get_u32(h+4, 4, 0, false);variants=xx_data_get_u32(h+8, 4, 0, false);samples=xx_data_get_u32(h+12, 4, 0, false);
    BLOB_NEED(header>=20 && header<=65536 && offset>=header && offset<=1048576 && variants && variants<=1024 && samples && samples<=1000000 && blob_load(f,&b,pd) && blob_span(&b,0,(uint64_t)offset+4));
    flags=xx_data_get_u32(b.p+header, 4, 0, false);BLOB_NEED((flags&0x7fffffffU)==4);at=(uint64_t)header+4;
    if(flags>>31) {uint64_t end;uint32_t count;BLOB_NEED(blob_span(&b,at,8));end=at+xx_data_get_u32(b.p+(size_t)at, 4, 0, false);count=xx_data_get_u32(b.p+(size_t)at+4, 4, 0, false);at+=8;BLOB_NEED(end<=offset+4U && count==samples);for(i=0;i<count;++i) BLOB_NEED(blob_string(&b,&at,end,2,false,false));BLOB_NEED(at==end);}
    BLOB_NEED(blob_zero(&b,at,(uint64_t)offset+4-at) && blob_add(f,s,&b,"header",0,(uint64_t)offset+4));at=(uint64_t)offset+4;
    for(i=0;i<variants;++i) {uint64_t start=at,n;uint32_t j;BLOB_NEED(blob_span(&b,at,4) && xx_data_get_u32(b.p+(size_t)at, 4, 0, false)==samples);at+=4;
        BLOB_NEED(blob_string(&b,&at,b.n,2,false,true) && blob_string(&b,&at,b.n,2,false,false) && blob_string(&b,&at,b.n,2,false,false) && blob_span(&b,at,4) && xx_data_get_u32(b.p+(size_t)at, 4, 0, false));at+=4;
        BLOB_NEED(blob_string(&b,&at,b.n,4,false,false) && blob_string(&b,&at,b.n,4,false,false));n=(uint64_t)samples*6;BLOB_NEED(blob_span(&b,at,n));
        for(j=0;j<samples;++j) {const uint8_t *p=b.p+(size_t)at+(size_t)j*6;uint32_t sum=(uint32_t)xx_data_get_u16(p, 2, 0, false)+xx_data_get_u16(p+2, 2, 0, false)+xx_data_get_u16(p+4, 2, 0, false);BLOB_NEED(!(j&4095U) ? !binary_stop(pd):true);BLOB_NEED(xx_data_get_u16(p, 2, 0, false)<=32768 && xx_data_get_u16(p+2, 2, 0, false)<=32768 && xx_data_get_u16(p+4, 2, 0, false)<=32768 && (!sum || (sum>=32767 && sum<=32769)));}
        at+=n;BLOB_NEED(blob_add(f,s,&b,"variant",start,at-start));
    }BLOB_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_genomics_bgen_init(xx_genomics_bgen *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_BGEN,"genomics_bgen"); } }
xx_genomics_bgen *xx_genomics_bgen_create(xx_io_device *d,int64_t b) { xx_genomics_bgen *r=(xx_genomics_bgen *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_bgen_init(r,d,b); return r; }
void xx_genomics_bgen_destroy(xx_genomics_bgen *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_bgen_free(xx_genomics_bgen *r) { if(r) { xx_genomics_bgen_destroy(r); xx_mem_free(r); } }
bool xx_genomics_bgen_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_bgen_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
