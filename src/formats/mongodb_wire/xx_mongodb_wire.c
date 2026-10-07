/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/mongodb/specifications/blob/master/source/message/OP_MSG.md */
#include "xxfclib/formats/mongodb_wire/xx_mongodb_wire.h"
#include "../xx_twelfth_a.h"
static bool bs_doc(nh_blob *,uint64_t *,uint64_t,unsigned,unsigned *,bool);
static bool bs_string(nh_blob *b,uint64_t *at,uint64_t end) {
    uint64_t n;if(!eh_span(*at,4,end) || !nh_span(b,*at,4)) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);*at+=4;
    if(!n || n>65536 || !eh_span(*at,n,end) || !nh_span(b,*at,n) || b->p[(size_t)(*at+n-1)] || !fourth_utf8(b->p+(size_t)*at,(size_t)n-1,b->pd)) { return false; } *at+=n;return true;
}
static bool bs_element(nh_blob *b,uint64_t *at,uint64_t end,unsigned depth,unsigned *nodes,bool array,unsigned index) {
    unsigned type;uint64_t n=0,start,key;char want[24];if(*at>=end || !nh_span(b,*at,1) || ++*nodes>1000000) return false;type=b->p[(size_t)(*at)++];key=*at;if(!tw_z(b,at,end,true)) return false;
    if(array) {xx_rt_snprintf(want,sizeof(want),"%u",index);if(*at-key!=xx_rt_strlen(want)+1 || xx_rt_memcmp(b->p+(size_t)key,want,(size_t)(*at-key-1))) return false;}
    switch(type) {
    case 1:case 9:case 0x11:case 0x12:n=8;break;
    case 2:case 0x0d:case 0x0e:return bs_string(b,at,end);
    case 3:case 4:return bs_doc(b,at,end,depth+1,nodes,type==4);
    case 5:if(!eh_span(*at,5,end) || !nh_span(b,*at,5)) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);type=b->p[(size_t)*at+4];*at+=5;if(type==2) {if(n<4 || !eh_span(*at,4,end) || xx_data_get_u32(b->p+(size_t)*at, 4, 0, false)!=n-4) return false;}break;
    case 6:case 0x0a:case 0x7f:case 0xff:n=0;break;
    case 7:n=12;break;
    case 8:if(!eh_span(*at,1,end) || !nh_span(b,*at,1) || b->p[(size_t)*at]>1) return false;n=1;break;
    case 0x0b:if(!tw_z(b,at,end,true)) return false;start=*at;if(!tw_z(b,at,end,true)) return false;for(uint64_t i=start;i+1<*at;++i) {uint8_t c=b->p[(size_t)i];if((c!=105 && c!=108 && c!=109 && c!=115 && c!=117 && c!=120) || (i>start && b->p[(size_t)i-1]>=c)) return false;}return true;
    case 0x0c:if(!bs_string(b,at,end)) return false;n=12;break;
    case 0x0f:start=*at;if(!eh_span(start,4,end) || !nh_span(b,start,4)) return false;n=xx_data_get_u32(b->p+(size_t)start, 4, 0, false);if(n<14 || !eh_span(start,n,end)) return false;*at+=4;if(!bs_string(b,at,start+n) || !bs_doc(b,at,start+n,depth+1,nodes,false) || *at!=start+n) return false;return true;
    case 0x10:n=4;break;case 0x13:n=16;break;default:return false;
    }if(!eh_span(*at,n,end) || !nh_span(b,*at,n)) return false;*at+=n;return true;
}
static bool bs_doc(nh_blob *b,uint64_t *at,uint64_t limit,unsigned depth,unsigned *nodes,bool array) {
    uint64_t start=*at,end,n;unsigned index=0;if(depth>32 || !eh_span(*at,5,limit) || !nh_span(b,*at,5)) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);if(n<5 || !eh_span(*at,n,limit)) return false;end=start+n;*at+=4;
    while(*at<end-1) if(!bs_element(b,at,end-1,depth,nodes,array,index++)) return false;
    if(*at!=end-1 || b->p[(size_t)*at]) { return false; } ++*at;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=0,start,end,n,p,section,idStart;uint32_t flags;uint64_t identifiers[256],idLengths[256];unsigned work=0,messages=0,body,sequences;bool ok=false;if(!nh_load(f,&b,pd)) return false;while(at<b.n) {NH_NEED(nh_span(&b,at,21));start=at;n=xx_data_get_u32(b.p+(size_t)at, 4, 0, false);NH_NEED(n>=26 && eh_span(at,n,b.n) && xx_data_get_u32(b.p+(size_t)at+12, 4, 0, false)==2013);flags=xx_data_get_u32(b.p+(size_t)at+16, 4, 0, false);NH_NEED(!(flags&~0x10003U));end=at+n;if(flags&1) {NH_NEED(n>=30 && ec_crc(&b,at,n-4,xx_data_get_u32(b.p+(size_t)end-4, 4, 0, false),true));end-=4;}NH_NEED(nh_add(f,s,&b,"opmsg-header",at,20));at+=20;body=0;sequences=0;while(at<end) {uint8_t kind=b.p[(size_t)at++];section=at-1;if(!kind) {p=at;NH_NEED(!body++ && bs_doc(&b,&at,end,0,&work,false) && nh_add(f,s,&b,"body-document",p,at-p));}else if(kind==1) {NH_NEED(eh_span(at,5,end));n=xx_data_get_u32(b.p+(size_t)at, 4, 0, false);NH_NEED(n>=5 && eh_span(at,n,end));p=at+n;at+=4;idStart=at;NH_NEED(tw_z(&b,&at,p,true) && at-idStart>1 && sequences<256);for(unsigned j=0;j<sequences;++j) NH_NEED(idLengths[j]!=at-idStart || xx_rt_memcmp(b.p+(size_t)identifiers[j],b.p+(size_t)idStart,(size_t)(at-idStart)));identifiers[sequences]=idStart;idLengths[sequences++]=at-idStart;NH_NEED(nh_add(f,s,&b,"sequence-header",section,at-section));unsigned docs=0;while(at<p) {uint64_t doc=at;NH_NEED(++docs<=2048 && bs_doc(&b,&at,p,0,&work,false) && nh_add(f,s,&b,"sequence-document",doc,at-doc));}NH_NEED(docs && at==p);}else NH_NEED(false);}NH_NEED(body==1 && at==end && ++messages<=256);at=start+xx_data_get_u32(b.p+(size_t)start, 4, 0, false);if(flags&1) NH_NEED(nh_add(f,s,&b,"crc32c",end,4));}NH_NEED(messages);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_mongodb_wire_init(xx_mongodb_wire *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MONGODB_WIRE,"opmsg"); } }
xx_mongodb_wire *xx_mongodb_wire_create(xx_io_device *d,int64_t b) { xx_mongodb_wire *r=(xx_mongodb_wire *)xx_mem_alloc(sizeof(*r)); if(r) xx_mongodb_wire_init(r,d,b); return r; }
void xx_mongodb_wire_destroy(xx_mongodb_wire *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mongodb_wire_free(xx_mongodb_wire *r) { if(r) { xx_mongodb_wire_destroy(r); xx_mem_free(r); } }
bool xx_mongodb_wire_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mongodb_wire_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
