/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Norbyte/lslib/master/LSLib/LS/Resources/LSF/LSFCommon.cs
 * LSOF version2 stored string/node/attribute/value streams using metadata format0. Exports four encoded components; string chains, name references, node parents/attribute indices and value lengths checked. Compressed or extended versions and semantic resource reconstruction unsupported.
 */
#include "xxfclib/formats/larian_lsf/xx_larian_lsf.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"

static uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
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
static uint16_t crc16(const uint8_t *p,size_t n) { return xx_crc16_modbus_calc(UINT16_MAX,p,n); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[52],b[12]; uint64_t offsets[4],sizes[4],at=52,values=0; uint16_t chains[4096]; uint32_t buckets,i,j,nodes,attrs; const char *labels[]={"strings.bin","nodes.bin","attributes.bin","values.bin"};
    if(!pm_read(f,0,h,52) || xx_rt_memcmp(h,"LSOF",4) || pm_le32(h+4)!=2 || pm_le32(h+44) || pm_le32(h+48)) return false;
    for(i=0;i<4;++i) { uint32_t raw=pm_le32(h+12+i*8),packed=pm_le32(h+16+i*8); if(packed && packed!=raw) return false; sizes[i]=raw; offsets[i]=at;
        if(!span(at,raw,(uint64_t)pm_available(f)) || raw>16U*1024U*1024U) return false; at+=raw; }
    if(sizes[0]<4 || sizes[1]%12 || sizes[2]%12 || sizes[1]/12>4096 || sizes[2]/12>4096) return false;
    nodes=(uint32_t)(sizes[1]/12); attrs=(uint32_t)(sizes[2]/12); if(!nodes) return false;
    if(!pm_read(f,(int64_t)offsets[0],b,4)) return false; buckets=pm_le32(b); if(!buckets || buckets>4096) return false;
    { uint64_t pos=offsets[0]+4,end=offsets[0]+sizes[0]; uint32_t names=0;
      for(i=0;i<buckets;++i) { if((pd && xx_pd_is_stopped(pd)) || !span(pos,2,end) || !pm_read(f,(int64_t)pos,b,2)) return false; pos+=2; chains[i]=pm_le16(b); names+=chains[i]; if(names>4096) return false;
        for(j=0;j<chains[i];++j) { uint32_t n; if(!span(pos,2,end) || !pm_read(f,(int64_t)pos,b,2)) return false; pos+=2; n=pm_le16(b); if(!n || !span(pos,n,end)) return false; pos+=n; } }
      if(pos!=end) return false; }
    for(i=0;i<nodes;++i) { uint32_t ref,first,parent;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(offsets[1]+i*12),b,12)) return false; ref=pm_le32(b); first=pm_le32(b+4); parent=pm_le32(b+8);
        if(ref>>16>=buckets || (ref&65535)>=chains[ref>>16] || (first!=UINT32_MAX && first>=attrs) || (parent!=UINT32_MAX && parent>=i)) return false; }
    for(i=0;i<attrs;++i) { uint32_t ref,tl;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(offsets[2]+i*12),b,12)) return false; ref=pm_le32(b); tl=pm_le32(b+4);
        if(ref>>16>=buckets || (ref&65535)>=chains[ref>>16] || (tl&63)>33 || !span(values,tl>>6,sizes[3])) return false; values+=tl>>6; }
    if(values!=sizes[3]) return false;
    for(i=0;i<4;++i) if(sizes[i] && !emit(f,s,labels[i],offsets[i],sizes[i],at)) return false;
    s->size=(int64_t)at; return true;

}

void xx_larian_lsf_init(xx_larian_lsf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LARIAN_LSF,"lsf"); } }
xx_larian_lsf *xx_larian_lsf_create(xx_io_device *d,int64_t b) { xx_larian_lsf *r=(xx_larian_lsf *)xx_mem_alloc(sizeof(*r)); if(r) xx_larian_lsf_init(r,d,b); return r; }
void xx_larian_lsf_destroy(xx_larian_lsf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_larian_lsf_free(xx_larian_lsf *r) { if(r) { xx_larian_lsf_destroy(r); xx_mem_free(r); } }
bool xx_larian_lsf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_larian_lsf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
