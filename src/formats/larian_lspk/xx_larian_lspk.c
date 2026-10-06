/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Norbyte/lslib/master/LSLib/LS/PackageReader.cs
 * LSPK version10 single-part archives with uncompressed file table and stored members. Checks optional CRC32. Solid, multi-part, compressed members and later LZ4-index package versions rejected.
 */
#include "xxfclib/formats/larian_lspk/xx_larian_lspk.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),b=(uint64_t)s->items[i].size;
        if(n && b && at<a+b && a<at+n) return false; }
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}
static XXFC_MAYBE_UNUSED uint16_t crc16(const uint8_t *p,size_t n) { return xx_crc16_modbus_calc(UINT16_MAX,p,n); }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[24],e[280]; uint32_t data,list,count,i; uint64_t total=(uint64_t)pm_available(f),end;
    if(!pm_read(f,0,h,24) || xx_rt_memcmp(h,"LSPK",4) || pm_le32(h+4)!=10 || pm_le16(h+16)!=1 || h[18]) return false;
    data=pm_le32(h+8); list=pm_le32(h+12); count=pm_le32(h+20);
    if(!count || count>4096 || list!=(uint64_t)count*280 || data<24+(uint64_t)list || data>total) { return false; } end=data;
    for(i=0;i<count;++i) { uint64_t at,n,crc; uint32_t j; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,24+(int64_t)i*280,e,280)) return false;
        for(j=0;j<256 && e[j];++j) {} if(!j || j==256 || pm_le32(e+264) || pm_le32(e+268) || pm_le32(e+272)) return false;
        at=(uint64_t)data+pm_le32(e+256); n=pm_le32(e+260);
        if(!span(at,n,total)) return false;
        if(pm_le32(e+276) && (!xx_crc_calculate_device_by_type(f->device,f->base_address+(int64_t)at,(int64_t)n,XX_CRC_TYPE_CRC32,pd,&crc) || crc!=pm_le32(e+276))) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",i); if(!emit(f,s,label,at,n,total)) return false; if(at+n>end) end=at+n; }
    s->size=(int64_t)end; return true;

}

void xx_larian_lspk_init(xx_larian_lspk *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LARIAN_LSPK,"pak"); } }
xx_larian_lspk *xx_larian_lspk_create(xx_io_device *d,int64_t b) { xx_larian_lspk *r=(xx_larian_lspk *)xx_mem_alloc(sizeof(*r)); if(r) xx_larian_lspk_init(r,d,b); return r; }
void xx_larian_lspk_destroy(xx_larian_lspk *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_larian_lspk_free(xx_larian_lspk *r) { if(r) { xx_larian_lspk_destroy(r); xx_mem_free(r); } }
bool xx_larian_lspk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_larian_lspk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
