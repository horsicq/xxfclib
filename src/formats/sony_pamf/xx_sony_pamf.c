/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/rpcs3/rpcs3/master/rpcs3/Emu/Cell/Modules/cellPamf.h
 * PAMF0040/0041 with one grouping period/group, up to32 stream descriptors and bounded12-byte entry-point tables. Exports stream metadata, entry points and encoded MPEG program-stream body; no codec decoding/demultiplexing, PSMF marks, trust or playback.
 */
#include "xxfclib/formats/sony_pamf/xx_sony_pamf.h"
#include "../xx_payload_members.h"

static uint16_t g16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t g32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
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
static bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[136],p[48],ep[12],marker[4]; uint32_t streams,i; uint64_t header,data,total,stream_end,next_ep=0; uint16_t channels[6]={0}; char label[40];
    if(!pm_read(f,0,h,136) || xx_rt_memcmp(h,"PAMF",4) || (xx_rt_memcmp(h+4,"0040",4) && xx_rt_memcmp(h+4,"0041",4))) return false;
    header=(uint64_t)pm_be32(h+8)*2048; data=(uint64_t)pm_be32(h+12)*2048; total=header+data;
    if(header<184 || header>16U*1024U*1024U || !data || total>(uint64_t)pm_available(f)) return false;
    for(i=16;i<80;++i) if(h[i]) return false;
    streams=h[135]; stream_end=136U+(uint64_t)streams*48;
    if(!streams || streams>32 || stream_end>header || pm_be32(h+106)!=streams || h[111]!=1 || h[129]!=1 || h[110] || h[128] || h[134] || pm_be16(h+84) || pm_be16(h+86) || pm_be16(h+92) || pm_be16(h+116) || pm_be16(h+122)) return false;
    if(pm_be32(h+88)>=pm_be32(h+94) || pm_be32(h+118)!=pm_be32(h+88) || pm_be32(h+124)!=pm_be32(h+94) || pm_be32(h+130)!=2U+streams*48U || pm_be32(h+112)!=20U+streams*48U || pm_be32(h+80)!=52U+streams*48U) return false;
    for(i=0;i<streams;++i) {
        uint32_t type,channel,offset,count,k,previous=0;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,136+(int64_t)i*48,p,48) || p[1] || p[2] || p[3] || (pm_be16(p+6)&0xc000)) return false;
        if(p[0]==0x1b || p[0]==2) { type=p[0]==0x1b ? 0 : 1; channel=p[4]&15; if((p[4]&0xf0)!=0xe0 || p[5]) return false; }
        else { type=p[0]==0xdc ? 2 : p[0]==0x80 ? 3 : p[0]==0x81 ? 4 : p[0]==0xdd ? 5 : 6; channel=p[5]&15;
            if(type==6 || p[4]!=0xbd || (p[5]&0xf0)!=(type==2 ? 0 : type==3 ? 0x40 : type==4 ? 0x30 : 0x20)) return false; }
        if(channels[type]&(1U<<channel)) return false; channels[type]|=(uint16_t)(1U<<channel);
        offset=pm_be32(p+8); count=pm_be32(p+12); if((!count && offset) || count>4096 || (count && (offset<stream_end || !span(offset,(uint64_t)count*12,header) || (next_ep && offset!=next_ep)))) return false;
        xx_rt_snprintf(label,sizeof(label),"stream-%u.header",i); if(!emit(f,s,label,136U+(uint64_t)i*48,48,total)) return false;
        for(k=0;k<count;++k) { uint32_t rpn; if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)offset+k*12,ep,12) || pm_be16(ep+2)) return false; rpn=pm_be32(ep+8);
            if((uint64_t)rpn*2048>data || (k && rpn<previous) || pm_be32(ep+4)>pm_be32(h+94)) return false; previous=rpn; }
        if(count) { xx_rt_snprintf(label,sizeof(label),"stream-%u.entry-points",i); if(!emit(f,s,label,offset,(uint64_t)count*12,total)) return false; next_ep=offset+(uint64_t)count*12; }
    }
    if(!pm_read(f,(int64_t)header,marker,4) || pm_be32(marker)!=0x000001baU || !emit(f,s,"program-stream.mpg",header,data,total)) return false;
    s->size=(int64_t)total; return true;

}

void xx_sony_pamf_init(xx_sony_pamf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_PAMF,"pamf"); } }
xx_sony_pamf *xx_sony_pamf_create(xx_io_device *d,int64_t b) { xx_sony_pamf *r=(xx_sony_pamf *)xx_mem_alloc(sizeof(*r)); if(r) xx_sony_pamf_init(r,d,b); return r; }
void xx_sony_pamf_destroy(xx_sony_pamf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sony_pamf_free(xx_sony_pamf *r) { if(r) { xx_sony_pamf_destroy(r); xx_mem_free(r); } }
bool xx_sony_pamf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sony_pamf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
