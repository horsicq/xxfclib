/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/BrawlCrate/BrawlCrate/master/BrawlLib/SSBB/Types/Audio/RSTM.cs
 * Big-endian RSTM1.0 PCM8/PCM16 streams with one block and1-2 channels, one mono/stereo track, standard HEAD/DATA references and no ADPC block. Checks disjoint HEAD records and exports encoded HEAD metadata and each stored PCM channel. Multi-block/multitrack streams, DSP ADPCM, alternate beta headers and audio playback unsupported.
 */
#include "xxfclib/formats/nintendo_brstm/xx_nintendo_brstm.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
typedef struct rg { uint64_t at,n; } rg;
static bool reserve(rg *r,unsigned *count,unsigned maximum,uint64_t at,uint64_t n,uint64_t lower,uint64_t end) { unsigned i; if(*count>=maximum || at<lower || !span(at,n,end)) return false; for(i=0;i<*count;++i) if(overlap(at,n,r[i].at,r[i].n)) return false; r[*count].at=at; r[*count].n=n; ++*count; return true; }
static bool zeros(Abstractformat *f,uint64_t at,uint64_t n,xx_pd_struct *pd) { size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false;
    if(n) { if(capacity>n) capacity=(size_t)n; b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result = (false); goto buffer_done; } } size_t i; while(n) { size_t part=n>capacity ? capacity:(size_t)n; if(stop(pd) || !pm_read(f,(int64_t)at,b,part)) { buffer_result = (false); goto buffer_done; } for(i=0;i<part;++i) if(b[i]) { buffer_result = (false); goto buffer_done; } at+=part; n-=part; } { buffer_result = (true); goto buffer_done; } 
buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && xx_data_get_u32(h+4, 4, 0, true)==n; }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[64],head[32],info[52],data[12],p[8]; uint32_t total,ho,hn,doff,dn,rel[3],codec,channels,samples,last,pad,i,track,nr=0; uint64_t pcm; char label[40]; rg ranges[12];
    if(!pm_read(f,0,h,64) || xx_rt_memcmp(h,"RSTM",4) || xx_data_get_u16(h+4, 2, 0, true)!=0xfeff || xx_data_get_u16(h+6, 2, 0, true)!=0x100 || xx_data_get_u16(h+12, 2, 0, true)!=64 || xx_data_get_u16(h+14, 2, 0, true)!=2 || (total=xx_data_get_u32(h+8, 4, 0, true))>(uint64_t)pm_available(f) || xx_data_get_u32(h+24, 4, 0, true) || xx_data_get_u32(h+28, 4, 0, true) || !zeros(f,40,24,pd)) return false;
    ho=xx_data_get_u32(h+16, 4, 0, true); hn=xx_data_get_u32(h+20, 4, 0, true); doff=xx_data_get_u32(h+32, 4, 0, true); dn=xx_data_get_u32(h+36, 4, 0, true);
    if(ho<64 || doff<64 || overlap(ho,hn,doff,dn) || !section(f,ho,total,"HEAD",hn,pd) || !section(f,doff,total,"DATA",dn,pd) || hn<32 || !pm_read(f,ho,head,32)) return false;
    for(i=0;i<3;++i) { if(xx_data_get_u32(head+8+i*8, 4, 0, true)!=0x01000000U) return false; rel[i]=xx_data_get_u32(head+12+i*8, 4, 0, true); if(rel[i]<24 || !span(rel[i],i ? 4:52,hn-8)) return false; }
    if(!reserve(ranges,&nr,12,rel[0],52,24,hn-8) || !pm_read(f,ho+8+(int64_t)rel[0],info,52)) { return false; } codec=info[0]; channels=info[2]; samples=xx_data_get_u32(info+12, 4, 0, true); last=xx_data_get_u32(info+32, 4, 0, true); pad=xx_data_get_u32(info+40, 4, 0, true);
    if(codec>1 || info[1]>1 || !channels || channels>2 || !xx_data_get_u16(info+4, 2, 0, true) || info[3] || xx_data_get_u16(info+6, 2, 0, true) || !samples || samples>16777216 || (info[1] && xx_data_get_u32(info+8, 4, 0, true)>=samples) || xx_data_get_u32(info+20, 4, 0, true)!=1 || last!=(uint64_t)samples*(codec+1) || xx_data_get_u32(info+36, 4, 0, true)!=samples || pad<last || pad>last+31 || xx_data_get_u32(info+48, 4, 0, true)!=8U*(codec+1)) return false;
    if(!reserve(ranges,&nr,12,rel[1],12,24,hn-8) || !pm_read(f,ho+8+(int64_t)rel[1],p,4) || p[0]!=1 || p[1] || p[2] || p[3] || !pm_read(f,ho+12+(int64_t)rel[1],p,8) || xx_data_get_u32(p, 4, 0, true)!=0x01000000U || !reserve(ranges,&nr,12,track=xx_data_get_u32(p+4, 4, 0, true),4,24,hn-8) || !pm_read(f,ho+8+(int64_t)track,p,4) || p[0]!=channels || p[1] || p[2]!=(channels==2 ? 1:0) || p[3]) return false;
    if(!reserve(ranges,&nr,12,rel[2],4+(uint64_t)channels*8,24,hn-8) || !pm_read(f,ho+8+(int64_t)rel[2],p,4) || p[0]!=channels || p[1] || p[2] || p[3]) return false;
    for(i=0;i<channels;++i) if(stop(pd) || !pm_read(f,ho+12+(int64_t)rel[2]+i*8,p,8) || xx_data_get_u32(p, 4, 0, true)!=0x01000000U || !reserve(ranges,&nr,12,xx_data_get_u32(p+4, 4, 0, true),8,24,hn-8) || !zeros(f,ho+8+(uint64_t)xx_data_get_u32(p+4, 4, 0, true),8,pd)) return false;
    if(!pm_read(f,doff,data,12) || xx_data_get_u32(data+8, 4, 0, true)<4) { return false; } pcm=doff+8+(uint64_t)xx_data_get_u32(data+8, 4, 0, true); if(pcm!=xx_data_get_u32(info+16, 4, 0, true) || !span(pcm,(uint64_t)channels*pad,doff+(uint64_t)dn)) return false;
    if(!emit(f,s,"head.bin",ho,hn,total)) { return false; } for(i=0;i<channels;++i) { xx_rt_snprintf(label,sizeof(label),"channel-%u.pcm",i); if(!emit(f,s,label,pcm+(uint64_t)i*pad,last,total)) return false; }
    s->size=total; return true;

}

void xx_nintendo_brstm_init(xx_nintendo_brstm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BRSTM,"brstm"); } }
xx_nintendo_brstm *xx_nintendo_brstm_create(xx_io_device *d,int64_t b) { xx_nintendo_brstm *r=(xx_nintendo_brstm *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_brstm_init(r,d,b); return r; }
void xx_nintendo_brstm_destroy(xx_nintendo_brstm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_brstm_free(xx_nintendo_brstm *r) { if(r) { xx_nintendo_brstm_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_brstm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_brstm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
