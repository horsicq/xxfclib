/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/DiscIO/FileSystemGCWii.cpp
 * GameCube disc images with bounded DOL and FST. Exports DOL, optional apploader and numbered FST files. Directory/name bounds and nested directory ranges checked; Wii encrypted partitions, rendering and execution unsupported.
 */
#include "xxfclib/formats/nintendo_gcm/xx_nintendo_gcm.h"
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
static bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[0x2460],dol[256],e[12]; uint64_t fst,fs,doloff,dolsize=256,end,total=(uint64_t)pm_available(f),app; uint32_t count,i,stack[65],ends[65],depth=0; char label[40];
    if(!pm_read(f,0,h,sizeof(h)) || pm_be32(h+0x1c)!=0xc2339f3d || pm_be32(h+0x18)) return false;
    doloff=pm_be32(h+0x420); fst=pm_be32(h+0x424); fs=pm_be32(h+0x428);
    if(doloff<sizeof(h) || fst<sizeof(h) || fs<12 || fs>16U*1024U*1024U || pm_be32(h+0x42c)<fs || !span(fst,fs,total) || !pm_read(f,(int64_t)doloff,dol,sizeof(dol))) return false;
    for(i=0;i<18;++i) { uint64_t at=pm_be32(dol+i*4),n=pm_be32(dol+0x90+i*4); unsigned j; if(!n) { if(at) return false; continue; }
        if(at<256 || !span(doloff+at,n,total)) return false;
        for(j=0;j<i;++j) { uint64_t a=pm_be32(dol+j*4),b=pm_be32(dol+0x90+j*4); if(b && at<a+b && a<at+n) return false; }
        if(at+n>dolsize) dolsize=at+n; }
    if(dolsize==256 || (doloff<fst+fs && fst<doloff+dolsize) || !emit(f,s,"main.dol",doloff,dolsize,total)) return false;
    app=(uint64_t)pm_be32(h+0x2454)+pm_be32(h+0x2458)+32;
    if(app>32 && (!span(0x2440,app,total) || 0x2440+app>doloff || 0x2440+app>fst || !emit(f,s,"apploader.bin",0x2440,app,total))) return false;
    if(!pm_read(f,(int64_t)fst,e,12) || pm_be32(e)!=0x1000000 || pm_be32(e+4)) return false;
    count=pm_be32(e+8); if(!count || count>4094 || (uint64_t)count*12>fs) return false;
    end=fst+fs; if(doloff+dolsize>end) end=doloff+dolsize; stack[0]=0; ends[0]=count;
    for(i=1;i<count;++i) { uint32_t a,b,c; uint64_t name;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(fst+i*12),e,12)) return false;
        while(depth && i>=ends[depth]) --depth;
        a=pm_be32(e); b=pm_be32(e+4); c=pm_be32(e+8); name=fst+(uint64_t)count*12+(a&0xffffff);
        if((a>>24)>1 || !zname(f,name,fst+fs)) return false;
        if(a>>24) { if(b!=stack[depth] || c<=i || c>ends[depth] || depth>=64) return false; ++depth; stack[depth]=i; ends[depth]=c; }
        else { if(b<sizeof(h) || !span(b,c,total) || (b<fst+fs && fst<(uint64_t)b+c)) return false;
            xx_rt_snprintf(label,sizeof(label),"file-%u.bin",i); if(!emit(f,s,label,b,c,total)) return false; if((uint64_t)b+c>end) end=(uint64_t)b+c; } }
    s->size=(int64_t)end; return true;

}

void xx_nintendo_gcm_init(xx_nintendo_gcm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_GCM,"gcm"); } }
xx_nintendo_gcm *xx_nintendo_gcm_create(xx_io_device *d,int64_t b) { xx_nintendo_gcm *r=(xx_nintendo_gcm *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_gcm_init(r,d,b); return r; }
void xx_nintendo_gcm_destroy(xx_nintendo_gcm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_gcm_free(xx_nintendo_gcm *r) { if(r) { xx_nintendo_gcm_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_gcm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_gcm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
