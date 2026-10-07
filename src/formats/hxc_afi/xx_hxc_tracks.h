/* SPDX-License-Identifier: MIT
 * Original bounded component primitives for documented floppy containers.
 * Format facts are referenced in the individual parsers; no upstream code.
 */
#ifndef XX_HXC_TRACKS_PRIVATE_H
#define XX_HXC_TRACKS_PRIVATE_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/data/xx_data.h"

#define HX_MAX_FILE (64U*1024U*1024U)
#define HX_MAX_OUTPUT (64U*1024U*1024U)
typedef struct hx_blob {uint8_t *p;uint64_t n,output,work;xx_pd_struct *pd;} hx_blob;
typedef struct hx_range {uint64_t at,n;} hx_range;
static bool hx_poll(hx_blob *b) {return !b->pd || !xx_pd_is_stopped(b->pd);}
static bool hx_span(hx_blob *b,uint64_t a,uint64_t n) {return hx_poll(b)&&a<=b->n&&n<=b->n-a;}
static bool hx_work(hx_blob *b,uint64_t n) {if(!hx_poll(b)||n>4U*HX_MAX_FILE-b->work)return false;b->work+=n;return true;}
static bool hx_limit(Abstractformat *f,uint32_t id,uint64_t n){const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,id);uint64_t limit;if(!v)return true;
 switch((xx_var_type_t)v->type){case XX_VAR_TYPE_UINT8:case XX_VAR_TYPE_UINT16:case XX_VAR_TYPE_UINT32:case XX_VAR_TYPE_UINT64:limit=xx_var_get_u64(v);break;
 case XX_VAR_TYPE_INT8:case XX_VAR_TYPE_INT16:case XX_VAR_TYPE_INT32:case XX_VAR_TYPE_INT64:if(xx_var_get_i64(v)<0)return false;limit=(uint64_t)xx_var_get_i64(v);break;default:return false;}
 return n<=limit;
}
static uint8_t *hx_alloc(Abstractformat *f,hx_blob *b,size_t n){if(!hx_poll(b)||n>HX_MAX_OUTPUT-b->output||!hx_limit(f,XX_META_ID_OPT_MAX_MEMBER_SIZE,n)||!hx_limit(f,XX_META_ID_OPT_MEMORY_LIMIT,b->n+b->output+(n?n:1U)))return NULL;return (uint8_t *)xx_mem_alloc(n?n:1U);}
static XXFC_MAYBE_UNUSED bool hx_load(Abstractformat *f,hx_blob *b,xx_pd_struct *pd) {
 int64_t n=pm_available(f);uint64_t a=0;
 xx_mem_zero(b,sizeof(*b));b->pd=pd;
 if(n<1 || n>HX_MAX_FILE || !hx_poll(b))return false;
 /* Every reader has at least a four-byte descriptor or a larger track and
  * info member. Reject an impossible output ceiling before reading bodies. */
 if(!hx_limit(f,XX_META_ID_OPT_MEMORY_LIMIT,(uint64_t)n)||!hx_limit(f,XX_META_ID_OPT_MAX_MEMBER_SIZE,4U))return false;
 b->n=(uint64_t)n;b->p=(uint8_t *)xx_mem_alloc((size_t)n);
 if(!b->p)return false;
 while(a<b->n){size_t z=(size_t)(b->n-a>65536U?65536U:b->n-a);if(!hx_poll(b)||!pm_read(f,(int64_t)a,b->p+a,z)){xx_mem_free(b->p);b->p=NULL;return false;}a+=z;}
 return true;
}
static XXFC_MAYBE_UNUSED bool hx_tag(hx_blob *b,uint64_t a,const char *p,size_t n) {return hx_span(b,a,n)&&!xx_rt_memcmp(b->p+a,p,n);}
static XXFC_MAYBE_UNUSED bool hx_zero(hx_blob *b,uint64_t a,uint64_t n) {uint64_t i;if(!hx_span(b,a,n)||!hx_work(b,n))return false;for(i=0;i<n;++i){if(!(i&4095U)&&!hx_poll(b))return false;if(b->p[a+i])return false;}return true;}
static XXFC_MAYBE_UNUSED bool hx_emit(Abstractformat *f,pm_stream *s,hx_blob *b,const char *name,uint64_t a,uint64_t n) {return s->count<4096U&&hx_span(b,a,n)&&hx_limit(f,XX_META_ID_OPT_MAX_MEMBER_SIZE,n)&&pm_add(f,s,name,(int64_t)a,(int64_t)n);}
static bool hx_owned(Abstractformat *f,pm_stream *s,hx_blob *b,const char *name,uint8_t *p,size_t n) {
 if(!p||!hx_poll(b)||n>HX_MAX_OUTPUT-b->output||!hx_limit(f,XX_META_ID_OPT_MAX_MEMBER_SIZE,n)||!hx_limit(f,XX_META_ID_OPT_MEMORY_LIMIT,b->n+b->output+(n?n:1U))||s->count>=4096U||!pm_add(f,s,name,0,0))return false;
 b->output+=n;s->items[s->count-1U].memory=p;s->items[s->count-1U].size=(int64_t)n;return true;
}
static XXFC_MAYBE_UNUSED bool hx_text(Abstractformat *f,pm_stream *s,hx_blob *b,const char *text) {size_t n=xx_rt_strlen(text);uint8_t *p=hx_alloc(f,b,n);if(!p)return false;xx_rt_memcpy(p,text,n);if(!hx_owned(f,s,b,"container-info.txt",p,n)){xx_mem_free(p);return false;}return true;}
static XXFC_MAYBE_UNUSED bool hx_claim(hx_blob *b,hx_range *r,unsigned *count,uint64_t at,uint64_t n) {
 unsigned i;if(!n||*count>=4096U||!hx_span(b,at,n))return false;
 for(i=0;i<*count;++i){if(!hx_work(b,1)|| (at<r[i].at+r[i].n&&r[i].at<at+n))return false;}
 r[*count].at=at;r[(*count)++].n=n;return true;
}
static XXFC_MAYBE_UNUSED uint16_t hx_crc16(const uint8_t *p,size_t n) {uint16_t c=0xffffU;size_t i;unsigned k;for(i=0;i<n;++i){c^=(uint16_t)p[i]<<8;for(k=0;k<8;++k)c=(uint16_t)((c<<1)^((c&0x8000U)?0x1021U:0));}return c;}
static XXFC_MAYBE_UNUSED bool hx_ccitt(hx_blob *b,uint64_t a,uint64_t n) {uint64_t i;uint16_t c=0xffffU;unsigned k;if(!hx_span(b,a,n)||!hx_work(b,n))return false;for(i=0;i<n;++i){if(!(i&4095U)&&!hx_poll(b))return false;c^=(uint16_t)b->p[a+i]<<8;for(k=0;k<8;++k)c=(uint16_t)((c<<1)^((c&0x8000U)?0x1021U:0));}return c==0;}
/* Pauline calls IEEE CRC-32 with the public initial value 0xffffffff.
 * Its complemented API therefore starts the internal remainder at zero. */
static XXFC_MAYBE_UNUSED bool hx_pauline_crc(hx_blob *b,uint64_t a,uint64_t n,uint32_t expected) {uint64_t i;uint32_t c=0;unsigned k;if(!hx_span(b,a,n)||!hx_work(b,n))return false;for(i=0;i<n;++i){if(!(i&4095U)&&!hx_poll(b))return false;c^=b->p[a+i];for(k=0;k<8;++k)c=(c>>1)^((c&1U)?0xedb88320U:0);}return (c^0xffffffffU)==expected;}
typedef struct hx_sink {uint8_t *p;size_t n,cap;xx_pd_struct *pd;} hx_sink;
static ssize_t hx_sink_write(xx_io_device *d,const void *p,size_t n) {hx_sink *s=(hx_sink *)d->priv;if(!s||(s->pd&&xx_pd_is_stopped(s->pd))||n>s->cap-s->n)return -1;xx_rt_memcpy(s->p+s->n,p,n);s->n+=n;return (ssize_t)n;}
static bool hx_zlib(hx_blob *b,const uint8_t *p,size_t n,uint8_t *out,size_t plain) {
 hx_sink sink;xx_io_device d;size_t used=0,i;uint32_t a=1,c=0;
 if(n<6U || (p[0]&15U)!=8U || (p[0]>>4)>7U || (((unsigned)p[0]<<8)|p[1])%31U || (p[1]&32U) || !hx_poll(b))return false;
 xx_mem_zero(&sink,sizeof(sink));xx_mem_zero(&d,sizeof(d));sink.p=out;sink.cap=plain;sink.pd=b->pd;d.priv=&sink;d.write=hx_sink_write;
 if(!xx_deflate_unpack_memory_to_device_ex(p+2,n-6U,&d,&used,false,b->pd)||used!=n-6U||sink.n!=plain||!hx_work(b,plain))return false;
 for(i=0;i<plain;++i){if(!(i&4095U)&&!hx_poll(b))return false;a=(a+out[i])%65521U;c=(c+a)%65521U;}
 return ((c<<16)|a)==xx_data_get_u32(p+n-4U, 4, 0, true);
}
static XXFC_MAYBE_UNUSED bool hx_decode(Abstractformat *f,pm_stream *s,hx_blob *b,const char *name,uint64_t a,uint32_t packed,uint32_t plain,bool lz4) {
 uint8_t *out;size_t written=0;bool ok;
 if(!packed||!hx_span(b,a,packed)||plain>HX_MAX_OUTPUT-b->output||(uint64_t)plain>(uint64_t)packed*1024U||!hx_work(b,plain))return false;
 out=hx_alloc(f,b,plain);if(!out)return false;
 ok=lz4?(xx_lz4_decompress_block(b->p+a,packed,out,plain,&written)&&written==plain):hx_zlib(b,b->p+a,packed,out,plain);
 if(!ok||!hx_owned(f,s,b,name,out,plain)){xx_mem_free(out);return false;}return true;
}
/* Parsing operation limits never mutates the persistent reader. The parsers
 * use only device/base and resolved budgets, so a private format snapshot can
 * safely carry the two effective options into pm_open. Published iterator
 * dispatch always points back to the caller's original format. */
static XXFC_MAYBE_UNUSED xx_archive_record_state *hx_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd){Abstractformat parse;xx_archive_record_state *st=NULL;unsigned i;const uint32_t ids[]={XX_META_ID_OPT_MEMORY_LIMIT,XX_META_ID_OPT_MAX_MEMBER_SIZE};
 if(!f) {return NULL; } xx_format_init(&parse,f->device,f->base_address);parse.file_type=f->file_type;
 for(i=0;i<2;++i){const xx_var *v=xx_format_resolve_extra_parameter(f,opts,ids[i]);if(v&&!xx_format_set_extra_parameter(&parse,ids[i],v))goto done;}
 st=pm_create_records(&parse,opts,pd);if(st)st->format=f;
done:xx_format_cleanup_extra_parameters(&parse);return st;
}
#define HX_NEED(x) do{if(!(x))goto done;}while(0)
#define HX_API(stem,type,ext) \
 void xx_##stem##_init(xx_##stem *r,xx_io_device *d,int64_t a){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,a,type,ext);r->format.create_archive_records_reading=hx_records;}} \
 xx_##stem *xx_##stem##_create(xx_io_device *d,int64_t a){xx_##stem *r=(xx_##stem *)xx_mem_alloc(sizeof(*r));if(r)xx_##stem##_init(r,d,a);return r;} \
 void xx_##stem##_destroy(xx_##stem *r){if(r)xx_format_cleanup_extra_parameters(&r->format);} \
 void xx_##stem##_free(xx_##stem *r){if(r){xx_##stem##_destroy(r);xx_mem_free(r);}} \
 bool xx_##stem##_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);} \
 bool xx_##stem##_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
#endif
