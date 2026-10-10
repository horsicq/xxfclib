/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://bsonspec.org/spec.html */
#include "xxfclib/formats/mongodb_bson/xx_mongodb_bson.h"
#include "../common/xx_container_codec_helpers.h"

static bool bs_doc(memory_blob *,uint64_t *,uint64_t,unsigned,unsigned *,bool);
static bool bs_string(memory_blob *b,uint64_t *at,uint64_t end) {
    uint64_t n;if(!record_span(*at,4,end) || !blob_span(b,*at,4)) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);*at+=4;
    if(!n || n>65536 || !record_span(*at,n,end) || !blob_span(b,*at,n) || b->p[(size_t)(*at+n-1)] || !bounded_utf8(b->p+(size_t)*at,(size_t)n-1,b->pd)) { return false; } *at+=n;return true;
}
static bool bs_element(memory_blob *b,uint64_t *at,uint64_t end,unsigned depth,unsigned *nodes,bool array,unsigned index) {
    unsigned type;uint64_t n=0,start,key;char want[24];if(*at>=end || !blob_span(b,*at,1) || ++*nodes>1000000) return false;type=b->p[(size_t)(*at)++];key=*at;if(!container_codec_z(b,at,end,true)) return false;
    if(array) {xx_rt_snprintf(want,sizeof(want),"%u",index);if(*at-key!=xx_rt_strlen(want)+1 || xx_rt_memcmp(b->p+(size_t)key,want,(size_t)(*at-key-1))) return false;}
    switch(type) {
    case 1:case 9:case 0x11:case 0x12:n=8;break;
    case 2:case 0x0d:case 0x0e:return bs_string(b,at,end);
    case 3:case 4:return bs_doc(b,at,end,depth+1,nodes,type==4);
    case 5:if(!record_span(*at,5,end) || !blob_span(b,*at,5)) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);type=b->p[(size_t)*at+4];*at+=5;if(type==2) {if(n<4 || !record_span(*at,4,end) || xx_data_get_u32(b->p+(size_t)*at, 4, 0, false)!=n-4) return false;}break;
    case 6:case 0x0a:case 0x7f:case 0xff:n=0;break;
    case 7:n=12;break;
    case 8:if(!record_span(*at,1,end) || !blob_span(b,*at,1) || b->p[(size_t)*at]>1) return false;n=1;break;
    case 0x0b:return container_codec_z(b,at,end,true) && container_codec_z(b,at,end,true);
    case 0x0c:if(!bs_string(b,at,end)) return false;n=12;break;
    case 0x0f:start=*at;if(!record_span(start,4,end) || !blob_span(b,start,4)) return false;n=xx_data_get_u32(b->p+(size_t)start, 4, 0, false);if(n<14 || !record_span(start,n,end)) return false;*at+=4;if(!bs_string(b,at,start+n) || !bs_doc(b,at,start+n,depth+1,nodes,false) || *at!=start+n) return false;return true;
    case 0x10:n=4;break;case 0x13:n=16;break;default:return false;
    }if(!record_span(*at,n,end) || !blob_span(b,*at,n)) return false;*at+=n;return true;
}
static bool bs_doc(memory_blob *b,uint64_t *at,uint64_t limit,unsigned depth,unsigned *nodes,bool array) {
    uint64_t start=*at,end,n;unsigned index=0;if(depth>32 || !record_span(*at,5,limit) || !blob_span(b,*at,5)) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);if(n<5 || !record_span(*at,n,limit)) return false;end=start+n;*at+=4;
    while(*at<end-1) if(!bs_element(b,at,end-1,depth,nodes,array,index++)) return false;
    if(*at!=end-1 || b->p[(size_t)*at]) { return false; } ++*at;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b;uint64_t at=0,end,start,check;unsigned nodes=0,index;bool ok=false;
    if(!blob_load(f,&b,pd)) return false;
    while(at<b.n) {check=at;BLOB_NEED(bs_doc(&b,&check,b.n,0,&nodes,false));end=check;BLOB_NEED(blob_add(f,s,&b,"document-header",at,4));at+=4;index=0;
        while(at<end-1) {start=at;BLOB_NEED(bs_element(&b,&at,end-1,0,&nodes,false,index++));BLOB_NEED(blob_add(f,s,&b,"field",start,at-start));}
        BLOB_NEED(blob_add(f,s,&b,"document-terminator",at,1));at=end;
    }BLOB_NEED(s->count);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_mongodb_bson_init(xx_mongodb_bson *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MONGODB_BSON,"bson"); } }
xx_mongodb_bson *xx_mongodb_bson_create(xx_io_device *d,int64_t b) { xx_mongodb_bson *r=(xx_mongodb_bson *)xx_mem_alloc(sizeof(*r)); if(r) xx_mongodb_bson_init(r,d,b); return r; }
void xx_mongodb_bson_destroy(xx_mongodb_bson *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mongodb_bson_free(xx_mongodb_bson *r) { if(r) { xx_mongodb_bson_destroy(r); xx_mem_free(r); } }
bool xx_mongodb_bson_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mongodb_bson_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
