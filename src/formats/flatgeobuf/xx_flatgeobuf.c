/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/flatgeobuf/flatgeobuf/tree/master/src/fbs */
#include "xxfclib/formats/flatgeobuf/xx_flatgeobuf.h"
#include "../common/xx_container_codec_helpers.h"

typedef struct fb_table {memory_blob *b;uint64_t at,vt,end;unsigned vs,object,slots;} fb_table;
static bool fb_table_at(memory_blob *b,uint64_t at,uint64_t end,unsigned slots,fb_table *t) {
    int64_t v;if(!record_span(at,4,end) || !blob_span(b,at,4)) return false;v=(int64_t)at-(int32_t)xx_data_get_u32(b->p+(size_t)at, 4, 0, false);if(v<0 || !record_span((uint64_t)v,4,end)) return false;
    t->b=b;t->at=at;t->vt=(uint64_t)v;t->end=end;t->vs=xx_data_get_u16(b->p+(size_t)v, 2, 0, false);t->object=xx_data_get_u16(b->p+(size_t)v+2, 2, 0, false);t->slots=slots;
    return t->vs>=4 && !(t->vs&1) && t->vs<=4+slots*2 && t->object>=4 && record_span(t->vt,t->vs,end) && record_span(at,t->object,end);
}
static bool fb_field(fb_table *t,unsigned slot,unsigned width,uint64_t *at) {
    unsigned o;*at=0;if(4+slot*2>=t->vs) return true;o=xx_data_get_u16(t->b->p+(size_t)t->vt+4+slot*2, 2, 0, false);if(!o) return true;if(o<4 || !record_span(o,width,t->object)) return false;*at=t->at+o;return true;
}
static bool fb_indirect(fb_table *t,unsigned slot,uint64_t *at) {uint64_t p;if(!fb_field(t,slot,4,&p)) return false;if(!p) {*at=0;return true;}*at=p+xx_data_get_u32(t->b->p+(size_t)p, 4, 0, false);return *at>p && record_span(*at,4,t->end);}
static bool fb_string(fb_table *t,unsigned slot,bool required) {uint64_t p,n;if(!fb_indirect(t,slot,&p)) return false;if(!p) return !required;n=xx_data_get_u32(t->b->p+(size_t)p, 4, 0, false);return n<=65536 && record_span(p+4,n+1,t->end) && !t->b->p[(size_t)(p+4+n)] && bounded_utf8(t->b->p+(size_t)p+4,(size_t)n,t->b->pd);}
static bool fb_vector(fb_table *t,unsigned slot,unsigned width,uint64_t *at,uint64_t *n) {uint64_t p,bytes;if(!fb_indirect(t,slot,&p)) return false;*n=0;*at=0;if(!p) return true;*n=xx_data_get_u32(t->b->p+(size_t)p, 4, 0, false);if(!binary_mul(*n,width,&bytes) || !record_span(p+4,bytes,t->end)) return false;*at=p+4;return true;}
static bool fb_scalar(fb_table *t,unsigned slot,unsigned width,uint64_t fallback,uint64_t *v) {uint64_t p;if(!fb_field(t,slot,width,&p)) return false;*v=!p ? fallback:width==1 ? t->b->p[(size_t)p]:width==2 ? xx_data_get_u16(t->b->p+(size_t)p, 2, 0, false):width==4 ? xx_data_get_u32(t->b->p+(size_t)p, 4, 0, false):xx_data_get_u64(t->b->p+(size_t)p, 8, 0, false);return true;}
static bool fb_columns(fb_table *t,unsigned slot,uint8_t *types,uint64_t *count) {
    uint64_t p,n,i,at,v;fb_table c;if(!fb_vector(t,slot,4,&p,&n) || n>256) return false;*count=n;
    for(i=0;i<n;++i) {at=p+i*4;v=xx_data_get_u32(t->b->p+(size_t)at, 4, 0, false);if(!v || !fb_table_at(t->b,at+v,t->end,11,&c) || !fb_string(&c,0,true) || !fb_string(&c,2,false) || !fb_string(&c,3,false) || !fb_string(&c,10,false) || !fb_scalar(&c,1,1,0,&v) || v>14) return false;types[i]=(uint8_t)v;
        for(unsigned j=4;j<=6;++j) if(!fb_scalar(&c,j,4,0xffffffffU,&v)) return false;
        for(unsigned j=7;j<=9;++j) if(!fb_scalar(&c,j,1,j==7,&v) || v>1) return false;
    }return true;
}
static bool fb_geometry(memory_blob *b,uint64_t at,uint64_t end,unsigned depth,unsigned dims) {
    fb_table t;uint64_t p,n,points=0,v,parts=0,i,ends=0,prev=0;unsigned j;if(depth>16 || !fb_table_at(b,at,end,8,&t) || !fb_scalar(&t,6,1,0,&v) || v>17 || !fb_vector(&t,1,8,&p,&n) || n%2 || n>1000000 || !blob_floats(b,p,n*8,8,false)) return false;points=n/2;
    if(!fb_vector(&t,0,4,&p,&n)) { return false; } ends=n;for(i=0;i<n;++i) {v=xx_data_get_u32(b->p+(size_t)(p+i*4), 4, 0, false);if(v<=prev || v>points) return false;prev=v;}if(ends && prev!=points) return false;
    for(j=2;j<=5;++j) {if(!fb_vector(&t,j,8,&p,&n) || (n && n!=points) || ((dims&(1U<<(j-2))) && points && n!=points) || (j!=5 && !blob_floats(b,p,n*8,8,false))) return false;}
    if(!fb_vector(&t,7,4,&p,&parts) || parts>1024) { return false; } for(i=0;i<parts;++i) {uint64_t q=p+i*4,off=xx_data_get_u32(b->p+(size_t)q, 4, 0, false);if(!off || !fb_geometry(b,q+off,end,depth+1,dims)) return false;}
    return points || parts;
}
static bool fb_properties(fb_table *t,uint8_t *types,uint64_t columns) {
    uint64_t p,n,at,end,len;uint8_t seen[256];if(!fb_vector(t,1,1,&p,&n)) return false;at=p;end=p+n;xx_mem_zero(seen,sizeof(seen));
    while(at<end) {unsigned col,type,width; if(at+1==end && !t->b->p[(size_t)at]) {++at;break;} if(!record_span(at,2,end)) return false;col=xx_data_get_u16(t->b->p+(size_t)at, 2, 0, false);at+=2;if(col>=columns || seen[col]) return false;seen[col]=1;type=types[col];width=type<=2 ? 1:type<=4 ? 2:type<=6 || type==9 ? 4:type<=10 ? 8:0;
        if(width) {if(!record_span(at,width,end) || (type==2 && t->b->p[(size_t)at]>1) || ((type==9 || type==10) && !blob_floats(t->b,at,width,width,false))) return false;at+=width;}
        else {if(!record_span(at,4,end)) return false;len=xx_data_get_u32(t->b->p+(size_t)at, 4, 0, false);at+=4;if(!record_span(at,len,end) || (type!=14 && !bounded_utf8(t->b->p+(size_t)at,(size_t)len,t->b->pd))) return false;at+=len;}
    }return at==end;
}
static bool fb_bbox(memory_blob *b,uint64_t at) {
    union {uint64_t u;double d;} v[4];unsigned i;if(!blob_span(b,at,32)) return false;for(i=0;i<4;++i) {v[i].u=xx_data_get_u64(b->p+(size_t)at+i*8, 8, 0, false);if(!numeric_finite64(v[i].u)) return false;}return v[0].d<=v[2].d && v[1].d<=v[3].d;
}
static bool fb_contains(memory_blob *b,uint64_t parent,uint64_t child) {
    union {uint64_t u;double d;} p[4],c[4];unsigned i;
    for(i=0;i<4;++i) {p[i].u=xx_data_get_u64(b->p+(size_t)parent+i*8, 8, 0, false);c[i].u=xx_data_get_u64(b->p+(size_t)child+i*8, 8, 0, false);}
    return p[0].d<=c[0].d && p[1].d<=c[1].d && p[2].d>=c[2].d && p[3].d>=c[3].d;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b,hb,fb;fb_table t,c;uint8_t types[256];uint64_t h,at,n,p,v,count,columns,index=0,nodes=0,levels[16],starts[16],depth=0,featurebase,offsets[1024],i,j;unsigned dims=0;bool ok=false;
    if(!blob_load(f,&b,pd)) { return false; } BLOB_NEED(blob_span(&b,0,16) && !xx_rt_memcmp(b.p,"fgb\x03" "fgb\0",8));h=xx_data_get_u32(b.p+8, 4, 0, false);BLOB_NEED(h>=8 && h<=1048576 && blob_span(&b,12,h));hb.p=b.p+12;hb.n=h;hb.pd=pd;at=xx_data_get_u32(hb.p, 4, 0, false);BLOB_NEED(at>=4 && fb_table_at(&hb,at,h,14,&t));
    BLOB_NEED(fb_string(&t,0,false) && fb_string(&t,11,false) && fb_string(&t,12,false) && fb_string(&t,13,false) && fb_vector(&t,1,8,&p,&n) && (!n || n==4 || n==6 || n==8) && blob_floats(t.b,p,n*8,8,false) && fb_scalar(&t,2,1,0,&v) && v<=17);
    for(i=3;i<=6;++i) {BLOB_NEED(fb_scalar(&t,(unsigned)i,1,0,&v) && v<=1);if(v) dims|=1U<<(unsigned)(i-3);}
    BLOB_NEED(fb_columns(&t,7,types,&columns) && fb_scalar(&t,8,8,0,&count) && count && count<=1024 && fb_scalar(&t,9,2,16,&n) && (n==0 || n>=2));
    BLOB_NEED(fb_indirect(&t,10,&p));if(p) {BLOB_NEED(fb_table_at(t.b,p,t.end,6,&c));for(i=0;i<6;++i) {if(i==1) BLOB_NEED(fb_scalar(&c,1,4,0,&v));else BLOB_NEED(fb_string(&c,(unsigned)i,false));}}
    at=12+h;if(n) {levels[depth++]=count;nodes=count;p=count;do {BLOB_NEED(depth<16);p=(p+n-1)/n;levels[depth++]=p;nodes+=p;} while(p!=1);p=nodes;for(i=0;i<depth;++i) {p-=levels[i];starts[i]=p;}index=nodes*40;BLOB_NEED(blob_span(&b,at,index));for(i=0;i<nodes;++i) BLOB_NEED(fb_bbox(&b,at+i*40));
        for(i=1;i<depth;++i) for(j=0;j<levels[i];++j) {uint64_t parent=at+(starts[i]+j)*40,child=starts[i-1]+j*n;BLOB_NEED(xx_data_get_u64(b.p+(size_t)parent+32, 8, 0, false)==child);for(uint64_t q=child;q<starts[i-1]+levels[i-1] && q<child+n;++q) BLOB_NEED(fb_contains(&b,parent,at+q*40));}
    }
    BLOB_NEED(blob_add(f,s,&b,"flatgeobuf-header",0,at));if(index) BLOB_NEED(blob_add(f,s,&b,"spatial-index",at,index));at+=index;featurebase=at;
    for(i=0;i<count;++i) {uint64_t end,size;offsets[i]=at-featurebase;BLOB_NEED(blob_span(&b,at,8));size=xx_data_get_u32(b.p+(size_t)at, 4, 0, false);BLOB_NEED(size>=8 && size<=8388608 && blob_span(&b,at+4,size));end=at+4+size;fb.p=b.p+(size_t)at+4;fb.n=size;fb.pd=pd;p=xx_data_get_u32(fb.p, 4, 0, false);BLOB_NEED(p>=4 && fb_table_at(&fb,p,size,3,&t) && fb_indirect(&t,0,&p) && p && fb_geometry(&fb,p,size,0,dims) && fb_properties(&t,types,columns));BLOB_NEED(fb_indirect(&t,2,&p) && !p);BLOB_NEED(blob_add(f,s,&b,"feature",at,size+4));at=end;}
    BLOB_NEED(at==b.n);if(index) for(i=0;i<count;++i) BLOB_NEED(xx_data_get_u64(b.p+(size_t)(12+h+(nodes-count+i)*40)+32, 8, 0, false)==offsets[i]);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_flatgeobuf_init(xx_flatgeobuf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FLATGEOBUF,"fgb"); } }
xx_flatgeobuf *xx_flatgeobuf_create(xx_io_device *d,int64_t b) { xx_flatgeobuf *r=(xx_flatgeobuf *)xx_mem_alloc(sizeof(*r)); if(r) xx_flatgeobuf_init(r,d,b); return r; }
void xx_flatgeobuf_destroy(xx_flatgeobuf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_flatgeobuf_free(xx_flatgeobuf *r) { if(r) { xx_flatgeobuf_destroy(r); xx_mem_free(r); } }
bool xx_flatgeobuf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_flatgeobuf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
