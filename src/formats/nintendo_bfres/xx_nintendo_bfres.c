/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/KillzXGaming/BfresLibrary/master/BfresLibrary/WiiU/ResFileParser.cs
 * Wii U big-endian FRES versions2.4 through4.x, attachment-only files with up to1024 external resources. Validates relative dictionary/name/data pointers and declared string pool. Exports attachment bytes; models/textures/animations, Switch FRES, relocation and rendering unsupported.
 */
#include "xxfclib/formats/nintendo_bfres/xx_nintendo_bfres.h"
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
static bool zname(Abstractformat *f,uint64_t at,uint64_t end,bool empty) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return empty || i!=0; } return false;
}
static XXFC_MAYBE_UNUSED bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[108],d[8],e[16],p[8]; uint32_t total,count,dict,ds,pool,ps,i; int64_t target; uint64_t metadata_end,descriptors[1024]; char label[40];
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"FRES",4) || h[8]!=0xfe || h[9]!=0xff || xx_data_get_u16(h+10, 2, 0, true)!=16 || xx_data_get_u32(h+4, 4, 0, true)<0x02040000 || xx_data_get_u32(h+4, 4, 0, true)>=0x05000000) return false;
    total=xx_data_get_u32(h+12, 4, 0, true); ps=xx_data_get_u32(h+24, 4, 0, true); count=xx_data_get_u16(h+102, 2, 0, true);
    if(total<108 || total>(uint64_t)pm_available(f) || !count || count>1024 || !xx_data_get_u32(h+16, 4, 0, true) || (xx_data_get_u32(h+16, 4, 0, true)&(xx_data_get_u32(h+16, 4, 0, true)-1)) || xx_data_get_u32(h+16, 4, 0, true)>65536 || xx_data_get_u32(h+104, 4, 0, true)) return false;
    for(i=0;i<11;++i) if(xx_data_get_u32(h+32+i*4, 4, 0, true) || xx_data_get_u16(h+80+i*2, 2, 0, true)) return false;
    target=28+(int64_t)(int32_t)xx_data_get_u32(h+28, 4, 0, true); if(target<108 || !span((uint64_t)target,ps,total) || !ps) return false; pool=(uint32_t)target;
    target=20+(int64_t)(int32_t)xx_data_get_u32(h+20, 4, 0, true); if(target<pool || !zname(f,(uint64_t)target,(uint64_t)pool+ps,false)) return false;
    target=76+(int64_t)(int32_t)xx_data_get_u32(h+76, 4, 0, true); if(target<108 || !span((uint64_t)target,8,total) || !pm_read(f,target,d,8)) return false; dict=(uint32_t)target; ds=xx_data_get_u32(d, 4, 0, true);
    if(xx_data_get_u32(d+4, 4, 0, true)!=count || ds!=8U+16U*(count+1U) || !span(dict,ds,total) || overlap(dict,ds,pool,ps)) return false;
    metadata_end=dict+ds; if((uint64_t)pool+ps>metadata_end) metadata_end=(uint64_t)pool+ps;
    for(i=0;i<=count;++i) {
        uint64_t node=(uint64_t)dict+8+i*16,external,data; uint32_t size;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)node,e,16) || xx_data_get_u16(e+4, 2, 0, true)>count || xx_data_get_u16(e+6, 2, 0, true)>count) return false;
        if(!i) { if(xx_data_get_u32(e+12, 4, 0, true)) return false; continue; }
        target=(int64_t)node+8+(int64_t)(int32_t)xx_data_get_u32(e+8, 4, 0, true); if(target<pool || !zname(f,(uint64_t)target,(uint64_t)pool+ps,false)) return false;
        target=(int64_t)node+12+(int64_t)(int32_t)xx_data_get_u32(e+12, 4, 0, true); if(target<108 || !span((uint64_t)target,8,total) || overlap((uint64_t)target,8,dict,ds) || overlap((uint64_t)target,8,pool,ps) || !pm_read(f,target,p,8)) return false; external=(uint64_t)target;
        { uint32_t j; for(j=0;j+1<i;++j) if(overlap(external,8,descriptors[j],8)) return false; descriptors[i-1]=external; }
        target=(int64_t)external+(int64_t)(int32_t)xx_data_get_u32(p, 4, 0, true); size=xx_data_get_u32(p+4, 4, 0, true); if(target<(int64_t)metadata_end || !size || !span((uint64_t)target,size,total) || overlap((uint64_t)target,size,external,8)) return false; data=(uint64_t)target;
        xx_rt_snprintf(label,sizeof(label),"attachment-%u.bin",i-1); if(!emit(f,s,label,data,size,total)) return false;
    }
    for(i=1;i<=count;++i) { uint64_t node=(uint64_t)dict+8+i*16; size_t j;
        if(!pm_read(f,(int64_t)node,e,16)) { return false; } target=(int64_t)node+12+(int64_t)(int32_t)xx_data_get_u32(e+12, 4, 0, true);
        for(j=0;j<s->count;++j) if(overlap((uint64_t)target,8,(uint64_t)(s->items[j].offset-f->base_address),(uint64_t)s->items[j].size)) return false; }
    s->size=total; return true;

}

void xx_nintendo_bfres_init(xx_nintendo_bfres *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BFRES,"bfres"); } }
xx_nintendo_bfres *xx_nintendo_bfres_create(xx_io_device *d,int64_t b) { xx_nintendo_bfres *r=(xx_nintendo_bfres *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_bfres_init(r,d,b); return r; }
void xx_nintendo_bfres_destroy(xx_nintendo_bfres *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_bfres_free(xx_nintendo_bfres *r) { if(r) { xx_nintendo_bfres_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_bfres_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_bfres_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
