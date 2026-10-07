/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/3dstools/master/src/3dsxtool.cpp
 * 3DSX version0/flags0 with standard32-byte header and8-byte relocation headers, up to256MiB combined segments. Exports code/rodata/stored-data and bounded relocation tables. Extended SMDH/RomFS header, unknown relocation types, loading/relocation and execution unsupported.
 */
#include "xxfclib/formats/nintendo_3dsx/xx_nintendo_3dsx.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t g16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static XXFC_MAYBE_UNUSED uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f,uint64_t at,uint64_t end,bool empty) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return empty || i!=0; } return false;
}
static XXFC_MAYBE_UNUSED bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32],rh[24],e[4]; uint32_t sizes[3],counts[6],i,j,k,bss; uint64_t at,bytes,word,relocation_start;
    if(!pm_read(f,0,h,32) || xx_rt_memcmp(h,"3DSX",4) || xx_data_get_u16(h+4, 2, 0, false)!=32 || xx_data_get_u16(h+6, 2, 0, false)!=8 || xx_data_get_u32(h+8, 4, 0, false) || xx_data_get_u32(h+12, 4, 0, false) || !pm_read(f,32,rh,24)) return false;
    sizes[0]=xx_data_get_u32(h+16, 4, 0, false); sizes[1]=xx_data_get_u32(h+20, 4, 0, false); sizes[2]=xx_data_get_u32(h+24, 4, 0, false); bss=xx_data_get_u32(h+28, 4, 0, false);
    if(!sizes[0] || bss>sizes[2] || (uint64_t)sizes[0]+sizes[1]+sizes[2]>256U*1024U*1024U) return false;
    bytes=0; for(i=0;i<6;++i) { counts[i]=xx_data_get_u32(rh+i*4, 4, 0, false); if(counts[i]>1048576) return false; bytes+=(uint64_t)counts[i]*4; }
    at=56; relocation_start=at+(uint64_t)sizes[0]+sizes[1]+sizes[2]-bss;
    if(!span(at,relocation_start-at+bytes,(uint64_t)pm_available(f))) return false;
    for(i=0;i<3;++i) { char label[40]; uint32_t n=sizes[i]-(i==2 ? bss : 0); xx_rt_snprintf(label,sizeof(label),"%s.bin",i==0 ? "code" : i==1 ? "rodata" : "data");
        if((pd && xx_pd_is_stopped(pd)) || (n && !emit(f,s,label,at,n,relocation_start+bytes))) { return false; } at+=n; }
    for(i=0;i<3;++i) for(j=0;j<2;++j) { char label[40]; uint64_t start=at; word=0;
        for(k=0;k<counts[i*2+j];++k) { uint32_t skip,patch;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)at,e,4)) { return false; } at+=4; skip=xx_data_get_u16(e, 2, 0, false); patch=xx_data_get_u16(e+2, 2, 0, false);
            if((!patch && !skip) || word+skip>(uint64_t)sizes[i]/4 || patch>(uint64_t)sizes[i]/4-word-skip) { return false; } word+=skip+patch; }
        if(at>start) { xx_rt_snprintf(label,sizeof(label),"segment-%u-%s-relocations.bin",i,j ? "relative" : "absolute"); if(!emit(f,s,label,start,at-start,at)) return false; }
    }
    s->size=(int64_t)at; return true;

}

void xx_nintendo_3dsx_init(xx_nintendo_3dsx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_3DSX,"3dsx"); } }
xx_nintendo_3dsx *xx_nintendo_3dsx_create(xx_io_device *d,int64_t b) { xx_nintendo_3dsx *r=(xx_nintendo_3dsx *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_3dsx_init(r,d,b); return r; }
void xx_nintendo_3dsx_destroy(xx_nintendo_3dsx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_3dsx_free(xx_nintendo_3dsx *r) { if(r) { xx_nintendo_3dsx_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_3dsx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_3dsx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
