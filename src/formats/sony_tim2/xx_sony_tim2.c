/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/GirianSeed/tim2/trunk/sample/tim2.h
 * TIM2 version4/format0, up to1024 single-level images with RGB16/RGB24/RGB32 or4/8-bit indexed texture data and matching CLUT. Exports encoded image/CLUT planes; no mipmaps, extended headers, swizzle reversal or rendering.
 */
#include "xxfclib/formats/sony_tim2/xx_sony_tim2.h"
#include "../xx_payload_members.h"

static XXFC_MAYBE_UNUSED uint16_t g16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static XXFC_MAYBE_UNUSED uint32_t g32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
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

    uint8_t h[16],p[48]; uint32_t count,i,j; uint64_t at=16,total=(uint64_t)pm_available(f); char label[40];
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"TIM2",4) || h[4]!=4 || h[5] || !(count=pm_le16(h+6)) || count>1024) return false;
    for(j=8;j<16;++j) if(h[j]) return false;
    for(i=0;i<count;++i) {
        uint32_t size,clut,image,type,palette,w,he,colors; uint64_t wanted;
        if((pd && xx_pd_is_stopped(pd)) || !span(at,48,total) || !pm_read(f,(int64_t)at,p,48)) return false;
        size=pm_le32(p); clut=pm_le32(p+4); image=pm_le32(p+8); colors=pm_le16(p+14); palette=p[18]&63U; type=p[19]; w=pm_le16(p+20); he=pm_le16(p+22);
        if(pm_le16(p+12)!=48 || p[16] || p[17]!=1 || !w || !he || type<1 || type>5 || !span(at,size,total) || (uint64_t)size!=48U+(uint64_t)image+clut) return false;
        wanted=(uint64_t)w*he; wanted=type==1 ? wanted*2 : type==2 ? wanted*3 : type==3 ? wanted*4 : type==4 ? (wanted+1)/2 : wanted;
        if(image!=wanted || !image) return false;
        if(type<=3) { if(clut || colors || p[18]) return false; }
        else { uint32_t entries=type==4 ? 16 : 256,bytes=palette==1 ? 2 : palette==2 ? 3 : palette==3 ? 4 : 0;
            if((p[18]&0x40) || !bytes || colors!=entries || clut!=entries*bytes) return false; }
        xx_rt_snprintf(label,sizeof(label),"image-%u.bin",i); if(!emit(f,s,label,at+48,image,total)) return false;
        if(clut) { xx_rt_snprintf(label,sizeof(label),"palette-%u.bin",i); if(!emit(f,s,label,at+48+image,clut,total)) return false; } at+=size;
    }
    s->size=(int64_t)at; return true;

}

void xx_sony_tim2_init(xx_sony_tim2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_TIM2,"tm2"); } }
xx_sony_tim2 *xx_sony_tim2_create(xx_io_device *d,int64_t b) { xx_sony_tim2 *r=(xx_sony_tim2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_sony_tim2_init(r,d,b); return r; }
void xx_sony_tim2_destroy(xx_sony_tim2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sony_tim2_free(xx_sony_tim2 *r) { if(r) { xx_sony_tim2_destroy(r); xx_mem_free(r); } }
bool xx_sony_tim2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sony_tim2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
