/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.rfc-editor.org/rfc/rfc9559.html, https://www.rfc-editor.org/rfc/rfc8794.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/matroska/xx_matroska.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

typedef struct mk_ctx { Abstractformat *f; pm_stream *s; xx_pd_struct *pd; uint64_t tracks[256]; unsigned track_count,blocks; } mk_ctx;
static bool mk_vint(Abstractformat *f,int64_t *at,int64_t end,uint64_t *value,unsigned *length,bool id,bool *unknown) {
    uint8_t b[8],mask=128; unsigned n=1,i; uint64_t v;
    if(*at>=end || !pm_read(f,*at,b,1) || !b[0]) return false;
    while(!(b[0]&mask)) { ++n; mask>>=1; }
    if(n>(id ? 4U : 8U) || n>(uint64_t)(end-*at) || !pm_read(f,*at,b,n)) return false;
    v=id ? b[0] : b[0]&(mask-1U); for(i=1;i<n;++i) v=(v<<8)|b[i];
    *at+=n; *value=v; *length=n; if(unknown) *unknown=!id && v==((1ULL<<(7*n))-1ULL); return true;
}
static bool mk_element(Abstractformat *f,int64_t at,int64_t end,uint32_t *id,int64_t *body,int64_t *stop,bool allow_unknown) {
    uint64_t v,n; unsigned width; bool unknown;
    if(!mk_vint(f,&at,end,&v,&width,true,NULL) || !mk_vint(f,&at,end,&n,&width,false,&unknown) || (unknown && !allow_unknown) || (!unknown && n>(uint64_t)(end-at))) return false;
    *id=(uint32_t)v; *body=at; *stop=unknown ? end : at+(int64_t)n; return true;
}
static bool mk_unsigned(Abstractformat *f,int64_t at,int64_t end,uint64_t *v) {
    uint8_t b[8]; int64_t n=end-at; unsigned i; uint64_t value=0;
    if(n<1 || n>8 || !pm_read(f,at,b,(size_t)n)) { return false; } for(i=0;i<(unsigned)n;++i) value=(value<<8)|b[i]; *v=value; return true;
}
static bool mk_crc(Abstractformat *f,int64_t start,int64_t end,int64_t omit_start,int64_t omit_end,uint32_t expected) {
    size_t capacity=xx_get_file_buffer_size();uint8_t *b=NULL;bool ok=false;uint32_t crc=0U;int64_t at=start;
    while(at<end) {int64_t boundary=at<omit_start ? omit_start:end;size_t n;
        if(at==omit_start){at=omit_end;continue;}
        if(!b){if((uint64_t)(end-at)<capacity)capacity=(size_t)(end-at);b=(uint8_t *)xx_mem_alloc(capacity);if(!b)goto done;}
        n=(uint64_t)(boundary-at)>capacity ? capacity:(size_t)(boundary-at);if(!n||!pm_read(f,at,b,n))goto done;
        crc=xx_crc32_calc(crc,b,n);at+=(int64_t)n;
    }ok=crc==expected;
done:xx_mem_free(b);return ok;
}
static bool mk_block(mk_ctx *c,int64_t at,int64_t end,bool simple) {
    uint64_t track,sizes[256],sum=0; unsigned width,lace,count=1,i; uint8_t h[3],b; bool unknown; char name[64]; int64_t data;
    if(!mk_vint(c->f,&at,end,&track,&width,false,&unknown) || !track || end-at<3 || !pm_read(c->f,at,h,3)) return false;
    for(i=0;i<c->track_count && c->tracks[i]!=track;++i) {} if(i==c->track_count) return false;
    if((h[2]&0x70U) || (!simple && (h[2]&0x81U))) { return false; } lace=(h[2]>>1)&3U; at+=3;
    if(lace) { if(at>=end || !pm_read(c->f,at++,&b,1) || !b) return false; count=(unsigned)b+1; }
    if(lace==1) for(i=0;i+1<count;++i) { sizes[i]=0; do { if(at>=end || !pm_read(c->f,at++,&b,1)) return false; sizes[i]+=b; } while(b==255); sum+=sizes[i]; }
    else if(lace==3 && count>1) {
        if(!mk_vint(c->f,&at,end,&sizes[0],&width,false,&unknown)) { return false; } sum=sizes[0];
        for(i=1;i+1<count;++i) { uint64_t raw,bias; int64_t delta;
            if(!mk_vint(c->f,&at,end,&raw,&width,false,&unknown)) { return false; } bias=(1ULL<<(7*width-1))-1;
            delta=(int64_t)raw-(int64_t)bias;
            if((delta<0 && (uint64_t)(-delta)>sizes[i-1]) || (delta>0 && sizes[i-1]>INT64_MAX-(uint64_t)delta)) return false;
            sizes[i]=(uint64_t)((int64_t)sizes[i-1]+delta); if(sizes[i]>UINT64_MAX-sum) return false; sum+=sizes[i];
        }
    }
    if(lace==2) { if((end-at)%count) return false; for(i=0;i<count;++i) sizes[i]=(uint64_t)(end-at)/count; }
    else { if(sum>(uint64_t)(end-at)) return false; sizes[count-1]=(uint64_t)(end-at)-sum; }
    data=at;
    for(i=0;i<count;++i) { xx_rt_snprintf(name,sizeof(name),"track-%u-block-%u-frame-%u.encoded",(unsigned)track,c->blocks,i);
        if(!sizes[i] || !pm_add(c->f,c->s,name,data,(int64_t)sizes[i])) { return false; } data+=(int64_t)sizes[i]; }
    ++c->blocks; return data==end;
}
static bool mk_master(uint32_t id) {
    return id==0x1549A966U || id==0x1654AE6BU || id==0xAE || id==0x1F43B675U || id==0xA0 || id==0xE0 || id==0xE1 || id==0x6D80 || id==0x6240 || id==0x5034 || id==0x5035 || id==0x1043A770U || id==0x45B9 || id==0xB6 || id==0x80 || id==0x1254C367U || id==0x7373 || id==0x63C0 || id==0x67C8 || id==0x1941A469U || id==0x61A7 || id==0x114D9B74U || id==0x4DBB || id==0x1C53BB6BU || id==0xBB || id==0xB7;
}
static bool mk_children(mk_ctx *c,int64_t start,int64_t end,unsigned depth,uint32_t parent) {
    int64_t at=start; unsigned crc_count=0,segment_parts=0; uint64_t track=0,uid=0,type=0; bool codec=false,timecode=false,muxing=false,writing=false; if(depth>24) return false;
    while(at<end) { uint32_t id; int64_t body,stop; char name[40]; uint8_t b[8];
        if((c->pd && xx_pd_is_stopped(c->pd)) || !mk_element(c->f,at,end,&id,&body,&stop,false)) return false;
        if((id==0xAE && parent!=0x1654AE6BU) || (id==0xA3 && parent!=0x1F43B675U) ||
           (id==0xA1 && parent!=0xA0) || (id==0xA0 && parent!=0x1F43B675U)) return false;
        if(id==0x1549A966U || id==0x1654AE6BU || id==0x1F43B675U) {
            unsigned part=id==0x1549A966U ? 1U : id==0x1654AE6BU ? 2U : 4U;
            if(parent!=0x18538067U || (part!=4 && (segment_parts&part))) { return false; } segment_parts|=part;
        }
        if(id==0xBF) { if(++crc_count>1 || stop-body!=4 || !pm_read(c->f,body,b,4) || !mk_crc(c->f,start,end,at,stop,xx_data_get_u32(b, 4, 0, false))) return false; }
        else if(id==0xEC) {}
        else if(id==0xA3 || id==0xA1) { if(!mk_block(c,body,stop,id==0xA3)) return false; }
        else if(mk_master(id)) { if(!mk_children(c,body,stop,depth+1,id)) return false; }
        else {
            if(parent==0x1549A966U && (id==0x4D80 || id==0x5741)) {
                bool *present=id==0x4D80 ? &muxing : &writing;
                if(*present || stop==body) { return false; } *present=true;
            }
            if(parent==0xAE) {
                if(id==0xD7) { if(track || !mk_unsigned(c->f,body,stop,&track)) return false; }
                if(id==0x73C5) { if(uid || !mk_unsigned(c->f,body,stop,&uid)) return false; }
                if(id==0x83) { if(type || !mk_unsigned(c->f,body,stop,&type)) return false; }
                if(id==0x86) { if(codec || stop==body || stop-body>255) return false; codec=true; }
            }
            if(parent==0x1F43B675U && id==0xE7) { uint64_t value; if(timecode || !mk_unsigned(c->f,body,stop,&value)) return false; timecode=true; }
            xx_rt_snprintf(name,sizeof(name),"element-%08X.bin",id); if(!pm_add(c->f,c->s,name,body,stop-body)) return false;
        } at=stop;
    }
    if(parent==0xAE) { unsigned i; if(!track || track>UINT32_MAX || !uid || !type || !codec || c->track_count==256) return false;
        for(i=0;i<c->track_count;++i) { if(c->tracks[i]==track) return false; } c->tracks[c->track_count++]=track; }
    if((parent==0x1F43B675U && !timecode) || (parent==0x18538067U && segment_parts!=7) ||
       (parent==0x1549A966U && (!muxing || !writing))) { return false; } return at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint32_t id; int64_t body,end,at,limit=pm_available(f); bool doctype=false; mk_ctx c;
    if(!mk_element(f,0,limit,&id,&body,&end,false) || id!=0x1A45DFA3U) return false;
    at=body;
    while(at<end) { int64_t value,stop; uint8_t text[8]; uint64_t n;
        if(!mk_element(f,at,end,&id,&value,&stop,false)) return false;
        if(id==0x4282) { if(doctype || (stop-value!=8 && stop-value!=4) || !pm_read(f,value,text,(size_t)(stop-value)) || (stop-value==8 ? xx_rt_memcmp(text,"matroska",8) : xx_rt_memcmp(text,"webm",4))) return false; doctype=true; }
        if(id==0x42F2 || id==0x42F3 || id==0x42F7 || id==0x4285) { if(!mk_unsigned(f,value,stop,&n) || (id==0x42F2 && n>4) || (id==0x42F3 && n>8) || (id==0x42F7 && n>1) || (id==0x4285 && n>4)) return false; }
        at=stop;
    }
    if(!doctype || !mk_element(f,end,limit,&id,&body,&at,true) || id!=0x18538067U) return false;
    xx_mem_zero(&c,sizeof(c)); c.f=f; c.s=s; c.pd=pd;
    if(!mk_children(&c,body,at,0,id) || !c.track_count || !c.blocks) { return false; } s->size=at; return true;
}

void xx_matroska_init(xx_matroska *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MATROSKA,"matroska"); } }
xx_matroska *xx_matroska_create(xx_io_device *d,int64_t b) { xx_matroska *r=(xx_matroska *)xx_mem_alloc(sizeof(*r)); if(r) xx_matroska_init(r,d,b); return r; }
void xx_matroska_destroy(xx_matroska *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_matroska_free(xx_matroska *r) { if(r) { xx_matroska_destroy(r); xx_mem_free(r); } }
bool xx_matroska_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_matroska_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
