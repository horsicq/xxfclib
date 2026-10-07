/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/zeldamods/byml-v2/master/byml/byml.py
 * BYAML version2, either endian, flat/nested maps and arrays of string/bool/int32/uint32/float/null values, up to1024 nodes/depth32 and1024 strings per table. Exports bounded encoded tables/nodes; validates key/string indices and sorted keys. Shared nodes, binary/64-bit values, newer revisions and application interpretation unsupported.
 */
#include "xxfclib/formats/nintendo_byaml/xx_nintendo_byaml.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint16_t g16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool take(Abstractformat *f,uint64_t *at,uint64_t end,void *p,size_t n,xx_pd_struct *pd) { if(stop(pd) || !span(*at,n,end) || !pm_read(f,(int64_t)*at,p,n)) return false; *at+=n; return true; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool finite32(const uint8_t *p,bool be) { return (g32(p,be)&0x7f800000U)!=0x7f800000U; }
static bool cstring(Abstractformat *f,uint64_t *at,uint64_t end,unsigned maximum,bool empty,xx_pd_struct *pd) { uint8_t c; unsigned i; for(i=0;i<maximum;++i) { if(!take(f,at,end,&c,1,pd)) return false; if(!c) return empty || i!=0; } return false; }
static bool by_table(Abstractformat *f,pm_stream *s,uint32_t offset,bool be,uint32_t *count,uint64_t *measured,const char *label,xx_pd_struct *pd) {
    uint8_t h[4],p[4]; uint32_t i,previous,last; uint64_t end=(uint64_t)pm_available(f),pos,table_end;
    if(!offset) { *count=0; return true; } if(offset<16 || (offset&3) || !pm_read(f,offset,h,4) || h[0]!=0xc2 || !(*count=xx_data_get_u24(h+1, 3, 0, be)) || *count>1024 || !span(offset,8+(uint64_t)*count*4,end)) return false;
    table_end=(uint64_t)offset+8+(uint64_t)*count*4; if(!pm_read(f,(int64_t)offset+4,p,4)) return false; previous=g32(p,be); if(previous!=table_end-offset) return false;
    for(i=0;i<*count;++i) { if(stop(pd) || !pm_read(f,(int64_t)offset+8+i*4,p,4)) return false; last=g32(p,be); if(last<=previous || !span(offset,last,end)) return false;
      pos=(uint64_t)offset+previous; if(!cstring(f,&pos,(uint64_t)offset+last,4096,true,pd) || pos!=(uint64_t)offset+last) return false; previous=last; }
    if(!emit(f,s,label,offset,previous,end)) { return false; } if((uint64_t)offset+previous>*measured) *measured=(uint64_t)offset+previous; return true;
}
static bool by_node(Abstractformat *f,pm_stream *s,uint32_t offset,bool be,uint32_t keys,uint32_t strings,unsigned expected,unsigned depth,uint32_t *visited,unsigned *nvisited,uint64_t *measured,xx_pd_struct *pd) {
    uint8_t h[4],e[8],p[4]; uint32_t n,i,previous=0; uint64_t at,total=(uint64_t)pm_available(f),length,value_at; char label[40];
    if(depth>32 || *nvisited>=1024 || offset<16 || (offset&3)) { return false; } for(i=0;i<*nvisited;++i) if(visited[i]==offset) return false; visited[(*nvisited)++]=offset;
    if(!pm_read(f,offset,h,4) || (h[0]!=0xc0 && h[0]!=0xc1) || (expected && h[0]!=expected) || (n=xx_data_get_u24(h+1, 3, 0, be))>1024) return false;
    length=h[0]==0xc1 ? 4+(uint64_t)n*8 : 4+((n+3U)&~3U)+(uint64_t)n*4; if(!span(offset,length,total)) return false;
    xx_rt_snprintf(label,sizeof(label),"%s-node.bin",h[0]==0xc1 ? "map" : "array"); if(!emit(f,s,label,offset,length,total)) return false; if((uint64_t)offset+length>*measured) *measured=(uint64_t)offset+length;
    for(i=0;i<n;++i) { uint8_t kind; uint32_t value;
      if(h[0]==0xc1) { at=(uint64_t)offset+4+i*8; if(!pm_read(f,(int64_t)at,e,8)) return false; value=xx_data_get_u24(e, 3, 0, be); if(value>=keys || (i && value<=previous)) return false; previous=value; kind=e[3]; value_at=at+4; }
      else { at=(uint64_t)offset+4+i; if(!pm_read(f,(int64_t)at,&kind,1)) return false; value_at=(uint64_t)offset+4+((n+3U)&~3U)+i*4; }
      if(stop(pd) || !pm_read(f,(int64_t)value_at,p,4)) { return false; } value=g32(p,be);
      if(kind==0xc0 || kind==0xc1) { if(!by_node(f,s,value,be,keys,strings,kind,depth+1,visited,nvisited,measured,pd)) return false; }
      else if(kind==0xa0) { if(value>=strings) return false; }
      else if(kind==0xd0) { if(value>1) return false; }
      else if(kind==0xd2) { if(!finite32(p,be)) return false; }
      else if(kind==0xff) { if(value) return false; }
      else if(kind!=0xd1 && kind!=0xd3) return false;
    }
    return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[16]; bool be; uint32_t keys,strings,visited[1024],root; unsigned count=0; uint64_t measured=16;
    if(!pm_read(f,0,h,16)) { return false; } be=h[0]=='B' && h[1]=='Y'; if(!be && !(h[0]=='Y' && h[1]=='B')) return false; if(g16(h+2,be)!=2) return false; root=g32(h+12,be);
    if(!by_table(f,s,g32(h+4,be),be,&keys,&measured,"key-table.bin",pd) || !by_table(f,s,g32(h+8,be),be,&strings,&measured,"string-table.bin",pd) || !root || !by_node(f,s,root,be,keys,strings,0,0,visited,&count,&measured,pd)) return false;
    s->size=(int64_t)measured; return true;

}

void xx_nintendo_byaml_init(xx_nintendo_byaml *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BYAML,"byml"); } }
xx_nintendo_byaml *xx_nintendo_byaml_create(xx_io_device *d,int64_t b) { xx_nintendo_byaml *r=(xx_nintendo_byaml *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_byaml_init(r,d,b); return r; }
void xx_nintendo_byaml_destroy(xx_nintendo_byaml *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_byaml_free(xx_nintendo_byaml *r) { if(r) { xx_nintendo_byaml_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_byaml_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_byaml_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
