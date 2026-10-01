/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/python/cpython/blob/3.13/Python/marshal.c */
#include "xxfclib/formats/python_marshal/xx_python_marshal.h"
#include "../xx_tenth_data.h"
typedef struct tm_state {nh_blob *b;Abstractformat *f;pm_stream *s;uint64_t at;unsigned refs,objects;uint8_t kinds[4096];} tm_state;
static bool tm_object(tm_state *t,unsigned depth,uint8_t *kind) {
    nh_blob *b=t->b;uint64_t start=t->at,bytes=0;uint8_t code,flag,k=1;unsigned ref=4096,i;uint32_t n=0;
    if(depth>64 || ++t->objects>16384 || !nh_span(b,t->at,1)) return false;code=b->p[(size_t)t->at++];flag=code&128;code&=127;
    if(flag) {if(t->refs>=4096 || code=='r' || code=='0') return false;ref=t->refs++;t->kinds[ref]=2;}
    switch(code) {
    case 'N':case 'F':case 'T':case '.':break;
    case 'i':bytes=4;break;case 'I':bytes=8;break;
    case 'g':case 'y':bytes=code=='g' ? 8:16;if(!nh_floats(b,t->at,bytes,8,false)) return false;break;
    case 'r':if(!nh_span(b,t->at,4)) return false;n=pm_le32(b->p+(size_t)t->at);t->at+=4;if(n>=t->refs) return false;k=t->kinds[n];break;
    case 'l': {
        int32_t signed_n;if(!nh_span(b,t->at,4)) return false;signed_n=(int32_t)pm_le32(b->p+(size_t)t->at);t->at+=4;if(signed_n==INT32_MIN) return false;n=(uint32_t)(signed_n<0 ? -signed_n:signed_n);if(n>4096 || !nh_span(b,t->at,(uint64_t)n*2)) return false;
        for(i=0;i<n;++i) if(pm_le16(b->p+(size_t)t->at+i*2)>32767) return false;if(n && !pm_le16(b->p+(size_t)t->at+(n-1)*2)) return false;bytes=(uint64_t)n*2;break;
    }
    case 's':case 't':case 'u':case 'a':case 'A':case 'z':case 'Z': {
        bool ascii=code=='a' || code=='A' || code=='z' || code=='Z';if(code=='z' || code=='Z') {if(!nh_span(b,t->at,1)) return false;n=b->p[(size_t)t->at++];}else {if(!nh_span(b,t->at,4)) return false;n=pm_le32(b->p+(size_t)t->at);t->at+=4;}
        if(n>16777216 || !nh_span(b,t->at,n)) return false;if(code!='s' && !fourth_utf8(b->p+(size_t)t->at,n,b->pd)) return false;if(ascii) for(i=0;i<n;++i) if(b->p[(size_t)t->at+i]>127) return false;bytes=n;break;
    }
    case '(':case ')':case '[':case '<':case '>': {
        bool hashable=code=='(' || code==')' || code=='>';k=(code=='[' || code=='<') ? 2:1;if(code==')') {if(!nh_span(b,t->at,1)) return false;n=b->p[(size_t)t->at++];}else {if(!nh_span(b,t->at,4)) return false;n=pm_le32(b->p+(size_t)t->at);t->at+=4;}
        if(n>4090) return false;if(depth==0 && !nh_add(t->f,t->s,b,"collection-header",start,t->at-start)) return false;
        for(i=0;i<n;++i) {uint8_t child;uint64_t at=t->at;if(!tm_object(t,depth+1,&child) || ((code=='<' || code=='>') && child!=1)) return false;if(hashable && child!=1) k=2;if(depth==0 && !nh_add(t->f,t->s,b,"item",at,t->at-at)) return false;}break;
    }
    case '{': {
        k=2;if(depth==0 && !nh_add(t->f,t->s,b,"collection-header",start,1)) return false;
        for(i=0;i<4090;++i) {uint8_t child;uint64_t at=t->at;if(!nh_span(b,at,1)) return false;if(b->p[(size_t)at]=='0') {++t->at;if(depth==0 && !nh_add(t->f,t->s,b,"end",at,1)) return false;break;}
            if(!tm_object(t,depth+1,&child) || child!=1 || !tm_object(t,depth+1,&child)) return false;if(depth==0 && !nh_add(t->f,t->s,b,"entry",at,t->at-at)) return false;
        }if(i==4090) return false;break;
    }
    default:return false;
    }
    if(!nh_span(b,t->at,bytes)) return false;t->at+=bytes;if(ref<4096) t->kinds[ref]=k;*kind=k;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};tm_state t;bool ok=false;uint8_t kind,code;xx_mem_zero(&t,sizeof(t));NH_NEED(nh_load(f,&b,pd) && b.n>=10);code=b.p[0]&127;
    NH_NEED((b.p[0]&128) && (code=='{' || code=='[' || code=='(' || code==')' || code=='<' || code=='>'));t.b=&b;t.f=f;t.s=s;
    NH_NEED(tm_object(&t,0,&kind) && t.at==b.n && s->count>=2);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_python_marshal_init(xx_python_marshal *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PYTHON_MARSHAL,"python_marshal"); } }
xx_python_marshal *xx_python_marshal_create(xx_io_device *d,int64_t b) { xx_python_marshal *r=(xx_python_marshal *)xx_mem_alloc(sizeof(*r)); if(r) xx_python_marshal_init(r,d,b); return r; }
void xx_python_marshal_destroy(xx_python_marshal *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_python_marshal_free(xx_python_marshal *r) { if(r) { xx_python_marshal_destroy(r); xx_mem_free(r); } }
bool xx_python_marshal_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_python_marshal_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
