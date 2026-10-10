/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * References: Chromium components/crx_file/crx3.proto and crx_verifier.cc
 * CRX3 framing: exports protobuf header and validated nested ZIP archive.
 * Signature fields are structurally checked; trust/signatures are not verified.
 */
#include "xxfclib/formats/crx/xx_crx.h"
#include "xxfclib/formats/zip/xx_zip.h"
#include "../common/xx_carrier_helpers.h"

/* CRX ZIP offsets are relative to the ZIP payload, with no SFX rebasing.
 * This subset refuses ZIP64 and multidisk archives; streams stay encoded. */
static bool crx_zip_origin(Abstractformat *f,int64_t start,int64_t end,xx_pd_struct *pd) {
    uint8_t h[22]; int64_t at=end-22,low=end-start>65557 ? end-65557 : start;
    for(;at>=low;--at) {
        if(carrier_stop(pd) || !pm_read(f,at,h,4)) return false;
        if(xx_rt_memcmp(h,"PK\5\6",4) || !pm_read(f,at,h,22) || at+22+xx_data_get_u16(h+20, 2, 0, false)!=end) continue;
        return (uint64_t)xx_data_get_u32(h+16, 4, 0, false)+xx_data_get_u32(h+12, 4, 0, false)==(uint64_t)(at-start);
    }
    return false;
}

static bool crx_varint(const uint8_t *p,size_t n,size_t *at,uint64_t *out) {
    unsigned i; uint64_t value=0;
    for(i=0;i<10;++i) {
        uint8_t c; if(*at>=n) return false; c=p[(*at)++]; if(i==9 && c>1) return false;
        value|=(uint64_t)(c&127)<<(7*i); if(!(c&128)) { *out=value; return true; }
    }
    return false;
}
/* mode0=CrxFileHeader, mode1=AsymmetricKeyProof, mode2=SignedData. */
static bool crx_proto(const uint8_t *p,size_t n,unsigned mode,xx_pd_struct *pd) {
    size_t at=0; bool first=false,second=false; unsigned fields=0;
    while(at<n) {
        uint64_t key,value,length=0; size_t data; unsigned wire; uint32_t field;
        if(++fields>4096 || (pd && xx_pd_is_stopped(pd)) || !crx_varint(p,n,&at,&key) || !key || key>>3>0x1fffffffU) return false;
        field=(uint32_t)(key>>3); wire=(unsigned)(key&7);
        if(wire==0) { if(!crx_varint(p,n,&at,&value)) return false; }
        else if(wire==1) { if(n-at<8) return false; at+=8; }
        else if(wire==5) { if(n-at<4) return false; at+=4; }
        else if(wire==2) {
            if(!crx_varint(p,n,&at,&length) || length>n-at) { return false; } data=at; at+=(size_t)length;
            if(mode==0 && (field==2 || field==3)) { if(!crx_proto(p+data,(size_t)length,1,pd)) return false; first=true; }
            else if(mode==0 && field==10000) { if(second || !crx_proto(p+data,(size_t)length,2,pd)) return false; second=true; }
            else if(mode==1 && field==1) { if(first || !length) return false; first=true; }
            else if(mode==1 && field==2) { if(second || !length) return false; second=true; }
            else if(mode==2 && field==1) { if(first || length!=16) return false; first=true; }
        } else return false;
        if(mode==0 && (field==2 || field==3 || field==10000) && wire!=2) return false;
        if(mode==1 && (field==1 || field==2) && wire!=2) return false;
        if(mode==2 && field==1 && wire!=2) return false;
    }
    return mode==2?first:(first && second);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],*header=NULL; uint32_t size; int64_t at,length; bool ok=false;
    xx_io_device *view=NULL; xx_zip *zip=NULL; xx_io_volume volume; xx_archive_record_state *records=NULL;
    const xx_var *budget=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    if(!pm_read(f,0,h,12) || xx_rt_memcmp(h,"Cr24",4) || xx_data_get_u32(h+4, 4, 0, false)!=3) return false;
    size=xx_data_get_u32(h+8, 4, 0, false); at=12+(int64_t)size;
    if(!size || size>1024U*1024U || at>pm_available(f) || (budget && size>xx_var_get_u64(budget))) return false;
    header=(uint8_t *)xx_mem_alloc(size); if(!header || !pm_read(f,12,header,size) || !crx_proto(header,size,0,pd)) goto done;
    if(!pm_read(f,at,h,4) || xx_rt_memcmp(h,"PK\3\4",4)) goto done;
    if(!crx_zip_origin(f,at,pm_available(f),pd) || !carrier_zip(f,at,pm_available(f),pd)) goto done;
    volume.device=f->device; volume.offset=f->base_address+at; volume.size=pm_available(f)-at;
    view=xx_io_multivolume_open(&volume,1,false); if(!view) goto done;
    zip=xx_zip_create(view,0); if(!zip || !xx_zip_handle_base_info(xx_zip_to_format(zip),pd)) goto done;
    length=xx_zip_get_format_size(xx_zip_to_format(zip),pd);
    if(length<=0 || length!=volume.size || !xx_zip_get_number_of_archive_records(xx_zip_to_format(zip),pd) ||
       zip->number_of_records>65536 || zip->cd_offset<0 || zip->cd_size<0 || zip->cd_offset>zip->eocd_offset ||
       zip->cd_size>zip->eocd_offset-zip->cd_offset) goto done;
    records=xx_zip_create_archive_records_reading(xx_zip_to_format(zip),NULL,pd);
    if(!records) goto done;
    {
        uint64_t count=0;
        while(xx_zip_get_current_archive_record(xx_zip_to_format(zip),records)) {
            if((pd && xx_pd_is_stopped(pd)) || ++count>zip->number_of_records) goto done;
            if(!xx_zip_archive_record_move_to_next(xx_zip_to_format(zip),records,pd)) break;
        }
        if(count!=zip->number_of_records) goto done;
    }
    if(!pm_add(f,s,"crx-header.protobuf",12,size) || !pm_add(f,s,"extension.zip",at,length)) goto done;
    s->size=at+length; ok=true;
done:
    if(records) xx_zip_free_archive_records_reading(xx_zip_to_format(zip),records);
    if(zip) { xx_zip_free(zip); } if(view) xx_io_close(view); if(header) xx_mem_free(header);
    return ok && (!pd || !xx_pd_is_stopped(pd));
}

void xx_crx_init(xx_crx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CRX,"crx"); } }
xx_crx *xx_crx_create(xx_io_device *d,int64_t b) { xx_crx *r=(xx_crx *)xx_mem_alloc(sizeof(*r)); if(r) xx_crx_init(r,d,b); return r; }
void xx_crx_destroy(xx_crx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_crx_free(xx_crx *r) { if(r) { xx_crx_destroy(r); xx_mem_free(r); } }
bool xx_crx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_crx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
