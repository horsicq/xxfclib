/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/Boot/DolReader.h
 * DOL headers with first text section at0x100, up to7 text/11 data sections in cached MEM1 or MEM2 addresses. Exports separate stored executable sections; validates file/memory ranges and entry point. Alternate first offsets, Ancast, relocation, loading and execution unsupported.
 */
#include "xxfclib/formats/nintendo_dol/xx_nintendo_dol.h"
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
static bool zeros(Abstractformat *f,uint64_t at,uint64_t n,xx_pd_struct *pd) { size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false;
    if(n) { if(capacity>n) capacity=(size_t)n; b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result = (false); goto buffer_done; } } size_t i; while(n) { size_t part=n>capacity ? capacity:(size_t)n; if(stop(pd) || !pm_read(f,(int64_t)at,b,part)) { buffer_result = (false); goto buffer_done; } for(i=0;i<part;++i) if(b[i]) { buffer_result = (false); goto buffer_done; } at+=part; n-=part; } { buffer_result = (true); goto buffer_done; } 
buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && pm_be32(h+4)==n; }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[256]; uint32_t address[18],size[18],entry,bss,bssn,i,j; uint64_t total=256; bool executable=false; char label[40];
    if(!pm_read(f,0,h,256) || pm_be32(h)!=256 || !zeros(f,228,28,pd)) return false; entry=pm_be32(h+224); bss=pm_be32(h+216); bssn=pm_be32(h+220);
    for(i=0;i<18;++i) { uint32_t offset=pm_be32(h+i*4); uint64_t limit; address[i]=pm_be32(h+72+i*4); size[i]=pm_be32(h+144+i*4);
      if(!size[i]) { if(offset || address[i]) return false; continue; } limit=address[i]>=0x90000000U ? 0x94000000ULL : 0x81800000ULL;
      if(offset<256 || address[i]<0x80000000U || (address[i]>=0x81800000U && address[i]<0x90000000U) || !span(address[i],size[i],limit) || (address[i]&3) || !span(offset,size[i],(uint64_t)pm_available(f))) return false;
      for(j=0;j<i;++j) if(overlap(address[i],size[i],address[j],size[j])) return false;
      xx_rt_snprintf(label,sizeof(label),i<7 ? "text-%u.bin":"data-%u.bin",i<7 ? i:i-7); if(stop(pd) || !emit(f,s,label,offset,size[i],(uint64_t)pm_available(f))) return false;
      if((uint64_t)offset+size[i]>total) total=(uint64_t)offset+size[i]; if(i<7 && entry>=address[i] && entry-address[i]<size[i]) executable=true; }
    if(!size[0] || !executable || (entry&3)) return false; if(bssn) { uint64_t limit=bss>=0x90000000U ? 0x94000000ULL:0x81800000ULL; if(bss<0x80000000U || (bss>=0x81800000U && bss<0x90000000U) || !span(bss,bssn,limit)) return false; for(i=0;i<18;++i) if(overlap(bss,bssn,address[i],size[i])) return false; }
    s->size=(int64_t)total; return true;

}

void xx_nintendo_dol_init(xx_nintendo_dol *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_DOL,"dol"); } }
xx_nintendo_dol *xx_nintendo_dol_create(xx_io_device *d,int64_t b) { xx_nintendo_dol *r=(xx_nintendo_dol *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_dol_init(r,d,b); return r; }
void xx_nintendo_dol_destroy(xx_nintendo_dol *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_dol_free(xx_nintendo_dol *r) { if(r) { xx_nintendo_dol_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_dol_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_dol_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
