/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/libsdl-org/libtiff/master/libtiff/tif_dirread.c, https://libtiff.gitlab.io/libtiff/specification/bigtiff.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/tiff/xx_tiff.h"
#include "../xx_payload_members.h"

typedef struct tf_value { uint16_t type; uint64_t count; int64_t at; } tf_value;
typedef struct tf_ctx { Abstractformat *f; pm_stream *s; bool be,big; int64_t limit,end; uint64_t queue[1024]; unsigned used,count; xx_pd_struct *pd; } tf_ctx;
static uint16_t tf16(tf_ctx *c,const uint8_t *p) { return c->be ? pm_be16(p) : pm_le16(p); }
static uint32_t tf32(tf_ctx *c,const uint8_t *p) { return c->be ? pm_be32(p) : pm_le32(p); }
static uint64_t tf64(tf_ctx *c,const uint8_t *p) { return c->be ? (uint64_t)pm_be32(p)<<32|pm_be32(p+4) : (uint64_t)pm_le32(p+4)<<32|pm_le32(p); }
static unsigned tf_width(unsigned type) { static const unsigned widths[19]={0,1,1,2,4,8,1,1,2,4,8,4,8,4,0,0,8,8,8}; return type<19 ? widths[type] : 0; }
static bool tf_num(tf_ctx *c,tf_value v,uint64_t index,uint64_t *n) {
    uint8_t b[8]; unsigned w=tf_width(v.type);
    if((v.type!=1 && v.type!=3 && v.type!=4 && v.type!=13 && v.type!=16 && v.type!=18) || index>=v.count || !pm_read(c->f,v.at+(int64_t)(index*w),b,w)) return false;
    *n=w==1 ? b[0] : w==2 ? tf16(c,b) : w==4 ? tf32(c,b) : tf64(c,b); return true;
}
static bool tf_enqueue(tf_ctx *c,uint64_t at) { unsigned i; if(!at) return true;
    if(at>INT64_MAX || at<(c->big ? 16U : 8U) || at>=(uint64_t)c->limit || c->count==1024) return false;
    for(i=0;i<c->count;++i) if(c->queue[i]==at) return false; c->queue[c->count++]=at; return true;
}
static bool tf_ifd(tf_ctx *c,uint64_t offset) {
    uint8_t h[20]; uint64_t entries,i,table_end,next,width=0,height=0,compression=1,spp=1,planar=1,total=0,bits=1,rows=UINT32_MAX,tilew=0,tileh=0;
    unsigned entry_size=c->big ? 20U : 12U,count_size=c->big ? 8U : 2U,next_size=c->big ? 8U : 4U; tf_value offsets={0},sizes={0},bitvalues={0}; bool tiled=false,sizes_tiled=false;
    if(!pm_read(c->f,(int64_t)offset,h,count_size)) return false; entries=c->big ? tf64(c,h) : tf16(c,h);
    if(entries>65536 || entries*entry_size+count_size+next_size>(uint64_t)c->limit-offset) return false;
    table_end=offset+count_size+entries*entry_size+next_size;
    if((int64_t)table_end>c->end) c->end=(int64_t)table_end;
    for(i=0;i<entries;++i) { uint16_t tag,type; unsigned w; uint64_t count,nbytes,value; int64_t pos=(int64_t)(offset+count_size+i*entry_size),data; tf_value v;
        if((c->pd && xx_pd_is_stopped(c->pd)) || !pm_read(c->f,pos,h,entry_size)) return false;
        tag=tf16(c,h); type=tf16(c,h+2); count=c->big ? tf64(c,h+4) : tf32(c,h+4); w=tf_width(type);
        if(!w || (!c->big && type>=16) || count>(uint64_t)INT64_MAX/w) return false; nbytes=count*w;
        data=pos+(c->big ? 12 : 8); if(nbytes>(c->big ? 8U : 4U)) { value=c->big ? tf64(c,h+12) : tf32(c,h+8); if(value>INT64_MAX) return false; data=(int64_t)value; }
        if(data<0 || data>c->limit || nbytes>(uint64_t)(c->limit-data)) return false;
        if(data+(int64_t)nbytes>c->end) c->end=data+(int64_t)nbytes;
        v.type=type; v.count=count; v.at=data;
        if(tag==273 || tag==324) { if(offsets.type) return false; offsets=v; tiled=tag==324; }
        else if(tag==279 || tag==325) { if(sizes.type) return false; sizes=v; sizes_tiled=tag==325; }
        else if(tag==258) bitvalues=v;
        else if(tag==330) { uint64_t j; for(j=0;j<count;++j) if(!tf_num(c,v,j,&value) || !tf_enqueue(c,value)) return false; }
        else if(tag==256 || tag==257 || tag==259 || tag==277 || tag==278 || tag==284 || tag==322 || tag==323) {
            if(count!=1 || !tf_num(c,v,0,&value)) return false;
            if(tag==256) width=value; else if(tag==257) height=value; else if(tag==259) compression=value; else if(tag==277) spp=value; else if(tag==278) rows=value; else if(tag==284) planar=value; else if(tag==322) tilew=value; else tileh=value;
        } else if(nbytes>(c->big ? 8U : 4U)) { char name[48]; xx_rt_snprintf(name,sizeof(name),"ifd-%u-tag-%u.bin",c->used-1,tag); if(!pm_add(c->f,c->s,name,data,(int64_t)nbytes)) return false; }
    }
    if(!pm_read(c->f,(int64_t)(table_end-next_size),h,next_size)) return false; next=c->big ? tf64(c,h) : tf32(c,h); if(!tf_enqueue(c,next)) return false;
    if(!width || !height || width>UINT32_MAX || height>UINT32_MAX || !spp || spp>65536 || !compression || (planar!=1 && planar!=2) || !offsets.count || offsets.count!=sizes.count || offsets.count>65536 || tiled!=sizes_tiled) return false;
    if(bitvalues.type) { uint64_t j,sum=0; if(bitvalues.count!=spp) return false; for(j=0;j<spp;++j) { uint64_t value; if(!tf_num(c,bitvalues,j,&value) || !value || value>64) return false; sum+=value; } bits=sum; } else bits=spp;
    if(tiled) { uint64_t expected; if(!tilew || !tileh || tilew>UINT32_MAX || tileh>UINT32_MAX) return false; expected=((width+tilew-1)/tilew)*((height+tileh-1)/tileh); if(expected>65536) return false; if(planar==2) expected*=spp; if(expected!=offsets.count) return false; }
    else { uint64_t expected; if(!rows) return false; expected=(height+rows-1)/rows; if(planar==2) expected*=spp; if(expected!=offsets.count) return false; }
    for(i=0;i<offsets.count;++i) { uint64_t at,bytes; char name[48];
        if(!tf_num(c,offsets,i,&at) || !tf_num(c,sizes,i,&bytes) || at<(c->big ? 16U : 8U) || !bytes || at>(uint64_t)c->limit || bytes>(uint64_t)c->limit-at || (at<table_end && at+bytes>offset)) return false;
        xx_rt_snprintf(name,sizeof(name),"ifd-%u-%s-%u.encoded",c->used-1,tiled ? "tile" : "strip",(unsigned)i);
        if(!pm_add(c->f,c->s,name,(int64_t)at,(int64_t)bytes) || bytes>UINT64_MAX-total) return false; total+=bytes;
        if((int64_t)(at+bytes)>c->end) c->end=(int64_t)(at+bytes);
    }
    if(compression==1 && !tiled && planar==1) { uint64_t row=(width*bits+7)/8; if(row>UINT64_MAX/height || total!=row*height) return false; }
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16]; tf_ctx c; uint16_t version; uint64_t first;
    xx_mem_zero(&c,sizeof(c)); c.f=f; c.s=s; c.pd=pd; c.limit=pm_available(f);
    if(!pm_read(f,0,h,8) || ((h[0]!='I' || h[1]!='I') && (h[0]!='M' || h[1]!='M'))) return false; c.be=h[0]=='M'; version=tf16(&c,h+2); c.big=version==43;
    if(version!=42 && version!=43) return false;
    if(c.big) { if(!pm_read(f,0,h,16) || tf16(&c,h+4)!=8 || tf16(&c,h+6)) return false; first=tf64(&c,h+8); } else first=tf32(&c,h+4);
    if(!first || !tf_enqueue(&c,first)) return false;
    while(c.used<c.count) { uint64_t offset=c.queue[c.used]; ++c.used; if(!tf_ifd(&c,offset)) return false; }
    s->size=c.end; return s->count>0;
}

void xx_tiff_init(xx_tiff *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TIFF,"tiff"); } }
xx_tiff *xx_tiff_create(xx_io_device *d,int64_t b) { xx_tiff *r=(xx_tiff *)xx_mem_alloc(sizeof(*r)); if(r) xx_tiff_init(r,d,b); return r; }
void xx_tiff_destroy(xx_tiff *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tiff_free(xx_tiff *r) { if(r) { xx_tiff_destroy(r); xx_mem_free(r); } }
bool xx_tiff_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tiff_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
