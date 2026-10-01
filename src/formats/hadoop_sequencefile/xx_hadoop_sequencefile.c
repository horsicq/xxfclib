/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/apache/hadoop/blob/trunk/hadoop-common-project/hadoop-common/src/main/java/org/apache/hadoop/io/SequenceFile.java */
#include "xxfclib/formats/hadoop_sequencefile/xx_hadoop_sequencefile.h"
#include "../microsoft_msf/xx_tenth_containers.h"

static bool sq_vint(nh_blob *b,uint64_t *at,uint64_t *v) {
    int c;unsigned width,i;uint64_t x=0;if(!nh_span(b,*at,1)) return false;c=(int)(signed char)b->p[(size_t)(*at)++];if(c>=-112) {if(c<0) return false;*v=(uint64_t)c;return true;}width=(unsigned)(c<-120 ? -120-c:-112-c);if(!width || width>8 || !nh_span(b,*at,width)) return false;for(i=0;i<width;++i) x=(x<<8)|b->p[(size_t)(*at)++];if(c<-120) x=~x;if(x>65536) return false;*v=x;return true;
}
static bool sq_text(nh_blob *b,uint64_t *at,bool empty) {uint64_t n;if(!sq_vint(b,at,&n) || n>65536 || (!empty && !n) || !nh_span(b,*at,n) || !fourth_utf8(b->p+(size_t)*at,(size_t)n,b->pd)) return false;*at+=n;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;uint64_t at=4,sync,n,key,i;uint32_t meta;bool ok=false;
    if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,4) && !xx_rt_memcmp(b.p,"SEQ\x06",4) && sq_text(&b,&at,false) && sq_text(&b,&at,false));NH_NEED(nh_span(&b,at,6) && !b.p[(size_t)at] && !b.p[(size_t)at+1]);at+=2;meta=pm_be32(b.p+(size_t)at);at+=4;NH_NEED(meta<=4094);
    for(i=0;i<meta;++i) NH_NEED(sq_text(&b,&at,false) && sq_text(&b,&at,true));sync=at;NH_NEED(nh_span(&b,at,16));at+=16;NH_NEED(nh_add(f,s,&b,"sequence-header",0,at));
    while(at<b.n) {NH_NEED(nh_span(&b,at,4));n=pm_be32(b.p+(size_t)at);at+=4;if(n==0xffffffffU) {NH_NEED(nh_span(&b,at,16) && !xx_rt_memcmp(b.p+(size_t)at,b.p+(size_t)sync,16));NH_NEED(nh_add(f,s,&b,"sync-marker",at-4,20));at+=16;if(at==b.n) break;NH_NEED(nh_span(&b,at,4));n=pm_be32(b.p+(size_t)at);at+=4;}
        NH_NEED(n<=67108864 && nh_span(&b,at,4));key=pm_be32(b.p+(size_t)at);at+=4;NH_NEED(key<=n && nh_span(&b,at,n));NH_NEED(nh_add(f,s,&b,"key",at,key) && nh_add(f,s,&b,"value",at+key,n-key));at+=n;
    }NH_NEED(s->count>1);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_hadoop_sequencefile_init(xx_hadoop_sequencefile *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_HADOOP_SEQUENCEFILE,"seq"); } }
xx_hadoop_sequencefile *xx_hadoop_sequencefile_create(xx_io_device *d,int64_t b) { xx_hadoop_sequencefile *r=(xx_hadoop_sequencefile *)xx_mem_alloc(sizeof(*r)); if(r) xx_hadoop_sequencefile_init(r,d,b); return r; }
void xx_hadoop_sequencefile_destroy(xx_hadoop_sequencefile *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_hadoop_sequencefile_free(xx_hadoop_sequencefile *r) { if(r) { xx_hadoop_sequencefile_destroy(r); xx_mem_free(r); } }
bool xx_hadoop_sequencefile_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_hadoop_sequencefile_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
