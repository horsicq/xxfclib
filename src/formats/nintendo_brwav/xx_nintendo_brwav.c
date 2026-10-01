/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/BrawlCrate/BrawlCrate/master/BrawlLib/SSBB/Types/Audio/RWAV.cs
 * Big-endian RWAV1.2 PCM8/PCM16 wave containers with1-8 channels and offset-based sample data. Checks INFO/DATA extents, channel table/records and sample ranges. Exports INFO metadata and stored PCM planes; DSP ADPCM, absolute runtime pointers and playback unsupported.
 */
#include "xxfclib/formats/nintendo_brwav/xx_nintendo_brwav.h"
#include "../xx_payload_members.h"

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
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && pm_be32(h+4)==n; }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32],b[36],p[28],q[4]; uint32_t total,io,in,da,dn,ch,n,codec,table,base,i; uint64_t data; char label[40]; rg ranges[10]; unsigned nr=0;
    if(!pm_read(f,0,h,32) || xx_rt_memcmp(h,"RWAV",4) || pm_be16(h+4)!=0xfeff || pm_be16(h+6)!=0x102 || pm_be16(h+12)!=32 || pm_be16(h+14)!=2 || (total=pm_be32(h+8))>(uint64_t)pm_available(f)) return false;
    io=pm_be32(h+16); in=pm_be32(h+20); da=pm_be32(h+24); dn=pm_be32(h+28); if(io<32 || da<32 || overlap(io,in,da,dn) || !section(f,io,total,"INFO",in,pd) || !section(f,da,total,"DATA",dn,pd) || in<36 || !pm_read(f,io,b,36)) return false;
    codec=b[8]; ch=b[10]; n=pm_be32(b+20); table=pm_be32(b+24); base=pm_be32(b+28); if(codec>1 || b[9]>1 || !ch || ch>8 || b[11] || !pm_be16(b+12) || b[14] || b[15] || !n || n>16777216 || (b[9] && pm_be32(b+16)>=n) || pm_be32(b+32) || !reserve(ranges,&nr,10,table,(uint64_t)ch*4,28,in-8)) return false;
    data=da+8U+(uint64_t)base; for(i=0;i<ch;++i) { uint32_t ci; if(stop(pd) || !pm_read(f,io+8+(int64_t)table+i*4,q,4) || !reserve(ranges,&nr,10,ci=pm_be32(q),28,28,in-8) || !pm_read(f,io+8+(int64_t)ci,p,28) || pm_be32(p+4) || pm_be32(p+24) || !span(data+pm_be32(p),(uint64_t)n*(codec+1),da+(uint64_t)dn)) return false;
      xx_rt_snprintf(label,sizeof(label),"channel-%u.pcm",i); if(!emit(f,s,label,data+pm_be32(p),(uint64_t)n*(codec+1),total)) return false; }
    if(!emit(f,s,"info.bin",io,in,total)) return false; s->size=total; return true;

}

void xx_nintendo_brwav_init(xx_nintendo_brwav *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BRWAV,"brwav"); } }
xx_nintendo_brwav *xx_nintendo_brwav_create(xx_io_device *d,int64_t b) { xx_nintendo_brwav *r=(xx_nintendo_brwav *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_brwav_init(r,d,b); return r; }
void xx_nintendo_brwav_destroy(xx_nintendo_brwav *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_brwav_free(xx_nintendo_brwav *r) { if(r) { xx_nintendo_brwav_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_brwav_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_brwav_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
