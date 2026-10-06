/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Youjose/PyCriCodecs/main/CriCodecs/src/usm/usm_reader.cpp
 * CRI USM CRID/@SFV/@SFA chunk sequences with standard32-byte headers and unflagged payload types0-3, up to4096 chunks. Checks chunk/padding extents and clear UTF metadata tables using a64-entry string-validation cache per table and one8MiB cumulative uncached string-scan budget shared across the entire container; exports encoded metadata tables and stream packets. Opaque codec/encrypted packet bytes are preserved without decryption/decoding; scan-budget excess, flagged headers, SFSH wrappers and unsupported stream IDs rejected.
 */
#include "xxfclib/formats/cri_usm/xx_cri_usm.h"
#include "../xx_payload_members.h"

static uint32_t g32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : (uint64_t)pm_le32(p)|((uint64_t)pm_le32(p+4)<<32); }
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
typedef struct rg { uint64_t at,n; } rg;
static bool zeros(Abstractformat *f,uint64_t at,uint64_t n,xx_pd_struct *pd) { size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false;
    if(n) { if(capacity>n) capacity=(size_t)n; b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result = (false); goto buffer_done; } } size_t i; while(n) { size_t part=n>capacity ? capacity:(size_t)n; if(stop(pd) || !pm_read(f,(int64_t)at,b,part)) { buffer_result = (false); goto buffer_done; } for(i=0;i<part;++i) if(b[i]) { buffer_result = (false); goto buffer_done; } at+=part; n-=part; } { buffer_result = (true); goto buffer_done; } 
buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static XXFC_MAYBE_UNUSED bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && pm_be32(h+4)==n; }


typedef struct utf_string_cache { uint64_t pool,end,*budget; uint32_t offsets[64]; unsigned count,next; } utf_string_cache;
static bool utf_string(Abstractformat *f,utf_string_cache *cache,uint32_t offset,xx_pd_struct *pd) {
    size_t capacity=xx_get_file_buffer_size(); uint8_t *p; uint64_t at;
    unsigned i,scanned=0; bool result=false;
    if(stop(pd) || offset>=cache->end-cache->pool) return false;
    for(i=0;i<cache->count;++i) if(cache->offsets[i]==offset) return true;
    at=cache->pool+offset; if(capacity>256) capacity=256;
    p=(uint8_t *)xx_mem_alloc(capacity); if(!p) return false;
    while(at<cache->end && scanned<4096) {
        uint64_t remaining=cache->end-at; size_t part=remaining>256 ? 256:(size_t)remaining,done=0;
        bool terminated=false;
        if(part>4096U-scanned) part=4096U-scanned;
        if(stop(pd) || part>*cache->budget) break;
        /* Keep the format's original validation budget and complete logical
         * read even when a NUL occurs in an earlier physical chunk. */
        while(done<part) {
            size_t take=part-done>capacity ? capacity:part-done;
            if(!pm_read(f,(int64_t)(at+done),p,take)) goto done;
            if(xx_rt_memchr(p,0,take)) terminated=true;
            done+=take;
        }
        *cache->budget-=part;
        if(terminated) {
            if(cache->count<64) cache->offsets[cache->count++]=offset;
            else { cache->offsets[cache->next]=offset; cache->next=(cache->next+1)%64; }
            result=true;break;
        }
        at+=part;scanned+=(unsigned)part;
    }
done:
    xx_mem_free(p);return result;
}
static bool utf_value(Abstractformat *f,pm_stream *s,uint64_t at,unsigned kind,utf_string_cache *cache,uint64_t data,uint64_t total,bool collect,xx_pd_struct *pd) { uint8_t p[16]; static const uint8_t sizes[]={1,1,2,2,4,4,8,8,4,8,4,8,16}; if(kind>12 || !pm_read(f,(int64_t)at,p,sizes[kind])) return false;
    if(kind==8 && !finite32(p,true)) { return false; } if(kind==9 && (g64(p,true)&0x7ff0000000000000ULL)==0x7ff0000000000000ULL) return false;
    if(kind==10) return utf_string(f,cache,pm_be32(p),pd);
    if(kind==11) { uint32_t offset=pm_be32(p),n=pm_be32(p+4); if(!span(offset,n,total-data)) return false; if(n && collect && !emit(f,s,"binary-value.bin",data+offset,n,total)) return false; }
    return !stop(pd);
}
static bool utf_table(Abstractformat *f,pm_stream *s,uint64_t base,uint64_t available,bool collect,uint64_t *length,uint64_t *scan_budget,xx_pd_struct *pd) { uint8_t h[32],p[5]; uint32_t rows,columns,width,i,j,colwidth=0; uint64_t total,row_at,strings,data,at=base+32; unsigned kinds[128],sizes[128],flags[128]; uint32_t positions[128]; utf_string_cache cache;
    static const uint8_t type_sizes[]={1,1,2,2,4,4,8,8,4,8,4,8,16};
    if(!span(base,32,available) || !pm_read(f,(int64_t)base,h,32) || xx_rt_memcmp(h,"@UTF",4) || pm_be16(h+8)>1) { return false; } *length=8U+(uint64_t)pm_be32(h+4); if(*length<32 || *length>67108864 || !span(base,*length,available)) return false; total=base+*length; row_at=base+8+pm_be16(h+10); strings=base+8+pm_be32(h+12); data=base+8+pm_be32(h+16); columns=pm_be16(h+24); width=pm_be16(h+26); rows=pm_be32(h+28);
    if(!scan_budget || !columns || columns>128 || !rows || rows>1024 || row_at<base+32 || strings<row_at || data<strings || data>total || data-strings>1048576 || !span(row_at,(uint64_t)rows*width,strings)) { return false; } xx_mem_zero(&cache,sizeof(cache)); cache.pool=strings; cache.end=data; cache.budget=scan_budget; if(!utf_string(f,&cache,pm_be32(h+20),pd)) return false;
    for(i=0;i<columns;++i) { uint32_t kind,flag; if(!take(f,&at,row_at,p,5,pd)) return false; kind=p[0]&15; flag=p[0]&0xf0; if(kind>12 || !(flag&0x10) || (flag&0x80) || !utf_string(f,&cache,pm_be32(p+1),pd)) return false; kinds[i]=kind; sizes[i]=type_sizes[kind]; flags[i]=flag; positions[i]=colwidth;
      if(flag&0x20) { if(!span(at,sizes[i],row_at) || !utf_value(f,s,at,kind,&cache,data,total,collect,pd)) return false; at+=sizes[i]; }
      if(flag&0x40) colwidth+=sizes[i]; }
    if(colwidth!=width || !zeros(f,at,row_at-at,pd) || !zeros(f,row_at+(uint64_t)rows*width,strings-row_at-(uint64_t)rows*width,pd)) return false;
    for(j=0;j<rows;++j) for(i=0;i<columns;++i) if((flags[i]&0x40) && !utf_value(f,s,row_at+(uint64_t)j*width+positions[i],kinds[i],&cache,data,total,collect,pd)) return false;
    if(collect && (!emit(f,s,"schema.bin",base+32,row_at-base-32,total) || (width && !emit(f,s,"rows.bin",row_at,(uint64_t)rows*width,total)) || !emit(f,s,"strings.bin",strings,data-strings,total))) { return false; } return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32]; uint64_t at=0,total=(uint64_t)pm_available(f),n,body,payload,end,length,scan_budget=8388608; unsigned count=0,packets=0; bool crid=false; char label[40];
    while(at<total) { uint32_t kind,type,pad,offset; if(++count>4096 || stop(pd) || !span(at,32,total) || !pm_read(f,(int64_t)at,h,32)) return false; kind=pm_be32(h); n=8U+(uint64_t)pm_be32(h+4); offset=pm_be16(h+8); pad=pm_be16(h+10); type=pm_be16(h+14);
      if(n<32 || offset<24 || type>3 || h[13] || pm_be32(h+24) || pm_be32(h+28) || !span(at,n,total) || offset+8U>n || pad>n-offset-8U) { return false; } end=at+n; payload=at+8+offset; body=end-payload-pad;
      if(kind==0x43524944U) { if(crid || count!=1 || type!=1 || h[12] || !body || !utf_table(f,s,payload,payload+body,true,&length,&scan_budget,pd) || length!=body) return false; crid=true; }
      else if(kind==0x40534656U || kind==0x40534641U) { if(!crid) return false; if(type==0) { if(!body) return false; xx_rt_snprintf(label,sizeof(label),kind==0x40534656U ? "video-%u-packet.bin":"audio-%u-packet.bin",h[12]); if(!emit(f,s,label,payload,body,total)) return false; ++packets; }
        else if(type==1 || type==3) { if(!body || !utf_table(f,s,payload,payload+body,true,&length,&scan_budget,pd) || length!=body) return false; }
        else if(body && !emit(f,s,"section-end.bin",payload,body,total)) return false; }
      else { return false; } if(!zeros(f,end-pad,pad,pd)) return false; at=end;
    }
    if(!crid || !packets) { return false; } s->size=(int64_t)total; return true;

}

void xx_cri_usm_init(xx_cri_usm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CRI_USM,"usm"); } }
xx_cri_usm *xx_cri_usm_create(xx_io_device *d,int64_t b) { xx_cri_usm *r=(xx_cri_usm *)xx_mem_alloc(sizeof(*r)); if(r) xx_cri_usm_init(r,d,b); return r; }
void xx_cri_usm_destroy(xx_cri_usm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_cri_usm_free(xx_cri_usm *r) { if(r) { xx_cri_usm_destroy(r); xx_mem_free(r); } }
bool xx_cri_usm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_cri_usm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
