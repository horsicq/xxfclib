/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/aboood40091/BNTX-Injector/master/structs.py
 * Little-endian BNTX versions0.4.0.0/0.4.1.0 NX 2D single-layer textures; exports bounded encoded mip slices. Validates BRTI/BRTD pointers, names, dimensions and mip extents. No sparse/multisample/array/runtime-relocated variants, swizzle reversal or pixel decoding; relocation bytes are not applied.
 */
#include "xxfclib/formats/nintendo_bntx/xx_nintendo_bntx.h"
#include "../xx_payload_members.h"

static uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),b=(uint64_t)s->items[i].size;
        if(n && b && at<a+b && a<at+n) return false; }
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[88],p[8],b[160],n[2],datah[16]; uint32_t total,count,i,version,reloc; uint64_t table,data,limit,data_end; char label[40];
    if(!pm_read(f,0,h,88) || xx_rt_memcmp(h,"BNTX\0\0\0\0",8) || h[12]!=0xff || h[13]!=0xfe || h[14]>16 || h[15]!=0x40 || pm_le16(h+20) || xx_rt_memcmp(h+32,"NX  ",4)) return false;
    version=pm_le32(h+8); total=pm_le32(h+28); count=pm_le32(h+36); reloc=pm_le32(h+24); table=g64(h+40,false); data=g64(h+48,false);
    if((version!=0x40000 && version!=0x40100) || total<88 || total>(uint64_t)pm_available(f) || !count || count>1024 || table<88 || !span(table,(uint64_t)count*8,total) || data<table+(uint64_t)count*8 || !span(data,16,total) || !pm_read(f,(int64_t)data,datah,16) || xx_rt_memcmp(datah,"BRTD",4)) return false;
    if(g64(h+64,false) || g64(h+72,false) || pm_le32(h+80)) return false;
    if(pm_le32(h+16)<2 || !span(pm_le32(h+16)-2,2,total) || !pm_read(f,pm_le32(h+16)-2,n,2) || !span(pm_le32(h+16),pm_le16(n)+1,total) || !zname(f,pm_le32(h+16),pm_le32(h+16)+pm_le16(n)+1)) return false;
    limit=reloc ? reloc : total;
    if(pm_le32(datah+8)<16 || !span(data,pm_le32(datah+8),limit)) return false; data_end=data+pm_le32(datah+8);
    if(reloc && (reloc<data+16 || !span(reloc,16,total) || !pm_read(f,reloc,datah,16) || xx_rt_memcmp(datah,"_RLT",4) || pm_le32(datah+4)!=reloc || pm_le32(datah+8)>16)) return false;
    for(i=0;i<count;++i) { uint64_t info,ptrs,name,first,last,image; uint32_t w,he,mips,j,alignment;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(table+i*8),p,8)) return false; info=g64(p,false);
        if(info<88 || !span(info,160,data) || !pm_read(f,(int64_t)info,b,160) || xx_rt_memcmp(b,"BRTI",4) || pm_le32(b+8)<160 || !span(info,pm_le32(b+8),data)) return false;
        w=pm_le32(b+36); he=pm_le32(b+40); mips=pm_le16(b+22); image=pm_le32(b+80); alignment=pm_le32(b+84); name=g64(b+96,false); ptrs=g64(b+112,false);
        if((b[16]&~1U) || b[17]!=2 || pm_le16(b+18)>1 || !mips || mips>16 || pm_le16(b+24)!=1 || !w || !he || w>16384 || he>16384 || pm_le32(b+44)!=1 || pm_le32(b+48)!=1 || !pm_le32(b+28) || !image || !alignment || (alignment&(alignment-1)) || alignment>65536 || name<88 || !span(name,2,data) || !pm_read(f,(int64_t)name,n,2) || !span(name+2,pm_le16(n)+1,data) || !zname(f,name+2,name+3+pm_le16(n)) || ptrs<88 || !span(ptrs,(uint64_t)mips*8,data)) return false;
        if(!pm_read(f,(int64_t)ptrs,p,8)) return false; first=g64(p,false); if(first<data+16 || first%alignment || !span(first,image,data_end)) return false; last=first;
        for(j=0;j<mips;++j) { uint64_t next=first+image;
            if(pd && xx_pd_is_stopped(pd)) return false;
            if(j+1<mips) { if(!pm_read(f,(int64_t)(ptrs+(j+1)*8),p,8)) return false; next=g64(p,false); }
            if(next<=last || next>first+image) return false;
            xx_rt_snprintf(label,sizeof(label),"texture-%u-mip-%u.bin",i,j); if(!emit(f,s,label,last,next-last,total)) return false; last=next; }
    }
    s->size=total; return true;

}

void xx_nintendo_bntx_init(xx_nintendo_bntx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BNTX,"bntx"); } }
xx_nintendo_bntx *xx_nintendo_bntx_create(xx_io_device *d,int64_t b) { xx_nintendo_bntx *r=(xx_nintendo_bntx *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_bntx_init(r,d,b); return r; }
void xx_nintendo_bntx_destroy(xx_nintendo_bntx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_bntx_free(xx_nintendo_bntx *r) { if(r) { xx_nintendo_bntx_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_bntx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_bntx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
