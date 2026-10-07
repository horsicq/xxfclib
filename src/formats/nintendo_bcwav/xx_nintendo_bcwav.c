/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/vgmstream/vgmstream/master/src/meta/bfwav.c
 * CWAV versions0x102/0x10102, either BOM, one or two PCM8/PCM16 channels. Exports INFO and exact encoded channel bytes; validates channel/data references and disjoint extents. ADPCM/IMA and playback unsupported.
 */
#include "xxfclib/formats/nintendo_bcwav/xx_nintendo_bcwav.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint16_t g16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
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
static bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool audio_blocks(Abstractformat *f,const char *magic,uint32_t version1,uint32_t version2,uint16_t first,
                         uint32_t *offsets,uint32_t *sizes,bool *be,uint32_t *total,xx_pd_struct *pd) {
    uint8_t h[20],e[12],b[8]; uint32_t count,header,i,j,seen=0,version;
    if(!pm_read(f,0,h,20) || xx_rt_memcmp(h,magic,4) || !bom(h+4,be)) return false;
    version=g32(h+8,*be); header=g16(h+6,*be); *total=g32(h+12,*be); count=g16(h+16,*be);
    if((version!=version1 && version!=version2) || count<2 || count>(first==0x4000 ? 4U : 2U) || header<20+12*count || (header&31) || header>*total || *total>(uint64_t)pm_available(f) || g16(h+18,*be)) return false;
    for(i=0;i<count;++i) {
        uint32_t k,at,n; const char *sig;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,20+(int64_t)i*12,e,12)) return false;
        k=g16(e,*be); at=g32(e+4,*be); n=g32(e+8,*be);
        if(k<first || k>first+(first==0x4000 ? 3U : 1U)) { return false; } k-=first;
        sig=k==0 ? "INFO" : first==0x7000 ? "DATA" : k==1 ? "SEEK" : k==2 ? "DATA" : "REGN";
        if((seen&(1U<<k)) || g16(e+2,*be) || at<header || (at&31) || n<8 || !span(at,n,*total) || !pm_read(f,at,b,8) || xx_rt_memcmp(b,sig,4) || g32(b+4,*be)!=n) return false;
        for(j=0;j<4;++j) if((seen&(1U<<j)) && overlap(at,n,offsets[j],sizes[j])) return false;
        offsets[k]=at; sizes[k]=n; seen|=1U<<k;
    }
    return first==0x4000 ? (seen&5)==5 : seen==3;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint32_t at[4]={0},n[4]={0},total,channels,samples,codec,i,j; uint64_t channel_at[2],data_at[2],bytes; bool be; uint8_t h[32],r[8],c[20]; char label[40];
    if(!audio_blocks(f,"CWAV",0x102,0x10102,0x7000,at,n,&be,&total,pd) || n[0]<32 || !pm_read(f,at[0],h,32)) return false;
    codec=h[8]; samples=g32(h+20,be); channels=g32(h+28,be); bytes=(uint64_t)samples*(codec+1U);
    if(codec>1 || h[9]>1 || g16(h+10,be) || !g32(h+12,be) || g32(h+12,be)>384000 || !samples || g32(h+16,be)>=samples || !channels || channels>2 || !span(32,(uint64_t)channels*8,n[0])) return false;
    for(i=0;i<channels;++i) {
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)at[0]+32+i*8,r,8) || g16(r,be)!=0x7100 || g16(r+2,be)) return false;
        channel_at[i]=28U+(uint64_t)g32(r+4,be);
        if(channel_at[i]<32U+channels*8 || !span(channel_at[i],20,n[0]) || !pm_read(f,(int64_t)at[0]+(int64_t)channel_at[i],c,20) || g16(c,be)!=0x1f00 || g16(c+2,be) || g16(c+8,be) || g16(c+10,be) || g32(c+12,be)!=UINT32_MAX || g32(c+16,be)) return false;
        data_at[i]=8U+(uint64_t)g32(c+4,be); if(!span(data_at[i],bytes,n[1])) return false;
        for(j=0;j<i;++j) if(overlap(channel_at[i],20,channel_at[j],20) || overlap(data_at[i],bytes,data_at[j],bytes)) return false;
    }
    if(!emit(f,s,"info.bin",at[0],n[0],total)) return false;
    for(i=0;i<channels;++i) { xx_rt_snprintf(label,sizeof(label),"channel-%u.pcm",i); if(!emit(f,s,label,(uint64_t)at[1]+data_at[i],bytes,total)) return false; }
    s->size=total; return true;

}

void xx_nintendo_bcwav_init(xx_nintendo_bcwav *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BCWAV,"bcwav"); } }
xx_nintendo_bcwav *xx_nintendo_bcwav_create(xx_io_device *d,int64_t b) { xx_nintendo_bcwav *r=(xx_nintendo_bcwav *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_bcwav_init(r,d,b); return r; }
void xx_nintendo_bcwav_destroy(xx_nintendo_bcwav *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_bcwav_free(xx_nintendo_bcwav *r) { if(r) { xx_nintendo_bcwav_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_bcwav_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_bcwav_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
