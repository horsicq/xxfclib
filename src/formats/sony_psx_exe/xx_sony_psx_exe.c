/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://psx-spx.consoledev.net/cdromfileformats/#filenameexe-general-purpose-executable
 * PS-X EXE with cached 2MiB RAM load addresses and sector-aligned code/data. Exports the 2048-byte header and stored load image; no relocation, loading, self-unpacking, BIOS calls or execution.
 */
#include "xxfclib/formats/sony_psx_exe/xx_sony_psx_exe.h"
#include "../xx_payload_members.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[2048]; uint32_t pc,address,n,bss,bssn,sp,i; uint64_t total;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"PS-X EXE",8)) return false;
    for(i=8;i<16;++i) { if(h[i]) return false; } for(i=56;i<76;++i) if(h[i]) return false;
    pc=pm_le32(h+16); address=pm_le32(h+24); n=pm_le32(h+28); bss=pm_le32(h+40); bssn=pm_le32(h+44); sp=pm_le32(h+48);
    if(!n || (n&2047) || (address&3) || address<0x80000000U || !span((uint64_t)address-0x80000000U,n,0x200000) || pc<address || pc-address>=n || (pc&3) || pm_le32(h+32) || pm_le32(h+36)) return false;
    if(bssn && ((bss&3) || (bssn&3) || bss<0x80000000U || !span((uint64_t)bss-0x80000000U,bssn,0x200000) || overlap(address,n,bss,bssn))) return false;
    if(sp && (sp<0x80000000U || (uint64_t)sp+(int64_t)(int32_t)pm_le32(h+52)<0x80000000U || (uint64_t)sp+(int64_t)(int32_t)pm_le32(h+52)>0x80200000U)) return false;
    total=2048U+(uint64_t)n; if(stop(pd) || total>(uint64_t)pm_available(f) || !emit(f,s,"executable-header.bin",0,2048,total) || !emit(f,s,"load-image.bin",2048,n,total)) return false; s->size=(int64_t)total; return true;

}

void xx_sony_psx_exe_init(xx_sony_psx_exe *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_PSX_EXE,"exe"); } }
xx_sony_psx_exe *xx_sony_psx_exe_create(xx_io_device *d,int64_t b) { xx_sony_psx_exe *r=(xx_sony_psx_exe *)xx_mem_alloc(sizeof(*r)); if(r) xx_sony_psx_exe_init(r,d,b); return r; }
void xx_sony_psx_exe_destroy(xx_sony_psx_exe *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sony_psx_exe_free(xx_sony_psx_exe *r) { if(r) { xx_sony_psx_exe_destroy(r); xx_mem_free(r); } }
bool xx_sony_psx_exe_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sony_psx_exe_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
