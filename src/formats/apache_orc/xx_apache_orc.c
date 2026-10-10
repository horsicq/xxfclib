/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://orc.apache.org/specification/ORCv1/ */
#include "xxfclib/formats/apache_orc/xx_apache_orc.h"
#include "../common/xx_container_codec_helpers.h"

typedef struct oc_field {uint64_t tag,wire,v,at,n;} oc_field;
static bool oc_next(memory_blob *b,uint64_t *at,uint64_t end,oc_field *x) {
    uint64_t key;if(!container_codec_var(b,at,end,&key) || !(key>>3) || (key>>3)>0x1fffffff) return false;x->tag=key>>3;x->wire=key&7;x->n=0;x->at=*at;x->v=0;
    if(!x->wire) return container_codec_var(b,at,end,&x->v);
    if(x->wire==2) {if(!container_codec_var(b,at,end,&x->n)) return false;x->at=*at;}else if(x->wire==1) x->n=8;else if(x->wire==5) x->n=4;else return false;
    if(!record_span(*at,x->n,end) || !blob_span(b,*at,x->n)) { return false; } *at+=x->n;return true;
}
static bool oc_proto(memory_blob *b,uint64_t at,uint64_t n) {uint64_t end=at+n;oc_field x;unsigned fields=0;while(at<end) if(++fields>100000 || !oc_next(b,&at,end,&x)) return false;return at==end;}
static bool oc_numbers(memory_blob *b,oc_field *x,uint64_t limit) {uint64_t at=x->at,v;if(!x->wire) return x->v<limit;if(x->wire!=2) return false;while(at<x->at+x->n) if(!container_codec_var(b,&at,x->at+x->n,&v) || v>=limit) return false;return true;}
static bool oc_type(memory_blob *b,uint64_t at,uint64_t n,uint64_t types) {uint64_t end=at+n;oc_field x;unsigned kind=99;bool seen=false;while(at<end) {if(!oc_next(b,&at,end,&x)) return false;
    if(x.tag==1) {if(x.wire || seen || x.v>18) return false;seen=true;kind=(unsigned)x.v;}
    else if(x.tag==2) {if(!oc_numbers(b,&x,types)) return false;}
    else if(x.tag==3 || x.tag==8) {if(x.wire!=2 || !bounded_utf8(b->p+(size_t)x.at,(size_t)x.n,b->pd)) return false;}
    else if(x.tag==4 || x.tag==5 || x.tag==6 || x.tag==9) {if(x.wire) return false;}
    else if(x.tag==7) {if(x.wire!=2 || !oc_proto(b,x.at,x.n)) return false;}else return false;
    }return seen && kind<=18;
}
static bool oc_stripe_footer(memory_blob *b,uint64_t at,uint64_t n,uint64_t types,uint64_t bytes) {uint64_t end=at+n,total=0,columns=0;oc_field x,y;while(at<end) {if(!oc_next(b,&at,end,&x)) return false;
    if(x.tag==1 || x.tag==2) {uint64_t p=x.at,e=p+x.n;unsigned flags=0;uint64_t len=0;if(x.wire!=2) return false;
        while(p<e) {if(!oc_next(b,&p,e,&y) || y.wire || y.tag>3) return false;if(y.tag==1) {if(y.v>(x.tag==1 ? 8:3) || (flags&1)) return false;flags|=1;}else if(y.tag==2) {if(x.tag==1 && y.v>=types) return false;if(flags&2) return false;flags|=2;}else {if(flags&4) return false;flags|=4;len=y.v;}}
        if(x.tag==1) {if(!(flags&4) || len>bytes-total) return false;total+=len;}else ++columns;
    }else if(x.tag==3) {if(x.wire!=2 || !bounded_utf8(b->p+(size_t)x.at,(size_t)x.n,b->pd)) return false;}else return false;
    }return total==bytes && columns==types;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b;oc_field x;uint64_t at,end,ps,fl=0,ml=0,footer,meta,types=0,stripes=0,totalrows=0,wantrows=0,content=0,header=0,bodyend=3;unsigned flags=0;bool ok=false;
    if(!blob_load(f,&b,pd)) { return false; } BLOB_NEED(blob_span(&b,0,4) && !xx_rt_memcmp(b.p,"ORC",3));ps=b.p[(size_t)b.n-1];BLOB_NEED(ps && ps<=b.n-4);at=b.n-1-ps;end=b.n-1;
    while(at<end) {BLOB_NEED(oc_next(&b,&at,end,&x));if(x.tag==1 || x.tag==2 || x.tag==5 || x.tag==7) {unsigned mask=x.tag==1 ? 1:x.tag==2 ? 2:x.tag==5 ? 4:8;BLOB_NEED(!x.wire && !(flags&mask));flags|=mask;if(x.tag==1) fl=x.v;else if(x.tag==5) ml=x.v;else BLOB_NEED(!x.v);}
        else if(x.tag==8000) BLOB_NEED(x.wire==2 && x.n==3 && !xx_rt_memcmp(b.p+(size_t)x.at,"ORC",3));
        else if(x.tag==4) {uint64_t p=x.at,v[2];BLOB_NEED(x.wire==2 && container_codec_var(&b,&p,x.at+x.n,&v[0]) && container_codec_var(&b,&p,x.at+x.n,&v[1]) && p==x.at+x.n && !v[0] && (v[1]==11 || v[1]==12));}
        else BLOB_NEED((x.tag==3 || x.tag==6) && !x.wire);
    }BLOB_NEED((flags&1) && fl<=b.n-1-ps-3 && ml<=b.n-1-ps-3-fl);footer=b.n-1-ps-fl;meta=footer-ml;at=footer;end=footer+fl;
    while(at<end) {BLOB_NEED(oc_next(&b,&at,end,&x));if(x.tag==4) {BLOB_NEED(x.wire==2 && ++types<=4094);}else if(x.tag==3) {BLOB_NEED(x.wire==2 && ++stripes<=1024);}else if(x.tag==1) {BLOB_NEED(!x.wire);header=x.v;}else if(x.tag==2) {BLOB_NEED(!x.wire);content=x.v;}else if(x.tag==6) {BLOB_NEED(!x.wire);wantrows=x.v;}
        else if(x.tag==5 || x.tag==7) BLOB_NEED(x.wire==2 && oc_proto(&b,x.at,x.n));else if(x.tag==8 || x.tag==9 || x.tag==11) BLOB_NEED(!x.wire);else if(x.tag==12) BLOB_NEED(x.wire==2 && bounded_utf8(b.p+(size_t)x.at,(size_t)x.n,pd));else BLOB_NEED(false);
    }BLOB_NEED(types && header==3 && oc_proto(&b,meta,ml));BLOB_NEED(blob_add(f,s,&b,"orc-header",0,3));at=footer;
    while(at<end) {BLOB_NEED(oc_next(&b,&at,end,&x));if(x.tag==4) BLOB_NEED(oc_type(&b,x.at,x.n,types));
        if(x.tag==3) {uint64_t p=x.at,e=p+x.n,v[5]={0,0,0,0,0},sum;unsigned seen=0;oc_field y;while(p<e) {BLOB_NEED(oc_next(&b,&p,e,&y) && !y.wire && y.tag>=1 && y.tag<=5 && !(seen&(1U<<(unsigned)y.tag)));seen|=1U<<(unsigned)y.tag;v[y.tag-1]=y.v;}
            BLOB_NEED(v[0]==bodyend && v[1]<=meta-v[0] && v[2]<=meta-v[0]-v[1] && v[3]<=meta-v[0]-v[1]-v[2] && v[4]<=UINT64_MAX-totalrows);sum=v[1]+v[2]+v[3];BLOB_NEED(v[3] && oc_stripe_footer(&b,v[0]+v[1]+v[2],v[3],types,v[1]+v[2]));BLOB_NEED(blob_add(f,s,&b,"stripe",v[0],sum));bodyend+=sum;totalrows+=v[4];}
    }BLOB_NEED(bodyend==meta && content==bodyend-header && totalrows==wantrows);
    BLOB_NEED(blob_add(f,s,&b,"metadata",meta,ml) && blob_add(f,s,&b,"footer",footer,fl) && blob_add(f,s,&b,"postscript",b.n-1-ps,ps+1));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_apache_orc_init(xx_apache_orc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_APACHE_ORC,"orc"); } }
xx_apache_orc *xx_apache_orc_create(xx_io_device *d,int64_t b) { xx_apache_orc *r=(xx_apache_orc *)xx_mem_alloc(sizeof(*r)); if(r) xx_apache_orc_init(r,d,b); return r; }
void xx_apache_orc_destroy(xx_apache_orc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_apache_orc_free(xx_apache_orc *r) { if(r) { xx_apache_orc_destroy(r); xx_mem_free(r); } }
bool xx_apache_orc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_apache_orc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
